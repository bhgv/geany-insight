/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "directory_compare.h"
#include <string.h>
#define ATTRS "standard::name,standard::type,standard::is-symlink,standard::is-hidden,standard::size,time::modified,time::modified-usec,id::file,etag::value"
static void entry_free(gpointer data) {
 InsightDirectoryEntry *e=data;g_free(e->relative);g_clear_object(&e->info[0]);g_clear_object(&e->info[1]);g_free(e->detail);g_free(e);
}
InsightDirectoryOptions insight_directory_options(void) {
 InsightDirectoryOptions o={INSIGHT_DIR_CONTENT,TRUE,FALSE,0,20000,32,256u*1024*1024};return o;
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
 if(g_file_info_get_file_type(a)==G_FILE_TYPE_DIRECTORY) { entry->status=INSIGHT_DIR_SAME;entry->detail=g_strdup("Folder; inspect child rows");return; }
 entry->status=same_time(a,b) && (options->method==INSIGHT_DIR_DATE || g_file_info_get_size(a)==g_file_info_get_size(b)) ? INSIGHT_DIR_SAME : INSIGHT_DIR_DIFFERENT;
}
GPtrArray *insight_directory_compare(const gchar *left,const gchar *right,const InsightDirectoryOptions *options,GCancellable *cancel,GError **error) {
 GPtrArray *rows=g_ptr_array_new_with_free_func(entry_free);GHashTable *map=g_hash_table_new(g_str_hash,g_str_equal);guint visited=0;gboolean ok=TRUE;GError *failure=NULL;
 if(options->method<INSIGHT_DIR_DATE || options->method>INSIGHT_DIR_SIZE_DATE) { g_set_error_literal(&failure,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"Unknown directory comparison method.");ok=FALSE; }
 for(gint side=0;side<2 && ok;side++) {
  GFile *root=g_file_new_for_path(side ? right : left);ok=scan(root,"",0,side,options,map,rows,&visited,cancel,&failure);g_object_unref(root);
 }
 g_hash_table_unref(map);
 if(ok) { g_ptr_array_sort(rows,compare_rows);for(guint i=0;i<rows->len;i++) classify(g_ptr_array_index(rows,i),options); }
 if(ok && cancelled(cancel,&failure)) ok=FALSE;
 if(!ok) { g_ptr_array_unref(rows);g_propagate_error(error,failure);return NULL; }return rows;
}
