/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Directory watches and project scanning.
 * RU: Наблюдение за каталогами и сканирование проекта. */

static void watch_directory(const char *path) {
 gchar *stamp=insight_project_db_stamp(path,TRUE);if(stamp) g_hash_table_replace(wb.directory_stamps,g_strdup(path),stamp);
 GError *error=NULL;
 if(!insight_workspace_watch_add(wb.watch,path,&error)) wb.watch_incomplete=TRUE;
 g_clear_error(&error);
}
typedef struct { InsightIndex *index;InsightProjectDb db; gchar *path,*root,*identity,*policy; guint64 epoch,generation; } CacheJob;
static GMutex cache_write_mutex;
static void cache_job_free(gpointer data) { CacheJob *j=data; retire_index(j->index);insight_project_db_clear(&j->db); g_free(j->path); g_free(j->root);g_free(j->identity);g_free(j->policy); g_free(j); }
static void cache_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; CacheJob *j=data; GError *error=NULL;
 /* A cancelled older writer cannot replace a newer project's cache. */
 g_mutex_lock(&cache_write_mutex);
 gboolean ok=!g_cancellable_set_error_if_cancelled(cancel,&error);
 if(ok) {
  gchar *directory=g_path_get_dirname(j->path);
  if(g_mkdir_with_parents(directory,0700)) { ok=FALSE; g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_FAILED,"Unable to create index cache directory"); }
  g_free(directory);
 }
 if(ok) ok=insight_project_db_save(j->path,j->identity,j->root,j->policy,&j->db,cancel,&error);
 g_mutex_unlock(&cache_write_mutex);
 if(error) g_task_return_error(task,error); else g_task_return_boolean(task,ok);
}
static void cache_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); CacheJob *j=g_task_get_task_data(task); GError *error=NULL; gboolean ok=g_task_propagate_boolean(task,&error);
 if(wb.alive && j->epoch==workbench_epoch && j->generation==wb.scan_generation) {
  if(wb.cache_worker==g_task_get_cancellable(task)) g_clear_object(&wb.cache_worker);
  if(ok) g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-saved",GINT_TO_POINTER(1));
  if(!ok && error && !g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) ui_set_statusbar(TRUE,"Workspace cache not saved: %s",error->message);
 }
 g_clear_error(&error);
}
static void save_index_cache(void) {
 if(!wb.alive || wb.restore_worker || !wb.base || !wb.root || !wb.cache_path) return;
 if(wb.cache_worker) { g_cancellable_cancel(wb.cache_worker); g_clear_object(&wb.cache_worker); }
 CacheJob *j=g_new0(CacheJob,1); j->index=NULL;j->db.symbols=insight_index_ref(wb.base);j->db.calls=insight_index_ref(wb.disk_calls);j->db.declarations=insight_index_ref(wb.disk_declarations);j->db.files=insight_project_db_copy_table(wb.file_stamps);j->db.directories=insight_project_db_copy_table(wb.directory_stamps);j->identity=g_strdup(wb.db_identity);j->policy=g_strdup(wb.db_policy); j->path=g_strdup(wb.cache_path); j->root=g_strdup(wb.root); j->epoch=workbench_epoch; j->generation=wb.scan_generation;
 wb.cache_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.cache_worker,cache_job_done,NULL); g_task_set_task_data(task,j,cache_job_free); g_task_run_in_thread(task,cache_job_run); g_object_unref(task);
}
static char *read_disk_source_full(const char *,gboolean,gint,GCancellable *);
static gboolean scan_step(gpointer);
typedef struct { InsightIndex *index,*calls,*outline; OutlineFile *declarations;gchar *stamp,*path,*utf8,*language,*compile_root,*clang_resources; gint lang,encoding; guint64 generation,epoch; gboolean read,parsed,failed,skipped,regular; } IndexJob;
static void index_job_free(gpointer data) { IndexJob *j=data;outline_free(j->declarations); retire_index(j->index);retire_index(j->calls);retire_index(j->outline);g_free(j->stamp); g_free(j->path); g_free(j->utf8); g_free(j->language);g_free(j->compile_root);g_free(j->clang_resources); g_free(j); }
static void index_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; IndexJob *j=data; GStatBuf st;
 if(g_cancellable_is_cancelled(cancel)) { g_task_return_boolean(task,FALSE); return; }
 if(!insight_index_exclusive(j->index)) { InsightIndex *copy=insight_index_clone(j->index); if(!copy) { j->failed=TRUE; g_task_return_boolean(task,TRUE); return; } insight_index_free(j->index); j->index=copy; }
 if(!insight_index_exclusive(j->calls)) { InsightIndex *copy=insight_index_clone(j->calls);insight_index_free(j->calls);j->calls=copy; }
 if(!insight_index_exclusive(j->outline)) { InsightIndex *copy=insight_index_clone(j->outline);insight_index_free(j->outline);j->outline=copy; }
 if(!j->calls||!j->outline) { j->failed=TRUE;g_task_return_boolean(task,TRUE);return; }
 insight_index_update(j->index,j->utf8,NULL,0);insight_index_update(j->calls,j->utf8,NULL,0);insight_index_update(j->outline,j->utf8,NULL,0);
 j->regular=!g_lstat(j->path,&st) && S_ISREG(st.st_mode);
 if(!j->regular || st.st_size>2*1024*1024) j->skipped=TRUE;
 else if(j->read) {
  gchar *text=read_disk_source_full(j->utf8,TRUE,j->encoding,cancel);
  if(!text) j->failed=TRUE;
  else {
   j->parsed=TRUE;
   if(j->lang>=0 && !g_cancellable_is_cancelled(cancel)) {
    TMSourceFile *file=tm_source_file_new(j->path,j->language);
    if(file) { file->lang=j->lang; if(!insight_native_parse(j->index,file,j->utf8,text,strlen(text))) j->failed=TRUE;else { j->declarations=outline_capture(file,j->utf8);if(!insight_native_outline_import(j->outline,file,j->utf8)) j->failed=TRUE; } tm_source_file_free(file); }
    else j->failed=TRUE;
   }
   gboolean semantic=FALSE;
   if(!j->failed && insight_clang_available() && insight_clang_source(j->utf8) && !g_cancellable_is_cancelled(cancel)) {
    InsightClangResult *parsed=insight_clang_parse(j->utf8,text,j->compile_root,j->clang_resources,NULL,cancel,NULL);
    if(insight_clang_result_usable(parsed)) {semantic=insight_clang_import(parsed,j->utf8,j->index,j->calls);if(semantic) {TMSourceFile *tags=insight_native_clang_tags(parsed,j->path,j->language,j->lang);if(tags) {outline_free(j->declarations);j->declarations=outline_capture(tags,j->utf8);if(!insight_native_outline_import(j->outline,tags,j->utf8)) j->failed=TRUE;for(guint n=0;n<tags->tags_array->len;n++) ((TMTag *)g_ptr_array_index(tags->tags_array,n))->file=NULL;tm_source_file_free(tags);}}}
    insight_clang_result_free(parsed);
   }
   InsightCodeDialect dialect;
   GeanyFiletype *ft=filetypes_lookup_by_name(j->language?j->language:"");
   if(!j->failed&&!semantic&&source_dialect(ft,&dialect)) {
    gchar *mask=insight_source_code_mask(text,strlen(text),dialect,NULL),*digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);GArray *rows=g_array_new(FALSE,FALSE,sizeof(InsightSymbol));GPtrArray *owned=g_ptr_array_new_with_free_func(g_free);size_t cursor=0,a=0,b=0,line=1,line_at=0;
    while(mask&&rows->len<32000&&insight_next_call(mask,strlen(text),&cursor,&a,&b)==1) {
     while(line_at<a) { if(text[line_at]=='\n') line++;else if(text[line_at]=='\r'&&(line_at+1>=a||text[line_at+1]!='\n')) line++;line_at++; }
     size_t start=a;while(start&&text[start-1]!='\n'&&text[start-1]!='\r') start--;
     gchar *name=g_strndup(text+a,b-a),*positions=g_strdup_printf("%zu:%zu:%lu",a,b,(gulong)g_utf8_strlen(text+start,a-start)+1),*snippet=reference_snippet(text,a);g_ptr_array_add(owned,name);g_ptr_array_add(owned,positions);g_ptr_array_add(owned,snippet);InsightSymbol row={name,j->utf8,"","@call",line,positions,snippet,digest};g_array_append_val(rows,row);
    }
    InsightSymbol coverage={"@file",j->utf8,"","@file",1,"","",digest};g_array_append_val(rows,coverage);
    if(!mask||rows->len>=32001||!insight_index_update(j->calls,j->utf8,(InsightSymbol *)rows->data,rows->len)) j->failed=TRUE;
    g_ptr_array_unref(owned);g_array_unref(rows);g_free(mask);g_free(digest);
   }
   gchar *after=source_stamp(j->path,j->language,j->encoding);if(g_strcmp0(after,j->stamp)) j->failed=TRUE;g_free(after);g_free(text);
  }
 }
 g_task_return_boolean(task,TRUE);
}
static void index_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); IndexJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.scan_generation) return;
 g_clear_object(&wb.scan_worker);
 if(j->failed||!ok) { retire_index(wb.base);wb.base=wb.scan_backup?insight_index_ref(wb.scan_backup):insight_index_new(); }
 else { retire_index(wb.base);wb.base=j->index;j->index=NULL;retire_index(wb.disk_calls);wb.disk_calls=j->calls;j->calls=NULL;retire_index(wb.disk_declarations);wb.disk_declarations=j->outline;j->outline=NULL;wb.index_dirty=TRUE; }
 retire_index(wb.scan_backup);wb.scan_backup=NULL;
 if(!j->failed&&j->stamp) g_hash_table_replace(wb.file_stamps,g_strdup(j->utf8),g_strdup(j->stamp));
 if(j->declarations && !j->failed) { g_hash_table_replace(wb.declarations,g_strdup(j->utf8),j->declarations);j->declarations=NULL; }
 if(!j->parsed&&!j->failed) g_hash_table_remove(wb.declarations,j->utf8);
 if(j->parsed) wb.parsed++;

 if(j->failed || j->skipped || !ok) wb.skipped++;
 if(j->failed) wb.scan_failed=TRUE;
 if((wb.parsed+wb.reused)%25==0) { if(!wb.scan_keep_files) files_filter(NULL,NULL); ui_set_statusbar(TRUE,"Workspace: indexing… %u files",wb.parsed); }
 if(g_get_monotonic_time()>=wb.publish_due) { wb.publish_due=g_get_monotonic_time()+1000000; refresh_index(); }
 wb.scan=g_idle_add(scan_step,NULL);
}
static gboolean scan_continue(void) { if(wb.scan_worker) { wb.scan=0; return G_SOURCE_REMOVE; } return G_SOURCE_CONTINUE; }
static void scan_source_file(const char *path) {
 if(!wb.scan_indexing) {
  if(g_hash_table_contains(wb.scan_seen,path)) return;
  g_hash_table_add(wb.scan_seen,g_strdup(path));
 }
 gchar *utf8=utils_get_utf8_from_locale(path),*root=utils_get_utf8_from_locale(wb.root);
 if(wb.workspace_from_project && !insight_project_files_contains(wb.membership,root,utf8)) { g_free(root); g_free(utf8); return; }
 g_free(root);
 if(!wb.scan_indexing) {
  GStatBuf st;
  if(!g_lstat(path,&st) && S_ISREG(st.st_mode)) {
   g_ptr_array_add(wb.scan_members,g_strdup(utf8));
   if(!wb.scan_keep_files) {
    g_ptr_array_add(wb.files,g_strdup(utf8));
    if(wb.files->len==1 || wb.files->len%100==0) files_filter(NULL,NULL);
   }
  }
  g_free(utf8); return;
 }
 GeanyFiletype *ft=filetypes_detect_from_extension(utf8);
 /* A cached shebang language avoids reopening extensionless source on every
  * validation. Detect content only for a genuinely new/changed file. */
 const gchar *stored=g_hash_table_lookup(wb.file_stamps,utf8);
 if(ft&&ft->id==GEANY_FILETYPES_NONE&&stored) { const gchar *colon=strchr(stored,':');if(colon) { gchar *language=g_strndup(stored,colon-stored);GeanyFiletype *cached=filetypes_lookup_by_name(language);if(cached) ft=cached;g_free(language); } }
 gchar *stamp=source_stamp(path,ft?ft->name:NULL,file_prefs.default_open_encoding);
 if(!wb.force_parse&&stamp&&!g_strcmp0(stamp,g_hash_table_lookup(wb.file_stamps,utf8))&&(!insight_clang_available()||!insight_clang_source(utf8)||insight_clang_dependencies_fresh(wb.disk_calls,utf8))) { wb.reused++;g_free(stamp);g_free(utf8);return; }
 ft=filetypes_detect_from_file(utf8);g_free(stamp);stamp=source_stamp(path,ft?ft->name:NULL,file_prefs.default_open_encoding);
 IndexJob *j=g_new0(IndexJob,1);j->compile_root=wb.root ? utils_get_utf8_from_locale(wb.root):NULL;j->clang_resources=g_build_filename(app->datadir,"insight","clang","resources",NULL);wb.scan_backup=insight_index_ref(wb.base); j->index=wb.base; wb.base=NULL;j->calls=insight_index_ref(wb.disk_calls);j->outline=insight_index_ref(wb.disk_declarations);j->stamp=stamp; j->path=g_strdup(path); j->utf8=utf8; j->language=g_strdup(ft ? ft->name : NULL); j->lang=ft ? ft->lang : -1; j->read=ft && ft->id!=GEANY_FILETYPES_NONE; j->encoding=file_prefs.default_open_encoding; j->generation=wb.scan_generation; j->epoch=workbench_epoch;
 wb.scan_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.scan_worker,index_job_done,NULL); g_task_set_task_data(task,j,index_job_free); g_task_run_in_thread(task,index_job_run); g_object_unref(task);
}
static gboolean scan_step(gpointer data) {
 (void)data;
 if((!wb.frames->len && wb.scan_file_position>=wb.scan_files->len && wb.scan_tree_position>=wb.scan_trees->len)) {
  /* Enumeration must never wait for Clang. Publish membership before parsing. */
  if(!wb.scan_indexing) {
   wb.scan_indexing=TRUE; wb.scan_index_position=0;
   g_ptr_array_set_size(wb.files,0);
   for(guint n=0;n<wb.scan_members->len;n++) g_ptr_array_add(wb.files,g_strdup(g_ptr_array_index(wb.scan_members,n)));
   files_filter(NULL,NULL); tree_refresh();
   hdl_invalidate_colors();
   g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-members-ready",GUINT_TO_POINTER(wb.files->len+1));
  }
  if(wb.scan_index_position<wb.scan_members->len) {
   gchar *native=utils_get_locale_from_utf8(g_ptr_array_index(wb.scan_members,wb.scan_index_position++));
   scan_source_file(native);g_free(native);return scan_continue();
  }
  gboolean limited=FALSE;
  wb.scan=0; wb.relation_dirty=TRUE;wb.report_scan_completed=!wb.scan_failed;
  /* Project membership is separate from the symbol index. Publish a complete
   * replacement only after enumeration; do not narrow the visible list while
   * a same-root rescan is still in progress. */
  if(!wb.scan_failed) {
   gboolean detached=FALSE;
   GHashTable *live=g_hash_table_new(g_str_hash,g_str_equal);for(guint n=0;n<wb.scan_members->len;n++) g_hash_table_add(live,g_ptr_array_index(wb.scan_members,n));
   GHashTableIter it;gpointer path;g_hash_table_iter_init(&it,wb.file_stamps);while(g_hash_table_iter_next(&it,&path,NULL)) if(!g_hash_table_contains(live,path)) {
    if(!detached) { InsightIndex *copy=insight_index_clone(wb.base);retire_index(wb.base);wb.base=copy;copy=insight_index_clone(wb.disk_calls);retire_index(wb.disk_calls);wb.disk_calls=copy;copy=insight_index_clone(wb.disk_declarations);retire_index(wb.disk_declarations);wb.disk_declarations=copy;detached=TRUE; }
    insight_index_update(wb.base,path,NULL,0);insight_index_update(wb.disk_calls,path,NULL,0);insight_index_update(wb.disk_declarations,path,NULL,0);g_hash_table_remove(wb.declarations,path);g_hash_table_iter_remove(&it); }g_hash_table_unref(live);
  }
  wb.force_parse=FALSE;
  if(!wb.scan_failed) { GPtrArray *old=wb.files; wb.files=wb.scan_members; wb.scan_members=old; g_ptr_array_set_size(wb.scan_members,0); }
  g_ptr_array_set_size(wb.frames,0); g_ptr_array_set_size(wb.scan_files,0); g_ptr_array_set_size(wb.scan_trees,0); g_hash_table_remove_all(wb.scan_seen); refresh_index();
  g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-parsed",GUINT_TO_POINTER(wb.parsed+1));g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-reused",GUINT_TO_POINTER(wb.reused+1));g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-members",GUINT_TO_POINTER(wb.files->len+1));g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-scanning",NULL);
  if(!limited && !wb.scan_failed) save_index_cache();
  ui_set_statusbar(TRUE,"Workspace: %u parsed, %u reused, %zu symbols, %u skipped%s",wb.parsed,wb.reused,insight_index_count(wb.index),wb.skipped,limited ? "; file limit reached" : (wb.watch_incomplete ? "; directory monitoring incomplete" : ""));
  browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); file_types_refresh(); categories_filter(NULL,NULL); local_filter(NULL,NULL); insight_workbench_refresh();
  if(wb.watch_changed && !wb.watch_idle) { wb.watch_changed=FALSE; wb.watch_idle=g_timeout_add(2000,rescan_changed,NULL); }
  return G_SOURCE_REMOVE;
 }
 if(wb.scan_file_position<wb.scan_files->len) { scan_source_file(g_ptr_array_index(wb.scan_files,wb.scan_file_position++)); return scan_continue(); }
 if(!wb.frames->len) {
  const char *path=g_ptr_array_index(wb.scan_trees,wb.scan_tree_position++);
  if(!g_hash_table_contains(wb.scan_seen,path)) { GDir *dir=g_dir_open(path,0,NULL); if(dir) { ScanFrame *extra=g_new0(ScanFrame,1); extra->path=g_strdup(path); extra->dir=dir; g_hash_table_add(wb.scan_seen,g_strdup(path)); g_ptr_array_add(wb.frames,extra); watch_directory(path); } else { wb.scan_failed=TRUE; wb.skipped++; } }
  return G_SOURCE_CONTINUE;
 }
 ScanFrame *frame=g_ptr_array_index(wb.frames,wb.frames->len-1); const char *name=g_dir_read_name(frame->dir);
 if(!name) { g_ptr_array_remove_index(wb.frames,wb.frames->len-1); return G_SOURCE_CONTINUE; }
 if(name[0]=='.' || !strcmp(name,"node_modules") || !strcmp(name,"__pycache__")) return G_SOURCE_CONTINUE;
 gchar *path=g_build_filename(frame->path,name,NULL); GStatBuf st;
 if(g_lstat(path,&st)) { wb.scan_failed=TRUE; wb.skipped++; g_free(path); return G_SOURCE_CONTINUE; }
 if(S_ISDIR(st.st_mode)) {
  gchar *profile=insight_canonicalize_filename(app->configdir,NULL); gboolean own_profile=!g_strcmp0(path,profile); g_free(profile);
  if(own_profile || g_hash_table_contains(wb.scan_seen,path)) { g_free(path); return G_SOURCE_CONTINUE; }
  GDir *dir=wb.frames->len<64 ? g_dir_open(path,0,NULL) : NULL;
  if(dir) { g_hash_table_add(wb.scan_seen,g_strdup(path)); watch_directory(path); ScanFrame *next=g_new0(ScanFrame,1); next->path=path; next->dir=dir; g_ptr_array_add(wb.frames,next); }
  else { wb.scan_failed=TRUE; wb.skipped++; g_free(path); }
  return G_SOURCE_CONTINUE;
 }
 scan_source_file(path); g_free(path); return scan_continue();
}
static guint hdl_color_timer,hdl_snapshot_idle,hdl_paint_idle,hdl_paint_cursor,hdl_paint_first,hdl_paint_last,hdl_paint_doc;
static guint64 hdl_color_revision;
static GCancellable *hdl_color_worker;

