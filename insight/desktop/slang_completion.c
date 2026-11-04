/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "slang_completion.h"
#include "index_cache.h"
#include <stdlib.h>
#include <glib/gstdio.h>
#include <string.h>

gboolean insight_slang_source(const gchar *path) {
 const gchar *dot=path ? strrchr(path,'.'):NULL;
 return dot && (!g_ascii_strcasecmp(dot,".v") || !g_ascii_strcasecmp(dot,".sv") || !g_ascii_strcasecmp(dot,".vh") || !g_ascii_strcasecmp(dot,".svh"));
}
static void candidate_free(gpointer data) {
 InsightClangCompletion *c=data;g_free(c->name);g_free(c->signature);g_free(c->type);g_free(c->comment);g_ptr_array_unref(c->parameters);g_free(c);
}
static gchar *unhex(const gchar *value) {
 gsize n=strlen(value);if(n>32768 || n%2) return NULL;gchar *result=g_malloc(n/2+1);
 for(gsize i=0;i<n;i+=2) {gint a=g_ascii_xdigit_value(value[i]),b=g_ascii_xdigit_value(value[i+1]);if(a<0 || b<0 || !(a|b)) {g_free(result);return NULL;}result[i/2]=(a<<4)|b;}
 result[n/2]=0;if(!g_utf8_validate(result,-1,NULL) || strpbrk(result,"\r\n\t")) {g_free(result);return NULL;}return result;
}
static gchar *disk_stamp(const gchar *path,guint64 *size) {
 GFile *file=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(file,"standard::size,etag::value",G_FILE_QUERY_INFO_NONE,NULL,NULL);g_object_unref(file);if(!info) return NULL;
 *size=g_file_info_get_size(info);gchar *etag=g_strdup(g_file_info_get_etag(info));g_object_unref(info);return etag;
}
typedef struct {gchar *key,*reply;gsize bytes;} CacheEntry;
static GMutex cache_mutex;
static GQueue cache_entries=G_QUEUE_INIT;
static InsightSlangCacheStats cache_stats;
static void cache_free(CacheEntry *entry) {g_free(entry->key);g_free(entry->reply);g_free(entry);}
void insight_slang_cache_clear(void) {
 g_mutex_lock(&cache_mutex);while(!g_queue_is_empty(&cache_entries)) cache_free(g_queue_pop_head(&cache_entries));cache_stats=(InsightSlangCacheStats){0};g_mutex_unlock(&cache_mutex);
}
void insight_slang_cache_stats(InsightSlangCacheStats *stats) {g_mutex_lock(&cache_mutex);*stats=cache_stats;g_mutex_unlock(&cache_mutex);}
static void key_field(GChecksum *sum,const gchar *text,gsize length) {
 gchar prefix[32];g_snprintf(prefix,sizeof prefix,"%"G_GSIZE_FORMAT":",length);g_checksum_update(sum,(const guchar *)prefix,strlen(prefix));g_checksum_update(sum,(const guchar *)text,length);
}
/* Cache only complete explicit source sets without preprocessor directives.
 * Includes / macro-built dependencies need a compiler dependency manifest;
 * conservatively bypass instead of reusing an incomplete dependency key. */
