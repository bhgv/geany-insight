/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "directory_compare.h"
#include "glib_compat.h"
#include <string.h>
#include "../core/diff.h"
#define ATTRS "standard::name,standard::type,standard::is-symlink,standard::is-hidden,standard::size,time::modified,time::modified-usec,id::file,etag::value"
static void entry_free(gpointer data) {
 InsightDirectoryEntry *e=data;g_free(e->relative);g_clear_object(&e->info[0]);g_clear_object(&e->info[1]);g_free(e->detail);g_free(e);
}
InsightDirectoryOptions insight_directory_options(void) {
 InsightDirectoryOptions o={INSIGHT_DIR_CONTENT,TRUE,FALSE,0,20000,32,256u*1024*1024,NULL};return o;
}
static gboolean cancelled(GCancellable *cancel,GError **error) { return cancel && g_cancellable_set_error_if_cancelled(cancel,error); }
static gboolean directory(GFile *file,GCancellable *cancel,GError **error) {
 GFileInfo *info=g_file_query_info(file,ATTRS,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,error);if(!info) return FALSE;
 gboolean ok=!g_file_info_get_is_symlink(info) && g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY;g_object_unref(info);
 if(!ok) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY,"Choose directories that are not symbolic links.");
 return ok;
}
static gboolean scan(GFile *folder,const gchar *prefix,guint depth,gint side,
 const InsightDirectoryOptions *options,GHashTable *map,GPtrArray *rows,guint *visited,GCancellable *cancel,GError **error) {
 if(cancelled(cancel,error) || !directory(folder,cancel,error)) return FALSE;
 GFileEnumerator *reader=g_file_enumerate_children(folder,ATTRS,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,error);if(!reader) return FALSE;
 gboolean ok=TRUE;
 while(ok) {
  GFileInfo *info=g_file_enumerator_next_file(reader,cancel,error);
  if(!info) { if(error && *error) ok=FALSE;break; }
  const gchar *name=g_file_info_get_name(info);
  if(++*visited>100000u) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Directory comparison visit limit reached.");g_object_unref(info);ok=FALSE;break; }
  if(!options->hidden && (g_file_info_get_is_hidden(info) || name[0]=='.')) { g_object_unref(info);continue; }
  gchar *relative=*prefix ? g_build_filename(prefix,name,NULL) : g_strdup(name);
  if(strlen(relative)>16384) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FILENAME_TOO_LONG,"Directory comparison path limit reached.");g_free(relative);g_object_unref(info);ok=FALSE;break; }
  InsightDirectoryEntry *entry=g_hash_table_lookup(map,relative);
  if(!entry) {
   if(rows->len>=options->max_entries) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Directory comparison entry limit reached.");g_free(relative);g_object_unref(info);ok=FALSE;break; }
   entry=g_new0(InsightDirectoryEntry,1);entry->relative=g_strdup(relative);g_ptr_array_add(rows,entry);g_hash_table_insert(map,entry->relative,entry);
  }
  entry->info[side]=info;
  if(options->recursive && !g_file_info_get_is_symlink(info) && g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY) {
   if(depth>=options->max_depth) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Directory comparison depth limit reached.");ok=FALSE; }
   else { GFile *child=g_file_get_child(folder,name);ok=scan(child,relative,depth+1,side,options,map,rows,visited,cancel,error);g_object_unref(child); }
  }
  g_free(relative);
 }
 g_object_unref(reader);return ok;
}
static gint compare_rows(gconstpointer a,gconstpointer b) {
 return strcmp((*(InsightDirectoryEntry *const *)a)->relative,(*(InsightDirectoryEntry *const *)b)->relative);
}
static gboolean same_time(GFileInfo *a,GFileInfo *b) {
 return g_file_info_get_attribute_uint64(a,G_FILE_ATTRIBUTE_TIME_MODIFIED)==g_file_info_get_attribute_uint64(b,G_FILE_ATTRIBUTE_TIME_MODIFIED) &&
  g_file_info_get_attribute_uint32(a,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC)==g_file_info_get_attribute_uint32(b,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
}
static void classify(InsightDirectoryEntry *entry,const InsightDirectoryOptions *options) {
 GFileInfo *a=entry->info[0],*b=entry->info[1];
 if((a && (g_file_info_get_is_symlink(a) || (g_file_info_get_file_type(a)!=G_FILE_TYPE_REGULAR && g_file_info_get_file_type(a)!=G_FILE_TYPE_DIRECTORY))) ||
    (b && (g_file_info_get_is_symlink(b) || (g_file_info_get_file_type(b)!=G_FILE_TYPE_REGULAR && g_file_info_get_file_type(b)!=G_FILE_TYPE_DIRECTORY)))) { entry->status=INSIGHT_DIR_SKIPPED;entry->detail=g_strdup("Symbolic link or special file");return; }
 if(!a || !b) { entry->status=a ? INSIGHT_DIR_LEFT : INSIGHT_DIR_RIGHT;return; }
 if(g_file_info_get_file_type(a)!=g_file_info_get_file_type(b)) { entry->status=INSIGHT_DIR_TYPE;return; }
 if(g_file_info_get_file_type(a)==G_FILE_TYPE_DIRECTORY) { entry->status=INSIGHT_DIR_FOLDER;entry->detail=g_strdup("Folder; inspect child rows");return; }
 entry->status=same_time(a,b) && (options->method==INSIGHT_DIR_DATE || g_file_info_get_size(a)==g_file_info_get_size(b)) ? INSIGHT_DIR_SAME : INSIGHT_DIR_DIFFERENT;
}
static gboolean matches(GFileInfo *a,GFileInfo *b) {
 return b && (!g_file_info_has_attribute(b,G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK) || !g_file_info_get_is_symlink(b)) && g_file_info_get_file_type(b)==G_FILE_TYPE_REGULAR &&
  g_file_info_get_size(a)==g_file_info_get_size(b) && same_time(a,b) &&
  !g_strcmp0(g_file_info_get_attribute_string(a,G_FILE_ATTRIBUTE_ID_FILE),g_file_info_get_attribute_string(b,G_FILE_ATTRIBUTE_ID_FILE)) &&
  !g_strcmp0(g_file_info_get_etag(a),g_file_info_get_etag(b));
}
static gboolean recheck(GFile *file,GFileInfo *snapshot,GCancellable *cancel,GError **error) {
 GFileInfo *current=g_file_query_info(file,ATTRS,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,error);
 if(!current) return FALSE;
 gboolean ok=matches(snapshot,current);g_object_unref(current);
 if(!ok) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"File changed during directory comparison; rescan.");
 return ok;
}
static gboolean normalized_equal(const gchar *a,const gchar *b,guint flags) {
 while(TRUE) {
  if(flags&INSIGHT_DIFF_IGNORE_SPACE) { while(*a && strchr(" \t\v\f",*a)) a++;while(*b && strchr(" \t\v\f",*b)) b++; }
  gint x=(guchar)*a,y=(guchar)*b;
  if(flags&INSIGHT_DIFF_IGNORE_EOL) { if(x=='\r') x='\n';if(y=='\r') y='\n'; }
  if(x!=y) return FALSE;
  if(!x) return TRUE;
  if((flags&INSIGHT_DIFF_IGNORE_EOL) && *a=='\r' && a[1]=='\n') a++;
  if((flags&INSIGHT_DIFF_IGNORE_EOL) && *b=='\r' && b[1]=='\n') b++;
  a++;b++;
 }
}
static void content(InsightDirectoryEntry *entry,const gchar *left,const gchar *right,const InsightDirectoryOptions *options,guint64 *charged,GCancellable *cancel) {
 if(!entry->info[0] || !entry->info[1] || entry->status==INSIGHT_DIR_TYPE || entry->status==INSIGHT_DIR_SKIPPED ||
    g_file_info_get_file_type(entry->info[0])!=G_FILE_TYPE_REGULAR) return;
 goffset sizes[2]={g_file_info_get_size(entry->info[0]),g_file_info_get_size(entry->info[1])};
 if(!options->text_flags && sizes[0]!=sizes[1]) { entry->status=INSIGHT_DIR_DIFFERENT;return; }
 guint64 limit=options->text_flags ? 1024u*1024 : 64u*1024*1024;
 if(sizes[0]<0 || sizes[1]<0 || (guint64)sizes[0]>limit || (guint64)sizes[1]>limit || *charged>options->max_bytes ||
    (guint64)sizes[0]+(guint64)sizes[1]>options->max_bytes-*charged) {
  entry->status=INSIGHT_DIR_SKIPPED;entry->detail=g_strdup("Content size or scan byte limit reached");return;
 }
 *charged+=(guint64)sizes[0]+(guint64)sizes[1];
 GFile *files[2]={NULL,NULL};GFileInputStream *streams[2]={NULL,NULL};gchar *text[2]={NULL,NULL};GError *error=NULL;gboolean equal=TRUE;
 for(gint side=0;side<2;side++) {
  gchar *path=g_build_filename(side ? right : left,entry->relative,NULL);files[side]=g_file_new_for_path(path);g_free(path);
  if(!recheck(files[side],entry->info[side],cancel,&error)) goto done;
  streams[side]=g_file_read(files[side],cancel,&error);if(!streams[side]) goto done;
  GFileInfo *opened=g_file_input_stream_query_info(streams[side],ATTRS,cancel,&error);
  if(!opened) goto done;
  gboolean valid=matches(entry->info[side],opened);g_object_unref(opened);
  if(!valid) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_BUSY,"File changed during directory comparison; rescan.");goto done; }
 }
 if(options->text_flags) {
  for(gint side=0;side<2;side++) {
   gsize len=0;text[side]=g_malloc((gsize)sizes[side]+2);
   if(!g_input_stream_read_all(G_INPUT_STREAM(streams[side]),text[side],(gsize)sizes[side]+1,&len,cancel,&error)) goto done;
   if(len!=(gsize)sizes[side]) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_BUSY,"File changed during directory comparison; rescan.");goto done; }
   if(memchr(text[side],0,len) || !g_utf8_validate(text[side],len,NULL)) { entry->status=INSIGHT_DIR_SKIPPED;entry->detail=g_strdup("Text options require UTF-8 without NUL bytes");goto done; }
   text[side][len]=0;
  }
  equal=normalized_equal(text[0],text[1],options->text_flags);
 } else {
  gchar a[65536],b[65536];guint64 total=0;
  while(TRUE) {
   gsize na=0,nb=0,wanted=(gsize)MIN((guint64)sizeof a,(guint64)sizes[0]-total+1);
   if(!g_input_stream_read_all(G_INPUT_STREAM(streams[0]),a,wanted,&na,cancel,&error) ||
      !g_input_stream_read_all(G_INPUT_STREAM(streams[1]),b,wanted,&nb,cancel,&error)) goto done;
   if(total+na>(guint64)sizes[0] || total+nb>(guint64)sizes[1]) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_BUSY,"File changed during directory comparison; rescan.");goto done; }
   if(na!=nb || memcmp(a,b,na)) { equal=FALSE;break; }
   total+=na;if(!na) break;
  }
 }
 for(gint side=0;side<2;side++) if(!recheck(files[side],entry->info[side],cancel,&error)) goto done;
 entry->status=equal ? INSIGHT_DIR_SAME : INSIGHT_DIR_DIFFERENT;
