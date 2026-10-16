/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "master_list.h"
#include <string.h>
#define TEXT_LIMIT (2u*1024u*1024u)
#define ENTRY_LIMIT 10000u
static gboolean root_valid(const gchar *root) { return root && g_path_is_absolute(root) && strlen(root)<=16384 && g_utf8_validate(root,-1,NULL) && !strpbrk(root,"\r\n"); }
static InsightMasterList *list_new(void) { InsightMasterList *list=g_new0(InsightMasterList,1);list->files=g_ptr_array_new_with_free_func(g_free);list->trees=g_ptr_array_new_with_free_func(g_free);return list; }
void insight_master_list_free(InsightMasterList *list) { if(!list) return;g_ptr_array_unref(list->files);g_ptr_array_unref(list->trees);g_free(list->etag);g_free(list->digest);g_free(list->text);g_free(list->membership_digest);g_free(list); }
static gchar *path_key(const gchar *path) {
#ifdef G_OS_WIN32
 return g_utf8_casefold(path,-1);
#else
 return g_strdup(path);
#endif
}
static gboolean add_path(InsightMasterList *list,GHashTable *seen,const gchar *path,gboolean tree,GError **error) {
 gchar *key=path_key(path);if(g_hash_table_contains(seen,key)) { g_free(key);return TRUE; }
 if(list->files->len+list->trees->len>=ENTRY_LIMIT) { g_free(key);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Master file list exceeds 10000 entries");return FALSE; }
 g_hash_table_add(seen,key);g_ptr_array_add(tree ? list->trees : list->files,g_strdup(path));return TRUE;
}
static gint fingerprint_compare(gconstpointer a,gconstpointer b) { return strcmp(*(gchar *const *)a,*(gchar *const *)b); }
void insight_master_list_rehash(InsightMasterList *list) {
 g_free(list->membership_digest);
 GChecksum *sum=g_checksum_new(G_CHECKSUM_SHA256);
 for(guint kind=0;kind<2;kind++) {
  GPtrArray *source=kind ? list->trees : list->files,*sorted=g_ptr_array_new();
  for(guint i=0;i<source->len;i++) g_ptr_array_add(sorted,g_ptr_array_index(source,i));
  g_ptr_array_sort(sorted,fingerprint_compare);guchar marker=(guchar)kind;g_checksum_update(sum,&marker,1);
  for(guint i=0;i<sorted->len;i++) { const gchar *path=g_ptr_array_index(sorted,i);g_checksum_update(sum,(const guchar *)path,strlen(path)+1); }
  g_ptr_array_unref(sorted);
 }
 list->membership_digest=g_strdup(g_checksum_get_string(sum));g_checksum_free(sum);
}
InsightMasterTypes *insight_master_types_copy(const InsightMasterTypes *types) {
 if(!types) return NULL;
 InsightMasterTypes *copy=g_new0(InsightMasterTypes,1);copy->patterns=(const gchar *const *)g_strdupv((gchar **)types->patterns);copy->extensions=(const gchar *const *)g_strdupv((gchar **)types->extensions);copy->files=(const gchar *const *)g_strdupv((gchar **)types->files);return copy;
}
void insight_master_types_free(InsightMasterTypes *types) { if(types) { g_strfreev((gchar **)types->patterns);g_strfreev((gchar **)types->extensions);g_strfreev((gchar **)types->files);g_free(types); } }
typedef struct { GPtrArray *patterns;GHashTable *extensions,*files; } KnownTypes;
static void known_free(KnownTypes *known) { if(known) { g_ptr_array_unref(known->patterns);g_hash_table_unref(known->extensions);g_hash_table_unref(known->files);g_free(known); } }
static KnownTypes *known_new(const InsightMasterTypes *types,GError **error) {
 KnownTypes *known=g_new0(KnownTypes,1);known->patterns=g_ptr_array_new_with_free_func((GDestroyNotify)g_pattern_spec_free);known->extensions=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);known->files=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 const gchar *const *lists[]={types->patterns,types->extensions,types->files};
 for(guint kind=0;kind<3;kind++) for(guint n=0;lists[kind] && lists[kind][n];n++) {
  const gchar *value=lists[kind][n];if(n>=(kind==2 ? 10000u : 4096u)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid known file type snapshot for master file list");known_free(known);return NULL; }if(!*value) continue;
  if(strlen(value)>(kind==2 ? 16384u : 1024u) || !g_utf8_validate(value,-1,NULL) || strpbrk(value,"\r\n") || (kind==2 && !g_path_is_absolute(value))) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid known file type snapshot for master file list");known_free(known);return NULL; }
  if(!kind) { gchar *pattern=path_key(value);g_ptr_array_add(known->patterns,g_pattern_spec_new(pattern));g_free(pattern); }
  else if(kind==1) g_hash_table_add(known->extensions,g_strdup(value));
  else { gchar *canonical=g_canonicalize_filename(value,NULL);g_hash_table_add(known->files,path_key(canonical));g_free(canonical); }
 }
 return known;
}
static gboolean known_match(const KnownTypes *known,const gchar *path,const gchar *name) {
 if(!known) return TRUE;
 const gchar *ext=strrchr(name,'.');if(ext && g_hash_table_contains(known->extensions,ext+1)) return TRUE;
 gchar *key=path_key(path);gboolean found=g_hash_table_contains(known->files,key);g_free(key);if(found) return TRUE;
 gchar *folded=path_key(name);for(guint n=0;n<known->patterns->len && !found;n++) found=g_pattern_spec_match_string(g_ptr_array_index(known->patterns,n),folded);g_free(folded);return found;
}
static gboolean expand_pattern(InsightMasterList *list,GHashTable *seen,const gchar *path,guint *visited,const KnownTypes *known,GCancellable *cancel,GError **error) {
 gchar *directory=g_path_get_dirname(path),*pattern=g_path_get_basename(path);gboolean valid=TRUE;
 if((strpbrk(directory,"*?") && !g_file_test(directory,G_FILE_TEST_IS_DIR)) || strlen(pattern)>1024) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Master file list wildcards require a literal directory and a filename pattern up to 1024 bytes");valid=FALSE; }
 GFile *folder=valid ? g_file_new_for_path(directory) : NULL;
 GFileEnumerator *entries=folder ? g_file_enumerate_children(folder,G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,error) : NULL;
 if(valid && !entries) valid=FALSE;
#ifdef G_OS_WIN32
 gchar *folded=g_utf8_casefold(pattern,-1);g_free(pattern);pattern=folded;
#endif
 GPatternSpec *spec=valid ? g_pattern_spec_new(pattern) : NULL;
 while(valid) {
  GError *next_error=NULL;GFileInfo *info=g_file_enumerator_next_file(entries,cancel,&next_error);
  if(!info) { if(next_error) { g_propagate_error(error,next_error);valid=FALSE; }break; }
  if(++*visited>100000) { g_object_unref(info);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Master file list wildcard scan exceeds 100000 entries");valid=FALSE;break; }
  const gchar *name=g_file_info_get_name(info);
  if(g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR && name && g_utf8_validate(name,-1,NULL)) {
#ifdef G_OS_WIN32
   gchar *match=g_utf8_casefold(name,-1);
#else
   const gchar *match=name;
#endif
   if(g_pattern_spec_match_string(spec,match)) { gchar *child=g_build_filename(directory,name,NULL);if(strlen(child)>16384) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Master file list path exceeds 16384 bytes");valid=FALSE; }else if(known_match(known,child,name)) valid=add_path(list,seen,child,FALSE,error);g_free(child); }
#ifdef G_OS_WIN32
   g_free(match);
#endif
  }
  g_object_unref(info);
 }
 if(spec) { g_pattern_spec_free(spec); }g_clear_object(&entries);g_clear_object(&folder);g_free(pattern);g_free(directory);return valid;
}
static InsightMasterList *parse(const gchar *text,gsize length,const gchar *root,const InsightMasterTypes *types,GCancellable *cancel,GError **error) {
 if(!root_valid(root) || !text || length>TEXT_LIMIT || memchr(text,0,length) || !g_utf8_validate(text,(gssize)length,NULL)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Master file list must be UTF-8 text up to 2 MiB with an absolute root");return NULL; }
 KnownTypes *known=types ? known_new(types,error) : NULL;if(types && !known) return NULL;
 InsightMasterList *list=list_new();list->digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)text,length);GHashTable *seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);gchar *copy=g_strndup(text,length);const gchar *start=g_str_has_prefix(copy,"\xEF\xBB\xBF") ? copy+3 : copy;gchar **lines=g_strsplit(start,"\n",-1);gboolean valid=TRUE;guint visited=0,patterns=0;
 for(guint i=0;lines[i];i++) {
  if(g_cancellable_set_error_if_cancelled(cancel,error)) { valid=FALSE;break; }
  gchar *line=lines[i];gsize size=strlen(line);if(size && line[size-1]=='\r') line[--size]=0;if(!size || line[0]==';') continue;
  if(size>16384 || strchr(line,'\r') || (g_ascii_isalpha(line[0]) && line[1]==':' && (line[2]=='/' || line[2]=='\\') && !g_path_is_absolute(line)) || (g_str_has_prefix(line,"\\\\") && !g_path_is_absolute(line))) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Unsupported master file list path");valid=FALSE;break; }
  gboolean tree=G_IS_DIR_SEPARATOR(line[size-1]);gchar *path=g_canonicalize_filename(line,root);if(strlen(path)>16384) { g_free(path);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Master file list path exceeds 16384 bytes");valid=FALSE;break; }tree|=g_file_test(path,G_FILE_TEST_IS_DIR);
  if(strpbrk(path,"*?") && !g_file_test(path,G_FILE_TEST_EXISTS)) {
   if(++patterns>256) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Master file list exceeds 256 wildcard patterns");valid=FALSE; }else valid=expand_pattern(list,seen,path,&visited,NULL,cancel,error);
  }else if(tree && types) {
   if(++patterns>256) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Master file list exceeds 256 directory/wildcard rules");valid=FALSE; }
   else {
    GFile *folder=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(folder,G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,error);g_object_unref(folder);
    if(!info) valid=FALSE;
    else if(g_file_info_get_file_type(info)!=G_FILE_TYPE_DIRECTORY || g_file_info_get_is_symlink(info)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Master file list directories must be real directories, not symbolic links");valid=FALSE; }
    else { gchar *pattern=g_build_filename(path,"*",NULL);valid=expand_pattern(list,seen,pattern,&visited,known,cancel,error);g_free(pattern); }
    g_clear_object(&info);
   }
  }else valid=add_path(list,seen,path,tree,error);g_free(path);if(!valid) break;
 }
 g_strfreev(lines);list->text=copy;g_hash_table_unref(seen);known_free(known);if(!valid) { insight_master_list_free(list);return NULL; }insight_master_list_rehash(list);return list;
}
InsightMasterList *insight_master_list_parse(const gchar *text,gsize length,const gchar *root,GError **error) { return parse(text,length,root,NULL,NULL,error); }
InsightMasterList *insight_master_list_parse_typed(const gchar *text,gsize length,const gchar *root,const InsightMasterTypes *types,GError **error) { return parse(text,length,root,types,NULL,error); }
InsightMasterList *insight_master_list_read_typed(const gchar *path,const gchar *root,const InsightMasterTypes *types,GCancellable *cancel,GError **error) {
 if(!path || !root_valid(root)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid master file list source");return NULL; }
 GFile *file=g_file_new_for_path(path);GFileInputStream *stream=g_file_read(file,cancel,error);g_object_unref(file);if(!stream) return NULL;
 GFileInfo *before=g_file_input_stream_query_info(stream,G_FILE_ATTRIBUTE_ETAG_VALUE "," G_FILE_ATTRIBUTE_STANDARD_SIZE,cancel,error);GString *bytes=g_string_new(NULL);gboolean valid=before!=NULL;
 if(before && g_file_info_get_size(before)>TEXT_LIMIT) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Master file list exceeds 2 MiB");valid=FALSE; }
 char buffer[16384];while(valid) { gssize size=g_input_stream_read(G_INPUT_STREAM(stream),buffer,sizeof buffer,cancel,error);if(size<0) { valid=FALSE;break; }if(!size) break;if(bytes->len+(gsize)size>TEXT_LIMIT) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Master file list exceeds 2 MiB");valid=FALSE;break; }g_string_append_len(bytes,buffer,size); }
 GFileInfo *after=valid ? g_file_input_stream_query_info(stream,G_FILE_ATTRIBUTE_ETAG_VALUE,cancel,error) : NULL;if(valid && !after) valid=FALSE;
 if(valid && g_strcmp0(g_file_info_get_etag(before),g_file_info_get_etag(after))) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"Master file list changed while reading");valid=FALSE; }
 InsightMasterList *list=valid ? parse(bytes->str,bytes->len,root,types,cancel,error) : NULL;if(list) list->etag=g_strdup(g_file_info_get_etag(after));g_clear_object(&before);g_clear_object(&after);g_object_unref(stream);g_string_free(bytes,TRUE);return list;
}
InsightMasterList *insight_master_list_read(const gchar *path,const gchar *root,GCancellable *cancel,GError **error) { return insight_master_list_read_typed(path,root,NULL,cancel,error); }
static gint compare_lines(gconstpointer a,gconstpointer b) { return strcmp(*(gchar *const *)a,*(gchar *const *)b); }
gchar *insight_master_list_render(const InsightMasterList *list,const gchar *root,GError **error) {
 if(!list || !list->files || !list->trees || !root_valid(root) || list->files->len+list->trees->len>ENTRY_LIMIT) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid master file list snapshot");return NULL; }
 GPtrArray *lines=g_ptr_array_new_with_free_func(g_free);GHashTable *seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);GFile *base=g_file_new_for_path(root);gboolean valid=TRUE;
 for(guint kind=0;kind<2 && valid;kind++) { GPtrArray *paths=kind ? list->trees : list->files;for(guint i=0;i<paths->len;i++) {
  const gchar *path=g_ptr_array_index(paths,i);if(!path || !g_path_is_absolute(path) || strlen(path)>16384 || strpbrk(path,"\r\n") || !g_utf8_validate(path,-1,NULL)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid master file list path");valid=FALSE;break; }
  gchar *normalized=g_canonicalize_filename(path,NULL),*key=path_key(normalized);if(g_hash_table_contains(seen,key)) { g_free(key);g_free(normalized);continue; }g_hash_table_add(seen,key);GFile *file=g_file_new_for_path(normalized);gchar *relative=g_file_get_relative_path(base,file);if(!relative && g_file_equal(base,file)) relative=g_strdup(".");gchar *line=relative ? relative : g_strdup(normalized);for(gchar *p=line;*p;p++) if(G_IS_DIR_SEPARATOR(*p)) *p='/';
  if(line[0]==';') { gchar *literal=g_strconcat("./",line,NULL);g_free(line);line=literal; }
  if(kind && !g_str_has_suffix(line,"/")) { gchar *directory=g_strconcat(line,"/",NULL);g_free(line);line=directory; }g_ptr_array_add(lines,line);g_object_unref(file);g_free(normalized);
 } }
 g_ptr_array_sort(lines,compare_lines);GString *text=g_string_new(NULL);for(guint i=0;valid && i<lines->len;i++) { g_string_append_printf(text,"%s\n",(gchar *)g_ptr_array_index(lines,i));if(text->len>TEXT_LIMIT) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Master file list exceeds 2 MiB");valid=FALSE; } }
 g_object_unref(base);g_hash_table_unref(seen);g_ptr_array_unref(lines);if(!valid) { g_string_free(text,TRUE);return NULL; }return g_string_free(text,FALSE);
}
static gboolean write_snapshot(const InsightMasterList *list,const gchar *path,const gchar *root,const gchar *etag,const gchar *digest,GCancellable *cancel,gchar **new_etag,gboolean original,GError **error) {
 if(!path) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid master file list destination");return FALSE; }
 gchar *text=original && list && list->text ? g_strdup(list->text) : (original ? NULL : insight_master_list_render(list,root,error));if(!text) { if(original) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Master file list snapshot has no original text");return FALSE; }
 if(digest) { InsightMasterList *current=insight_master_list_read(path,root,cancel,error);if(!current) { g_free(text);return FALSE; }gboolean same=!g_strcmp0(digest,current->digest) && (!etag || !g_strcmp0(etag,current->etag));insight_master_list_free(current);if(!same) { g_free(text);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"Master file list changed since its snapshot");return FALSE; } }
 GFile *file=g_file_new_for_path(path);gchar *revision=NULL;gboolean saved=g_file_replace_contents(file,text,strlen(text),etag,FALSE,G_FILE_CREATE_PRIVATE,&revision,cancel,error);if(saved && new_etag) *new_etag=revision;else g_free(revision);g_object_unref(file);g_free(text);return saved;
}
gboolean insight_master_list_save(const InsightMasterList *list,const gchar *path,const gchar *root,const gchar *etag,const gchar *digest,GCancellable *cancel,gchar **new_etag,GError **error) { return write_snapshot(list,path,root,etag,digest,cancel,new_etag,FALSE,error); }
gboolean insight_master_list_restore(const InsightMasterList *list,const gchar *path,const gchar *root,const gchar *etag,const gchar *digest,GCancellable *cancel,gchar **new_etag,GError **error) { return write_snapshot(list,path,root,etag,digest,cancel,new_etag,TRUE,error); }
InsightProjectFiles *insight_master_list_policy(const InsightMasterList *list,const InsightProjectFiles *previous) {
 if(!list || !previous) return NULL;
 InsightProjectFiles *policy=insight_project_files_clone(previous);policy->automatic=FALSE;g_ptr_array_set_size(policy->files,0);g_ptr_array_set_size(policy->trees,0);for(guint kind=0;kind<2;kind++) { GPtrArray *from=kind ? list->trees : list->files,*to=kind ? policy->trees : policy->files;for(guint i=0;i<from->len;i++) g_ptr_array_add(to,g_strdup(g_ptr_array_index(from,i))); }return policy;
}
typedef struct { const InsightProjectFiles *policy;const gchar *root;GCancellable *cancel;InsightMasterList *list;GHashTable *seen,*directories;guint entries; } Collect;
static gboolean collect_file(Collect *job,const gchar *path,GError **error) { return !insight_project_files_contains(job->policy,job->root,path) || add_path(job->list,job->seen,path,FALSE,error); }
static gboolean collect_tree(Collect *job,const gchar *path,guint depth,GError **error) {
 if(g_cancellable_set_error_if_cancelled(job->cancel,error)) return FALSE;
 gchar *key=path_key(path);if(g_hash_table_contains(job->directories,key)) { g_free(key);return TRUE; }
 if(depth>=64 || g_hash_table_size(job->directories)>=4096) { g_free(key);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Master file list directory limit reached");return FALSE; }
 g_hash_table_add(job->directories,key);GFile *file=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,job->cancel,error);g_object_unref(file);if(!info) return FALSE;gboolean directory=g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY;g_object_unref(info);if(!directory) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY,"Master file list tree must be a real directory");return FALSE; }
 GDir *dir=g_dir_open(path,0,error);if(!dir) return FALSE;const gchar *name;gboolean valid=TRUE;
 while(valid && (name=g_dir_read_name(dir))) {
  if(g_cancellable_set_error_if_cancelled(job->cancel,error)) { valid=FALSE;break; }
  if(++job->entries>100000) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Master file list scan work limit reached");valid=FALSE;break; }
  if(name[0]=='.' || !strcmp(name,"node_modules") || !strcmp(name,"__pycache__")) continue;
  gchar *child=g_build_filename(path,name,NULL);GFile *entry=g_file_new_for_path(child);GFileInfo *metadata=g_file_query_info(entry,G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,job->cancel,error);g_object_unref(entry);if(!metadata) valid=FALSE;else { GFileType type=g_file_info_get_file_type(metadata);if(type==G_FILE_TYPE_DIRECTORY) valid=collect_tree(job,child,depth+1,error);else if(type==G_FILE_TYPE_REGULAR) valid=collect_file(job,child,error);g_object_unref(metadata); }g_free(child);
 }
 g_dir_close(dir);return valid;
}
InsightMasterList *insight_master_list_collect(const InsightProjectFiles *policy,const gchar *root,GCancellable *cancel,GError **error) {
 if(!policy || !root_valid(root)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid master file list membership");return NULL; }
 Collect job={policy,root,cancel,list_new(),g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL),g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL),0};gboolean valid=!g_cancellable_set_error_if_cancelled(cancel,error);
 for(guint i=0;valid && i<policy->files->len;i++) valid=collect_file(&job,g_ptr_array_index(policy->files,i),error);
 if(valid && policy->automatic) valid=collect_tree(&job,root,0,error);
 for(guint i=0;valid && i<policy->trees->len;i++) valid=collect_tree(&job,g_ptr_array_index(policy->trees,i),0,error);
 g_hash_table_unref(job.seen);g_hash_table_unref(job.directories);if(!valid) { insight_master_list_free(job.list);return NULL; }insight_master_list_rehash(job.list);return job.list;
}