static void hdl_clear_document_colors(ScintillaObject *sci)
{
 if(!g_object_get_data(G_OBJECT(sci),"insight-hdl-painted")) return;
 g_object_set_data(G_OBJECT(sci),"insight-hdl-painted",NULL);
 g_object_set_data(G_OBJECT(sci),"insight-hdl-spans",NULL);
 SSM(sci,SCI_SETINDICATORCURRENT,INSIGHT_HDL_COLOR,0);
 SSM(sci,SCI_INDICATORCLEARRANGE,0,sci_get_length(sci));
}

static void hdl_cancel_colors(void)
{
 hdl_color_revision++;
 if(hdl_color_timer) {g_source_remove(hdl_color_timer);hdl_color_timer=0;}
 if(hdl_snapshot_idle) {g_source_remove(hdl_snapshot_idle);hdl_snapshot_idle=0;}
 if(hdl_paint_idle) {g_source_remove(hdl_paint_idle);hdl_paint_idle=0;}
 if(hdl_color_worker) g_cancellable_cancel(hdl_color_worker);
}

static void hdl_invalidate_colors(void)
{
 hdl_cancel_colors();
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);
  if(DOC_VALID(doc)) hdl_clear_document_colors(doc->editor->sci);
 }
 hdl_schedule_colors();
}
static gboolean start_scan(const char *root) {
 gchar *requested_root=g_strdup(root);hdl_invalidate_colors();stop_scan();root=requested_root;
 if(wb.workspace_from_project && insight_git_active() && !wb.git_manual) {
  stop_scan();GError *error=NULL;InsightProjectFiles *policy=insight_git_membership(&error);
  if(!policy) { g_free(requested_root);ui_set_statusbar(TRUE,"Cannot read Git project files: %s",error ? error->message : "Git is busy");g_clear_error(&error);return FALSE; }
  insight_project_files_free(wb.membership);wb.membership=policy;
 }
 gchar *owned=insight_canonicalize_filename(root,NULL);g_free(requested_root); GDir *dir=g_dir_open(owned,0,NULL); InsightIndex *next=NULL;
 gboolean context_changed=database_identity(owned)||g_strcmp0(wb.root,owned)!=0;
 if(context_changed) { g_hash_table_remove_all(wb.file_stamps);g_hash_table_remove_all(wb.directory_stamps);g_hash_table_remove_all(wb.declarations);retire_index(wb.base);wb.base=insight_index_new();retire_index(wb.disk_calls);wb.disk_calls=insight_index_new();retire_index(wb.disk_declarations);wb.disk_declarations=insight_index_new(); }
 if(dir && !wb.force_parse&&(context_changed||!g_hash_table_size(wb.file_stamps)) && g_strcmp0(wb.restore_attempt_identity,wb.db_identity)) {
  if(dir) g_dir_close(dir);
  if(wb.watch_idle) {g_source_remove(wb.watch_idle);wb.watch_idle=0;}
  wb.watch_changed=FALSE;wb.report_scan_completed=FALSE;
  if(context_changed) g_ptr_array_set_size(wb.files,0);
  insight_workspace_watch_clear(wb.watch);
  g_free(wb.root);wb.root=g_strdup(owned);
  restore_project_database(owned);g_free(owned);return TRUE;
 }
 next=wb.base?insight_index_ref(wb.base):insight_index_new();
 if(!dir || !next) { if(dir) g_dir_close(dir); insight_index_free(next); g_free(owned); return FALSE; }
 gboolean same_root=!g_strcmp0(wb.root,owned)&&(!context_changed||g_hash_table_size(wb.file_stamps)>0)&&(wb.files->len||wb.report_scan_completed);
 invalidate_search_context(); stop_scan(); wb.scan_keep_files=same_root; g_ptr_array_set_size(wb.scan_members,0);
 if(wb.watch_idle) { g_source_remove(wb.watch_idle); wb.watch_idle=0; }
 insight_workspace_watch_clear(wb.watch); wb.watch_changed=FALSE; wb.watch_incomplete=FALSE; if(!wb.cache_validation) watch_directory(owned);
 retire_index(wb.base); wb.base=next; g_free(wb.root); wb.root=g_strdup(owned);
 if(!same_root) g_ptr_array_set_size(wb.files,0);
 ScanFrame *frame=g_new0(ScanFrame,1); frame->path=owned; frame->dir=dir;
 gboolean known_only=wb.cache_validation;wb.cache_validation=FALSE;
 if(known_only) { GHashTableIter it;gpointer path,value;g_hash_table_iter_init(&it,wb.directory_stamps);while(g_hash_table_iter_next(&it,&path,&value)) { gchar *now=insight_project_db_stamp(path,TRUE);if(g_strcmp0(now,value)) known_only=FALSE;g_free(now); } }
 if(!known_only) { g_hash_table_remove_all(wb.directory_stamps);watch_directory(owned); }
 if(known_only) {
  GHashTableIter it;gpointer path;g_hash_table_iter_init(&it,wb.file_stamps);while(g_hash_table_iter_next(&it,&path,NULL)) { gchar *native=utils_get_locale_from_utf8(path);if(g_file_test(native,G_FILE_TEST_IS_REGULAR)) g_ptr_array_add(wb.scan_files,native);else g_free(native); }
  g_hash_table_iter_init(&it,wb.directory_stamps);while(g_hash_table_iter_next(&it,&path,NULL)) { GError *error=NULL;insight_workspace_watch_add(wb.watch,path,&error);g_clear_error(&error); }
  frame_free(frame);
 }else if(!wb.workspace_from_project || wb.membership->automatic) { g_ptr_array_add(wb.frames,frame); g_hash_table_add(wb.scan_seen,g_strdup(owned)); } else frame_free(frame);
 if(wb.workspace_from_project&&!known_only) {
  for(guint i=0;i<wb.membership->files->len;i++) { const char *path=g_ptr_array_index(wb.membership->files,i); gchar *native=utils_get_locale_from_utf8(path),*parent=g_path_get_dirname(native); g_ptr_array_add(wb.scan_files,native); watch_directory(parent); g_free(parent); }
  for(guint i=0;i<wb.membership->trees->len;i++) g_ptr_array_add(wb.scan_trees,utils_get_locale_from_utf8(g_ptr_array_index(wb.membership->trees,i)));
 }
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-saved",NULL);g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-scanning",GINT_TO_POINTER(1));
 wb.scan_indexing=FALSE;wb.scan_index_position=0;g_object_set_data(G_OBJECT(main_widgets.window),"insight-db-members-ready",NULL);
 wb.scan_file_position=wb.scan_tree_position=0; wb.parsed=wb.skipped=wb.reused=0;wb.report_scan_completed=FALSE; mark_index_dirty(); wb.scan_failed=FALSE; wb.scan=g_idle_add(scan_step,NULL); ui_set_statusbar(TRUE,wb.force_parse?"Rebuilding project symbols…":known_only?"Project database restored; checking file stamps…":"Synchronizing project membership and changed files…"); return TRUE;
}