static gchar *request_key(const gchar *service,const gchar *const *arguments,GPtrArray *paths,GHashTable *buffers,GCancellable *cancel) {
 guint64 executable_size=0;gchar *stamp=disk_stamp(service,&executable_size);if(!stamp) return NULL;
 GChecksum *sum=g_checksum_new(G_CHECKSUM_SHA256);key_field(sum,service,strlen(service));key_field(sum,stamp,strlen(stamp));g_free(stamp);
 gchar *cwd=g_get_current_dir();key_field(sum,cwd,strlen(cwd));g_free(cwd);
 for(guint i=0;arguments[i];i++) key_field(sum,arguments[i],strlen(arguments[i]));
 gboolean valid=TRUE;gsize total=0;guint files=0;GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
 for(guint i=0;valid && i<paths->len;i++) {
  const gchar *file=g_ptr_array_index(paths,i);if(!insight_slang_source(file) || g_hash_table_contains(seen,file)) continue;g_hash_table_add(seen,(gpointer)file);
  if(++files>256 || (cancel && g_cancellable_is_cancelled(cancel))) {valid=FALSE;break;}
  const gchar *text=g_hash_table_lookup(buffers,file);gchar *disk=NULL;gsize length=0;
  if(text) length=strlen(text);
  else {guint64 size=0;gchar *etag=disk_stamp(file,&size);if(!etag || size>2u*1024u*1024u) {g_free(etag);valid=FALSE;break;}g_free(etag);if(!g_file_get_contents(file,&disk,&length,NULL)) {valid=FALSE;break;}text=disk;}
  total+=length;if(length>2u*1024u*1024u || total>32u*1024u*1024u || memchr(text,'`',length) || memchr(text,0,length) || !g_utf8_validate(text,length,NULL)) valid=FALSE;
  if(valid) {key_field(sum,file,strlen(file));key_field(sum,text,length);}g_free(disk);
 }
 g_hash_table_unref(seen);gchar *key=valid && files ? g_strdup(g_checksum_get_string(sum)):NULL;g_checksum_free(sum);return key;
}
static gchar *cache_get(const gchar *key) {
 gchar *reply=NULL;g_mutex_lock(&cache_mutex);
 if(key) for(GList *link=cache_entries.head;link;link=link->next) {CacheEntry *entry=link->data;if(!strcmp(key,entry->key)) {reply=g_strdup(entry->reply);g_queue_unlink(&cache_entries,link);g_queue_push_head_link(&cache_entries,link);cache_stats.hits++;break;}}
 else cache_stats.bypasses++;
 g_mutex_unlock(&cache_mutex);return reply;
}
static void cache_put(const gchar *key,const gchar *reply) {
 if(!key || !reply || strlen(reply)>4u*1024u*1024u-128) return;
 CacheEntry *entry=g_new0(CacheEntry,1);entry->key=g_strdup(key);entry->reply=g_strdup(reply);entry->bytes=strlen(key)+strlen(reply)+2;
 g_mutex_lock(&cache_mutex);
 for(GList *link=cache_entries.head;link;link=link->next) {CacheEntry *old=link->data;if(!strcmp(key,old->key)) {cache_stats.bytes-=old->bytes;cache_stats.entries--;g_queue_delete_link(&cache_entries,link);cache_free(old);break;}}
 while(cache_entries.length>=8 || cache_stats.bytes+entry->bytes>4u*1024u*1024u) {CacheEntry *old=g_queue_pop_tail(&cache_entries);cache_stats.bytes-=old->bytes;cache_stats.entries--;cache_free(old);}
 g_queue_push_head(&cache_entries,entry);cache_stats.bytes+=entry->bytes;cache_stats.entries++;g_mutex_unlock(&cache_mutex);
}
static void terminate(GCancellable *cancel,gpointer data) {(void)cancel;g_subprocess_force_exit(G_SUBPROCESS(data));}
static gchar *query(const gchar *service,const gchar *const *arguments,GPtrArray *paths,GHashTable *buffers,GCancellable *cancel) {
 if(!service || !*service || !paths || !buffers || (cancel && g_cancellable_is_cancelled(cancel))) return NULL;
 gchar *key=request_key(service,arguments,paths,buffers,cancel);if(cancel && g_cancellable_is_cancelled(cancel)) {g_free(key);return NULL;}gchar *cached=cache_get(key);
 if(cached) {g_free(key);if(cancel && g_cancellable_is_cancelled(cancel)) {g_free(cached);return NULL;}return cached;}
 gchar *directory=g_dir_make_tmp("geany-slang-XXXXXX",NULL);if(!directory) {g_free(key);return NULL;}
 GPtrArray *argv=g_ptr_array_new_with_free_func(g_free),*temporary=g_ptr_array_new_with_free_func(g_free);gchar *out=NULL;
 g_ptr_array_add(argv,g_strdup(service));for(guint i=0;arguments[i];i++) g_ptr_array_add(argv,g_strdup(arguments[i]));
 GHashTable *selected=g_hash_table_new(g_str_hash,g_str_equal),*stamps=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);gsize bytes=0,total_args=0;gboolean valid=TRUE;
 for(guint i=0;i<paths->len;i++) {
  const gchar *file=g_ptr_array_index(paths,i);if(!insight_slang_source(file) || g_hash_table_contains(selected,file)) continue;
  total_args+=strlen(file);if(g_hash_table_size(selected)>=256 || total_args>12000) {valid=FALSE;break;}g_hash_table_add(selected,(gpointer)file);
  const gchar *overlay=g_hash_table_lookup(buffers,file);
  if(!overlay) {guint64 size=0;gchar *stamp=disk_stamp(file,&size);if(!stamp || size>2u*1024u*1024u || bytes+size>32u*1024u*1024u) {g_free(stamp);valid=FALSE;break;}bytes+=size;g_hash_table_insert(stamps,g_strdup(file),stamp);}
  if(overlay) {gsize length=strlen(overlay);bytes+=length;if(length>2u*1024u*1024u || bytes>32u*1024u*1024u) {valid=FALSE;break;}
   gchar *target=g_strdup_printf("%s/%u.sv",directory,temporary->len);g_ptr_array_add(temporary,target);
   if(!g_file_set_contents(target,overlay,length,NULL)) {valid=FALSE;break;}
   g_ptr_array_add(argv,g_strdup("--overlay"));g_ptr_array_add(argv,g_strdup(file));g_ptr_array_add(argv,g_strdup(target));
  }
 }
 g_ptr_array_add(argv,g_strdup("--"));
 /* Preserve captured source order: do not iterate the membership hash. */
 for(guint i=0;i<paths->len;i++) {const gchar *file=g_ptr_array_index(paths,i);if(g_hash_table_remove(selected,file)) g_ptr_array_add(argv,g_strdup(file));}
 g_hash_table_unref(selected);g_ptr_array_add(argv,NULL);
 gsize command_bytes=0;for(guint i=0;i+1<argv->len;i++) command_bytes+=2*strlen(g_ptr_array_index(argv,i))+4;
 if(command_bytes>24000) valid=FALSE;
 if(valid) {
  GSubprocessLauncher *launcher=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_SILENCE);