done:
 if(error) { entry->status=INSIGHT_DIR_ERROR;entry->detail=g_strdup(error->message);g_clear_error(&error); }
 for(gint side=0;side<2;side++) { g_clear_object(&streams[side]);g_clear_object(&files[side]);g_free(text[side]); }
}
static GPtrArray *known_compile(const gchar *const *patterns,GError **error) {
 GPtrArray *specs=g_ptr_array_new_with_free_func((GDestroyNotify)g_pattern_spec_free);
 for(guint i=0;patterns && patterns[i];i++) {
  if(i>=8192 || strlen(patterns[i])>1024 || !g_utf8_validate(patterns[i],-1,NULL) || strpbrk(patterns[i],"\r\n")) {
   g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid directory comparison file type snapshot.");g_ptr_array_unref(specs);return NULL;
  }
  gchar *text=g_strdup(patterns[i]);
#ifdef G_OS_WIN32
  gchar *folded=g_utf8_casefold(text,-1);g_free(text);text=folded;
#endif
  if(*text) g_ptr_array_add(specs,g_pattern_spec_new(text));
  g_free(text);
 }
 return specs;
}
static gboolean known_entry(InsightDirectoryEntry *entry,GPtrArray *specs) {
 for(gint side=0;side<2;side++) if(entry->info[side] && !g_file_info_get_is_symlink(entry->info[side]) && g_file_info_get_file_type(entry->info[side])==G_FILE_TYPE_DIRECTORY) return TRUE;
 gchar *name=g_path_get_basename(entry->relative),*text=g_filename_to_utf8(name,-1,NULL,NULL,NULL);g_free(name);if(!text) return FALSE;
#ifdef G_OS_WIN32
 gchar *folded=g_utf8_casefold(text,-1);g_free(text);text=folded;
#endif
 gboolean found=FALSE;
 for(guint i=0;i<specs->len && !found;i++) {
#if GLIB_CHECK_VERSION(2,70,0)
  found=insight_pattern_match_string(g_ptr_array_index(specs,i),text);
#else
  found=g_pattern_match_string(g_ptr_array_index(specs,i),text);
#endif
 }
 g_free(text);return found;
}
GPtrArray *insight_directory_compare(const gchar *left,const gchar *right,const InsightDirectoryOptions *options,GCancellable *cancel,GError **error) {
 GPtrArray *rows=g_ptr_array_new_with_free_func(entry_free);GHashTable *map=g_hash_table_new(g_str_hash,g_str_equal);guint visited=0;gboolean ok=TRUE;GError *failure=NULL;
 GPtrArray *known=known_compile(options->known_patterns,&failure);if(!known) ok=FALSE;
 if(ok && (options->method<INSIGHT_DIR_CONTENT || options->method>INSIGHT_DIR_SIZE_DATE)) { g_set_error_literal(&failure,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"Unknown directory comparison method.");ok=FALSE; }
 for(gint side=0;side<2 && ok;side++) {
  GFile *root=g_file_new_for_path(side ? right : left);ok=scan(root,"",0,side,options,map,rows,&visited,cancel,&failure);g_object_unref(root);
 }
 g_hash_table_unref(map);
 if(ok) {
  if(options->known_patterns) for(guint i=rows->len;i>0;i--) {
   if(cancelled(cancel,&failure)) { ok=FALSE;break; }
   if(!known_entry(g_ptr_array_index(rows,i-1),known)) g_ptr_array_remove_index_fast(rows,i-1);
  }
  guint64 charged=0;g_ptr_array_sort(rows,compare_rows);
  for(guint i=0;ok && i<rows->len;i++) {
   if(cancelled(cancel,&failure)) { ok=FALSE;break; }
   InsightDirectoryEntry *entry=g_ptr_array_index(rows,i);
   classify(entry,options);
   if(options->method==INSIGHT_DIR_CONTENT) content(entry,left,right,options,&charged,cancel);
  }
 }
 if(known) g_ptr_array_unref(known);
 if(ok && cancelled(cancel,&failure)) ok=FALSE;
 if(!ok) { g_ptr_array_unref(rows);g_propagate_error(error,failure);return NULL; }return rows;
}
