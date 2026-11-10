/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Index events and source updates.
 * RU: События индекса и обновление исходников. */

#include "index-publication.inc"

#include "../source/preview-links.inc"

static void stop_scan(void) {
 if(wb.scan || wb.scan_worker || wb.restore_worker) wb.report_scan_completed=FALSE;
 stop_definition(); stop_file_preview();
 wb.scan_generation++;if(wb.restore_worker) {g_cancellable_cancel(wb.restore_worker);g_clear_object(&wb.restore_worker);if(wb.alive) {g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-loading",NULL);g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-scanning",NULL);}}
 if(wb.cache_worker) { g_cancellable_cancel(wb.cache_worker); g_clear_object(&wb.cache_worker); } if(wb.scan_worker) { g_cancellable_cancel(wb.scan_worker); g_clear_object(&wb.scan_worker); if(wb.alive && !wb.base) wb.base=wb.scan_backup?insight_index_ref(wb.scan_backup):insight_index_new(); }
 retire_index(wb.scan_backup);wb.scan_backup=NULL;
 if(wb.scan) { g_source_remove(wb.scan); wb.scan=0; }
 if(wb.frames) g_ptr_array_set_size(wb.frames,0);
 if(wb.scan_files) g_ptr_array_set_size(wb.scan_files,0);
 if(wb.scan_trees) g_ptr_array_set_size(wb.scan_trees,0);
 if(wb.scan_seen) g_hash_table_remove_all(wb.scan_seen);
}
static gboolean source_dialect(GeanyFiletype *,InsightCodeDialect *);
static gchar *reference_snippet(const char *,gsize);
static gboolean rescan_changed(gpointer data) {
 (void)data; wb.watch_idle=0; workspace_changed(NULL); return G_SOURCE_REMOVE;
}
static void workspace_changed(gpointer data) {
 (void)data;
 if(!wb.root) return;
 if(wb.scan || wb.scan_worker || wb.restore_worker) { wb.watch_changed=TRUE; return; }
 GPtrArray *paths=insight_workspace_watch_take_changes(wb.watch);gboolean membership=FALSE;
 for(guint n=0;n<paths->len;n++) { const gchar *native=g_ptr_array_index(paths,n);gchar *utf8=utils_get_utf8_from_locale(native);if(!g_hash_table_contains(wb.file_stamps,utf8)||!g_file_test(native,G_FILE_TEST_IS_REGULAR)) membership=TRUE;g_free(utf8); }
 g_ptr_array_unref(paths);if(!membership&&g_hash_table_size(wb.file_stamps)) wb.cache_validation=TRUE;
 if(!start_scan(wb.root)) ui_set_statusbar(TRUE,"Workspace changed but source folder is unavailable; previous index retained");
}
static gchar *project_policy(const gchar *native_root) {
 GKeyFile *key=g_key_file_new();gchar *root=native_root?utils_get_utf8_from_locale(native_root):NULL;
 if(wb.workspace_from_project&&wb.membership&&root) insight_project_files_write(wb.membership,key,root,NULL);
 gchar *data=g_key_file_to_data(key,NULL,NULL),*digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,data,-1);g_free(data);g_free(root);g_key_file_unref(key);return digest;
}
static gboolean database_identity(const gchar *root) {
 gchar *identity=wb.workspace_from_project&&app->project?g_strdup(app->project->file_name):g_strconcat("workspace:",root,NULL);
 gchar *policy=project_policy(root);gboolean changed=g_strcmp0(identity,wb.db_identity)!=0;
 if(g_strcmp0(identity,wb.db_identity)||g_strcmp0(policy,wb.db_policy)) {
  g_free(wb.cache_path);wb.cache_path=insight_project_db_path(app->configdir,identity);g_free(wb.db_identity);wb.db_identity=g_strdup(identity);g_free(wb.db_policy);wb.db_policy=g_strdup(policy);
 }
 g_free(identity);g_free(policy);return changed;
}
#ifndef INSIGHT_CLANG_PROVIDER
#define INSIGHT_CLANG_PROVIDER "disabled"
#endif
static gchar *source_stamp(const gchar *native,const gchar *language,gint encoding) {
 gchar *stamp=insight_project_db_stamp(native,FALSE);if(!stamp) return NULL;gchar *value=g_strdup_printf("%s:%d:%s:clang%d-cpp%d-provider%s-v15",language?language:"",encoding,stamp,insight_clang_available(),insight_clang_cpp_available(),INSIGHT_CLANG_PROVIDER);g_free(stamp);return value;
}
static gboolean start_scan(const char *);
typedef struct { InsightProjectDb db;GHashTable *declarations;GPtrArray *members;gchar *path,*root,*identity;guint64 generation,epoch; } RestoreJob;
static void restore_job_free(gpointer data) {
 RestoreJob *j=data;retire_index(j->db.symbols);retire_index(j->db.calls);retire_index(j->db.declarations);
 if(j->db.files) retire_names(j->db.files);if(j->db.directories) retire_names(j->db.directories);
 if(j->declarations) retire_names(j->declarations);if(j->members) g_ptr_array_unref(j->members);
 g_free(j->path);g_free(j->root);g_free(j->identity);g_free(j->db.policy);g_free(j);
}
static void restore_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;RestoreJob *j=data;GError *error=NULL;
 if(g_cancellable_is_cancelled(cancel) || !insight_project_db_load(j->path,j->identity,j->root,NULL,&j->db,&error)) {
  g_clear_error(&error);g_task_return_boolean(task,FALSE);return;
 }
 j->declarations=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,outline_free);
 j->members=g_ptr_array_new_with_free_func(g_free);GHashTableIter iter;gpointer path;
 g_hash_table_iter_init(&iter,j->db.files);while(g_hash_table_iter_next(&iter,&path,NULL)) g_ptr_array_add(j->members,g_strdup(path));
 for(gsize n=0;n<insight_index_count(j->db.declarations);n++) {
  if(g_cancellable_is_cancelled(cancel)) {g_task_return_boolean(task,FALSE);return;}
  const InsightSymbol *row=insight_index_at(j->db.declarations,n);
  if(!g_str_has_prefix(row->kind,"@decl:")) continue;
  gchar *end=NULL;guint type=g_ascii_strtoull(row->kind+6,&end,10);if(!end||*end!=':'||!end[1]) continue;
  OutlineFile *outline=g_hash_table_lookup(j->declarations,row->path);
  if(!outline) {outline=outline_capture(NULL,row->path);g_hash_table_insert(j->declarations,g_strdup(row->path),outline);}
  TMTag *tag=tm_tag_new();tag->name=g_strdup(row->name);tag->type=type;tag->kind_letter=end[1];tag->line=row->line;
  tag->scope=g_strdup(row->scope);tag->arglist=g_strdup(row->signature);tag->var_type=g_strdup(row->type);tag->inheritance=g_strdup(row->inheritance);g_ptr_array_add(outline->tags,tag);
 }
 g_task_return_boolean(task,TRUE);
}
static void restore_job_done(GObject *source,GAsyncResult *result,gpointer unused) {
 (void)source;(void)unused;GTask *task=G_TASK(result);RestoreJob *j=g_task_get_task_data(task);
 gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.scan_generation) return;
 g_clear_object(&wb.restore_worker);g_free(wb.restore_attempt_identity);wb.restore_attempt_identity=g_strdup(j->identity);
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-loading",NULL);
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-loaded-after-map",GINT_TO_POINTER(gtk_widget_get_mapped(main_widgets.window)));
 if(ok) {
  retire_index(wb.base);wb.base=j->db.symbols;j->db.symbols=NULL;
  retire_index(wb.disk_calls);wb.disk_calls=j->db.calls;j->db.calls=NULL;
  retire_index(wb.disk_declarations);wb.disk_declarations=j->db.declarations;j->db.declarations=NULL;
  retire_names(wb.file_stamps);wb.file_stamps=j->db.files;j->db.files=NULL;
  retire_names(wb.directory_stamps);wb.directory_stamps=j->db.directories;j->db.directories=NULL;
  retire_names(wb.declarations);wb.declarations=j->declarations;j->declarations=NULL;
  /* Restored @decl rows already encode their native tag type and kind.
   * No GUI-thread filetype scan is required to import these outlines. */
  g_ptr_array_unref(wb.files);wb.files=j->members;j->members=NULL;
  g_free(wb.root);wb.root=g_strdup(j->root);
  wb.cache_validation=!g_strcmp0(j->db.policy,wb.db_policy);
 }
 start_scan(j->root);browser_filter(NULL,NULL);files_filter(NULL,NULL);tree_refresh();
}
static void restore_project_database(const gchar *root) {
 RestoreJob *j=g_new0(RestoreJob,1);j->path=g_strdup(wb.cache_path);j->root=g_strdup(root);j->identity=g_strdup(wb.db_identity);j->generation=wb.scan_generation;j->epoch=workbench_epoch;
 wb.restore_worker=g_cancellable_new();g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-loading",GINT_TO_POINTER(1));g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-scanning",GINT_TO_POINTER(1));
 GTask *task=g_task_new(NULL,wb.restore_worker,restore_job_done,NULL);g_task_set_task_data(task,j,restore_job_free);g_task_run_in_thread(task,restore_job_run);g_object_unref(task);
}