#ifdef G_OS_WIN32
  /* Portable runtime DLLs live beside geany.exe, outside the helper directory. */
  gchar *prefix=g_path_get_dirname(service);for(guint i=0;i<3;i++) {gchar *parent=g_path_get_dirname(prefix);g_free(prefix);prefix=parent;}
  gchar *bin=g_build_filename(prefix,"bin",NULL),*search=g_strconcat(bin,G_SEARCHPATH_SEPARATOR_S,g_getenv("PATH") ? g_getenv("PATH"):"",NULL);
  g_subprocess_launcher_setenv(launcher,"PATH",search,TRUE);g_free(search);g_free(bin);g_free(prefix);
#endif
  g_mutex_lock(&cache_mutex);cache_stats.starts++;g_mutex_unlock(&cache_mutex);
  GSubprocess *process=g_subprocess_launcher_spawnv(launcher,(const gchar *const *)argv->pdata,NULL);g_object_unref(launcher);
  if(process) {
   gulong handler=cancel ? g_cancellable_connect(cancel,G_CALLBACK(terminate),process,NULL):0;
   gchar *reply=NULL;
   gboolean ok=g_subprocess_communicate_utf8(process,NULL,cancel,&reply,NULL,NULL);
   if(cancel && handler) g_cancellable_disconnect(cancel,handler);
   if(!ok) {g_subprocess_force_exit(process);g_subprocess_wait(process,NULL,NULL);}
   if(ok && g_subprocess_get_successful(process) && reply && strlen(reply)<=4u*1024u*1024u) out=g_steal_pointer(&reply);
   g_free(reply);g_object_unref(process);
  }
 }
 GHashTableIter iter;gpointer file,stamp;g_hash_table_iter_init(&iter,stamps);
 while(out && g_hash_table_iter_next(&iter,&file,&stamp)) {guint64 size=0;gchar *now=disk_stamp(file,&size);if(g_strcmp0(now,stamp)) g_clear_pointer(&out,g_free);g_free(now);}g_hash_table_unref(stamps);
 if(out && key && !(cancel && g_cancellable_is_cancelled(cancel))) {
  gchar *fresh=request_key(service,arguments,paths,buffers,cancel);
  if(!g_strcmp0(key,fresh)) cache_put(key,out);else g_clear_pointer(&out,g_free);g_free(fresh);
 }g_free(key);
 for(guint i=0;i<temporary->len;i++) g_unlink(g_ptr_array_index(temporary,i));g_rmdir(directory);g_free(directory);g_ptr_array_unref(temporary);g_ptr_array_unref(argv);return out;
}

GPtrArray *insight_slang_complete(const gchar *service,const gchar *path,const gchar *text,
 guint offset,guint rootlen,GPtrArray *paths,GHashTable *buffers,GCancellable *cancel) {
 if(!service || !*service || !path || !text || offset>strlen(text) || rootlen>offset || !paths || !buffers || (cancel && g_cancellable_is_cancelled(cancel))) return NULL;
 guint start=offset-rootlen,begin=start,line=1;
 for(guint i=0;i<offset;i++) if(text[i]=='\n') line++;
 /* Only plain identifier hierarchies. Indexed/dynamic receivers decline. */
 while(begin && (g_ascii_isalnum(text[begin-1]) || strchr("_$.:",text[begin-1]))) begin--;
 gchar *receiver=g_strndup(text+begin,start-begin),*prefix=g_strndup(text+start,rootlen);
 gsize size=strlen(receiver);if(size && receiver[size-1]=='.') receiver[--size]=0;
 else if(size>=2 && receiver[size-1]==':' && receiver[size-2]==':') receiver[size-2]=0;
 else if(size) {g_free(receiver);g_free(prefix);return NULL;}
 if(begin && (text[begin-1]==']' || text[begin-1]==')')) {g_free(receiver);g_free(prefix);return NULL;}
 gchar *line_text=g_strdup_printf("%u",line);const gchar *arguments[]={"--editor-complete",path,line_text,receiver,prefix,NULL};
 gchar *reply=query(service,arguments,paths,buffers,cancel);g_free(line_text);g_free(receiver);g_free(prefix);GPtrArray *out=NULL;
 if(reply && g_str_has_prefix(reply,"insight-slang-completion-v1\n")) {
  out=g_ptr_array_new_with_free_func(candidate_free);gchar **rows=g_strsplit(reply,"\n",-1);
  for(guint i=1;rows[i] && *rows[i] && out->len<4096;i++) {
   gchar **fields=g_strsplit(rows[i],"\t",3);gchar *name=fields[0] && fields[1] && !fields[2] ? unhex(fields[0]):NULL,*kind=name ? unhex(fields[1]):NULL;
   if(!name || !*name || !kind) {g_free(name);g_free(kind);g_strfreev(fields);g_ptr_array_unref(out);out=NULL;break;}
   InsightClangCompletion *c=g_new0(InsightClangCompletion,1);c->name=name;c->signature=g_strdup(name);c->type=kind;c->comment=g_strdup("");c->parameters=g_ptr_array_new_with_free_func(g_free);g_ptr_array_add(out,c);g_strfreev(fields);
  }g_strfreev(rows);
 }
 if(out && !out->len) {g_ptr_array_unref(out);out=NULL;}g_free(reply);return out;
}
static gboolean positive_position(const gchar *text,guint *value) {
 if(!text || !*text) return FALSE;for(const gchar *p=text;*p;p++) if(!g_ascii_isdigit(*p)) return FALSE;
 guint64 n=g_ascii_strtoull(text,NULL,10);if(!n || n>G_MAXINT) return FALSE;*value=(guint)n;return TRUE;
}
gboolean insight_slang_definition(const gchar *service,const gchar *path,const gchar *text,guint offset,
 GPtrArray *paths,GHashTable *buffers,GCancellable *cancel,gchar **target,guint *line,guint *column) {
 *target=NULL;*line=*column=0;if(!path || !text || offset>strlen(text)) return FALSE;
 if(offset && !(g_ascii_isalnum(text[offset]) || text[offset]=='_' || text[offset]=='$') && (g_ascii_isalnum(text[offset-1]) || text[offset-1]=='_' || text[offset-1]=='$')) offset--;
 guint l=1,c=1;for(guint i=0;i<offset;i++) {if(text[i]=='\n') {l++;c=1;}else c++;}
 gchar *ls=g_strdup_printf("%u",l),*cs=g_strdup_printf("%u",c);const gchar *arguments[]={"--editor-definition",path,ls,cs,NULL};
 gchar *reply=query(service,arguments,paths,buffers,cancel);g_free(ls);g_free(cs);gboolean ok=FALSE;
 if(reply && g_str_has_prefix(reply,"insight-slang-definition-v1\n")) {
  gchar **rows=g_strsplit(reply,"\n",-1);
  if(rows[1] && *rows[1] && rows[2] && !*rows[2] && !rows[3]) {
   gchar **fields=g_strsplit(rows[1],"\t",4);
   if(g_strv_length(fields)==3 && positive_position(fields[1],line) && positive_position(fields[2],column)) {
    *target=unhex(fields[0]);ok=*target && g_path_is_absolute(*target);if(!ok) g_clear_pointer(target,g_free);
   }g_strfreev(fields);
  }g_strfreev(rows);
 }g_free(reply);return ok;
}

void insight_slang_project_free(InsightSlangProject *project) {if(project) {insight_index_free(project->index);g_free(project);}}
InsightSlangProject *insight_slang_project(const gchar *service,GPtrArray *paths,GHashTable *buffers,const gchar *root,const gchar *cache,GCancellable *cancel) {
 if(!service || !*service || !root || !g_path_is_absolute(root) || !paths || !buffers || (cancel && g_cancellable_is_cancelled(cancel))) return NULL;
 const gchar *arguments[]={"--editor-index",NULL};gchar *key=request_key(service,arguments,paths,buffers,cancel);
 InsightSlangProject *project=g_new0(InsightSlangProject,1);
 if(cache && key) {
  char *stored=NULL;project->index=insight_cache_load(cache,&stored,NULL);const InsightSymbol *meta=NULL;
  gboolean valid=project->index && stored && !strcmp(root,stored) && insight_index_query(project->index,"@hdlmeta",TRUE,NULL,"@hdlmeta",root,0,&meta,1)==1 && !strcmp(meta->signature,key) && !strcmp(meta->type,"0") && !strcmp(meta->inheritance,"slang-project-v1");free(stored);
  if(valid) {project->restored=project->persisted=TRUE;g_free(key);return project;}insight_index_free(project->index);project->index=NULL;
 }
 gchar *reply=query(service,arguments,paths,buffers,cancel);if(!reply || !g_str_has_prefix(reply,"insight-slang-index-v1\n")) {g_free(reply);g_free(key);g_free(project);return NULL;}
 gchar **lines=g_strsplit(reply,"\n",-1);gboolean valid=lines[1] && *lines[1];
 if(valid) {for(const gchar *p=lines[1];*p;p++) if(!g_ascii_isdigit(*p)) valid=FALSE;guint64 count=g_ascii_strtoull(lines[1],NULL,10);if(count>G_MAXUINT) valid=FALSE;else project->diagnostics=count;}
 GHashTable *groups=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_array_unref);GPtrArray *owned=g_ptr_array_new_with_free_func(g_free);guint records=0;
 for(guint n=2;valid && lines[n] && *lines[n];n++) {
  gchar **fields=g_strsplit(lines[n],"\t",-1);guint line=0,column=0;
  if(g_strv_length(fields)!=8 || (strcmp(fields[0],"D") && strcmp(fields[0],"R")) || !positive_position(fields[3],&line) || !positive_position(fields[4],&column) || ++records>100000) {valid=FALSE;g_strfreev(fields);break;}
  gchar *name=unhex(fields[1]),*path=unhex(fields[2]),*scope=unhex(fields[5]),*type=unhex(fields[6]),*relation=unhex(fields[7]),*position=g_strdup(fields[4]);
  gchar *parts[]={name,path,scope,type,relation,position};for(guint i=0;i<G_N_ELEMENTS(parts);i++) if(parts[i]) g_ptr_array_add(owned,parts[i]);else valid=FALSE;
  if(valid && *name && g_path_is_absolute(path) && (!strcmp(fields[0],"D") || g_path_is_absolute(scope))) {
   GArray *rows=g_hash_table_lookup(groups,path);if(!rows) {rows=g_array_new(FALSE,FALSE,sizeof(InsightSymbol));g_hash_table_insert(groups,g_strdup(path),rows);}
   InsightSymbol row={name,path,scope,!strcmp(fields[0],"D") ? "@hdldef":"@hdlref",line,position,type,relation};g_array_append_val(rows,row);
  }else valid=FALSE;g_strfreev(fields);
 }
 if(valid) {
  project->index=insight_index_new();GHashTableIter iter;gpointer path,rows;g_hash_table_iter_init(&iter,groups);
  while(valid && g_hash_table_iter_next(&iter,&path,&rows)) valid=insight_index_update(project->index,path,(const InsightSymbol *)((GArray *)rows)->data,((GArray *)rows)->len);
  gchar *diagnostics=g_strdup_printf("%u",project->diagnostics);InsightSymbol meta={"@hdlmeta",root,"","@hdlmeta",0,key ? key:"",diagnostics,"slang-project-v1"};
  /* Root is a directory, distinct from source paths. */
  if(valid) valid=insight_index_update(project->index,root,&meta,1);g_free(diagnostics);
 }
 g_hash_table_unref(groups);g_ptr_array_unref(owned);g_strfreev(lines);g_free(reply);
 if(valid && key) {gchar *fresh=request_key(service,arguments,paths,buffers,cancel);valid=!g_strcmp0(key,fresh);g_free(fresh);}
 if(valid && cache && !(cancel && g_cancellable_is_cancelled(cancel))) {gchar *parent=g_path_get_dirname(cache);if(!g_mkdir_with_parents(parent,0700)) project->persisted=insight_cache_save_cancellable(cache,project->index,root,cancel,NULL);g_free(parent);}
 g_free(key);if(!valid || (cancel && g_cancellable_is_cancelled(cancel))) {insight_slang_project_free(project);return NULL;}return project;
}
