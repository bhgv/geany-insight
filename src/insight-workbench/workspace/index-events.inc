/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Index events and source updates.
 * RU: События индекса и обновление исходников. */

static void index_event_publish(GHashTable *changed,GHashTable *names) {
 if(!changed || !g_hash_table_size(changed)) return;
 /* The common single publication transfers references in constant time.
  * Only overlapping queued events need to merge their dependency sets. */
 GHashTableIter it; gpointer key;
 if(!wb.index_event_paths) wb.index_event_paths=g_hash_table_ref(changed);
 else { g_hash_table_iter_init(&it,changed); while(g_hash_table_iter_next(&it,&key,NULL)) g_hash_table_add(wb.index_event_paths,g_strdup(key)); }
 if(names) {
  if(!wb.index_event_names) wb.index_event_names=g_hash_table_ref(names);
  else { g_hash_table_iter_init(&it,names); while(g_hash_table_iter_next(&it,&key,NULL)) g_hash_table_add(wb.index_event_names,g_strdup(key)); }
 }
 if(!wb.index_event_idle) wb.index_event_idle=g_idle_add(index_event_dispatch,NULL);
}
static void snapshot_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; SnapshotJob *j=data; if(g_cancellable_is_cancelled(cancel)) { g_task_return_boolean(task,FALSE); return; }
 j->result=insight_index_clone(j->base); gboolean ok=j->valid && j->result!=NULL; gsize count=j->paths->len;
 gchar **paths=(gchar **)j->paths->pdata;
 for(gsize n=0;ok && n<count && !g_cancellable_is_cancelled(cancel);n++) {
  gsize size=insight_index_query(j->overlay,"",FALSE,NULL,NULL,paths[n],0,NULL,0); const InsightSymbol **tags=g_new(const InsightSymbol *,size); InsightSymbol *rows=g_new(InsightSymbol,size); insight_index_query(j->overlay,"",FALSE,NULL,NULL,paths[n],0,tags,size); for(gsize k=0;k<size;k++) rows[k]=*tags[k];
  gsize old=insight_index_query(j->result,"",FALSE,NULL,NULL,paths[n],0,NULL,0); ok=insight_index_count(j->result)-old+size<=250000 && insight_index_update(j->result,paths[n],rows,size); g_free(rows); g_free(tags);
 }
 if(ok && !g_cancellable_is_cancelled(cancel)) index_collect_changes(j);
 g_task_return_boolean(task,ok && !g_cancellable_is_cancelled(cancel));
}
static void snapshot_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); SnapshotJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.index_generation) return;
 g_clear_object(&wb.snapshot_worker);
 if(!ok) { ui_set_statusbar(TRUE,"Index snapshot could not be rebuilt; previous snapshot retained"); return; }
 /* No symbol changes: keep the existing version and borrowed GUI pointers. */
 if(!g_hash_table_size(j->changed)) return;
 /* Stop decoration before atomically replacing its owned name snapshot. */
 if(wb.preview_links_idle) { g_source_remove(wb.preview_links_idle); wb.preview_links_idle=0; }
 /* Candidate arrays borrow symbols from the current index. Drop them before
  * retirement, including the gap before the coalesced decoration refresh. */
 if(wb.preview_candidates) g_hash_table_remove_all(wb.preview_candidates);
 retire_names(wb.preview_names); wb.preview_names=j->all_names; j->all_names=NULL;
 retire_index(wb.index); wb.index=j->result; j->result=NULL; wb.index_version++; wb.preview_names_position=insight_index_count(wb.index);
 index_event_publish(j->changed,j->names);
}
static void refresh_index(void) {
 if(!wb.alive || wb.restore_worker || wb.scan_worker || wb.snapshot_worker || !wb.base || !wb.index_dirty) return;
 SnapshotJob *j=g_new0(SnapshotJob,1); j->base=insight_index_ref(wb.base); j->previous=insight_index_ref(wb.index); j->overlay=insight_index_new(); j->paths=g_ptr_array_new_with_free_func(g_free); j->valid=j->overlay!=NULL; j->epoch=workbench_epoch; j->generation=wb.index_generation;
 gchar *root=wb.workspace_from_project && wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;
 for(guint n=0;n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n); if(doc->is_valid && doc->file_name && (!root || insight_project_files_contains(wb.membership,root,doc->file_name))) { g_ptr_array_add(j->paths,g_strdup(doc->file_name)); if(!insight_native_import(j->overlay,doc->tm_file,doc->file_name)) j->valid=FALSE; } }
 g_free(root); wb.index_dirty=FALSE; wb.snapshot_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.snapshot_worker,snapshot_job_done,NULL); g_task_set_task_data(task,j,snapshot_job_free); g_task_run_in_thread(task,snapshot_job_run); g_object_unref(task);
}
static gboolean definition_language(const gchar *source,GeanyFiletype *from,const gchar *path) {
 return insight_languages_compatible(source,from,path);
}
typedef struct { const gchar *path;GeanyFiletype *type; } ScopeLanguage;
static int scope_language_eligible(const InsightSymbol *symbol,void *data) { ScopeLanguage *scope=data;return definition_language(scope->path,scope->type,symbol->path); }
static gboolean python_language(GeanyFiletype *type) { return type && type->lang==TM_PARSER_PYTHON; }
static gboolean member_calls_language(GeanyFiletype *type) { return type && (type->lang==TM_PARSER_C || type->lang==TM_PARSER_CPP || type->lang==TM_PARSER_CUDA); }
static gboolean definition_keyword(GeanyFiletype *ft,const gchar *word) {
 if(ft && (ft->lang==TM_PARSER_C || ft->lang==TM_PARSER_CPP || ft->lang==TM_PARSER_CUDA) && (!strcmp(word,"false") || !strcmp(word,"true") || !strcmp(word,"nullptr"))) return TRUE;
 return highlighting_is_primary_keyword(ft,word);
}
static gboolean definition_code(ScintillaObject *sci,gint position) {
 gint lexer=SSM(sci,SCI_GETLEXER,0,0),style=SSM(sci,SCI_GETSTYLEAT,position,0);
 return !highlighting_is_comment_style(lexer,style) && !highlighting_is_string_style(lexer,style);
}
static void preview_scopes_clear(void) {
 g_clear_pointer(&wb.preview_python_bindings,insight_native_python_bindings_free);g_clear_pointer(&wb.preview_python_scopes,g_hash_table_unref);g_clear_pointer(&wb.preview_scopes,insight_native_scopes_free);g_clear_pointer(&wb.preview_scopes_path,g_free);wb.preview_scopes_ready=FALSE;
}
static InsightNativeSource preview_python_source(void) {
 InsightNativeSource source={wb.preview_links_text,wb.preview_code_mask,wb.preview_links_length,NULL};
 if(!wb.preview_python_bindings || wb.preview_python_binding_version!=wb.index_version) {
  g_clear_pointer(&wb.preview_python_bindings,insight_native_python_bindings_free);
  wb.preview_python_bindings=insight_native_python_bindings_new(wb.index,wb.source_path,&source);wb.preview_python_binding_version=wb.index_version;
 }
 source.python=wb.preview_python_bindings;return source;
}
static gchar *preview_function_scope(gulong line,gint position,gboolean *uncertain) {
 *uncertain=FALSE;
 if(python_language(wb.preview_filetype)) {
  if(!wb.preview_python_scopes || wb.preview_python_version!=wb.index_version) { g_clear_pointer(&wb.preview_python_scopes,g_hash_table_unref);wb.preview_python_scopes=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free);wb.preview_python_version=wb.index_version; }
  gpointer cached=NULL;if(g_hash_table_lookup_extended(wb.preview_python_scopes,GUINT_TO_POINTER((guint)line),NULL,&cached)) return *(gchar *)cached ? g_strdup(cached) : NULL;
  InsightNativeSource source={wb.preview_links_text,wb.preview_code_mask,wb.preview_links_length,NULL};gchar *scope=insight_native_python_scope(wb.index,wb.source_path,&source,(gsize)position);g_hash_table_insert(wb.preview_python_scopes,GUINT_TO_POINTER((guint)line),g_strdup(scope ? scope : ""));return scope;
 }
 if(!member_calls_language(wb.preview_filetype)) return insight_native_function_scope(wb.index,wb.source_path,line);
 if(!wb.preview_scopes_ready || wb.preview_scopes_version!=wb.index_version || g_strcmp0(wb.preview_scopes_path,wb.source_path)) {
  preview_scopes_clear();InsightNativeSource source={wb.preview_links_text,wb.preview_code_mask,wb.preview_links_length,NULL};wb.preview_scopes=insight_native_scopes_new(wb.index,wb.source_path,&source);wb.preview_scopes_path=g_strdup(wb.source_path);wb.preview_scopes_version=wb.index_version;wb.preview_scopes_ready=TRUE;
 }
 return insight_native_scopes_at(wb.preview_scopes,line,(gsize)position,uncertain);
}
static gboolean preview_resolvable(const gchar *word,gint position) {
 if(insight_languages_keyword(wb.preview,word) || definition_keyword(wb.preview_filetype,word) || !definition_code(wb.preview,position)) return FALSE;
 GPtrArray *tags=g_hash_table_lookup(wb.preview_candidates,word);if(!tags && !python_language(wb.preview_filetype)) return FALSE;
 const gchar *lexical=member_calls_language(wb.preview_filetype) ? wb.preview_code_mask : NULL;
 if(lexical && position>=0 && (gsize)position<wb.preview_links_length && lexical[position]!=wb.preview_links_text[position]) return FALSE;
 gboolean scoped=FALSE,member=FALSE;gint at=position;while(at>0 && g_ascii_isspace(lexical ? lexical[at-1] : sci_get_char_at(wb.preview,at-1))) at--;
 gchar before=at>0 ? (lexical ? lexical[at-1] : sci_get_char_at(wb.preview,at-1)) : 0,previous=at>1 ? (lexical ? lexical[at-2] : sci_get_char_at(wb.preview,at-2)) : 0;
 gboolean qualified=before=='.' || (at>1 && ((previous=='-' && before=='>') || (previous==':' && before==':')));
 for(guint i=0;tags && i<tags->len;i++) { const InsightSymbol *tag=g_ptr_array_index(tags,i);if(!strcmp(tag->kind,"l") || !strcmp(tag->kind,"z")) scoped=TRUE; }
 gulong line=(gulong)SSM(wb.preview,SCI_LINEFROMPOSITION,position,0)+1;
 gboolean uncertain_scope=FALSE;gchar *scope=(scoped || qualified || python_language(wb.preview_filetype)) ? preview_function_scope(line,position,&uncertain_scope) : NULL,*owner=NULL;
 if(uncertain_scope) { g_free(scope);return FALSE; }
 if(python_language(wb.preview_filetype)) {
  InsightNativeSource text=preview_python_source();gboolean imported=FALSE;const InsightSymbol *hit;ScopeLanguage language={wb.source_path,wb.preview_filetype};
  gsize count=insight_native_python_import_query(wb.index,wb.source_path,scope,&text,position,word,scope_language_eligible,&language,&hit,1,&imported);
  if(imported || !tags) { g_free(scope);return count>0; }
 }
 if(qualified) { member=TRUE;if(wb.preview_links_text) { InsightNativeSource reference={wb.preview_links_text,wb.preview_code_mask,wb.preview_links_length,NULL};if(python_language(wb.preview_filetype)) reference=preview_python_source();ScopeLanguage language={wb.source_path,wb.preview_filetype};owner=python_language(wb.preview_filetype) ? insight_native_python_owner(wb.index,wb.source_path,scope,line,&reference,position,&member,scope_language_eligible,&language) : insight_native_member_owner_source_filtered(wb.index,wb.source_path,scope,line,&reference,position,&member,member_calls_language(wb.preview_filetype) && wb.preview_code_mask!=NULL,scope_language_eligible,&language); } }
 gboolean found=FALSE;
 if(!member && python_language(wb.preview_filetype)) {
  InsightNativeSource text=preview_python_source();gchar *binding=insight_native_python_binding_scope(wb.index,wb.source_path,scope,word,&text);g_free(scope);scope=binding;
  if(!scope) { g_free(owner);return FALSE; }
 }
 if(member) { ScopeLanguage language={wb.source_path,wb.preview_filetype};const InsightSymbol *hit;found=insight_member_query(wb.index,owner,word,wb.source_path,scope_language_eligible,&language,&hit,1,NULL,NULL)>0; }
 else for(guint i=0;i<tags->len;i++) { const InsightSymbol *tag=g_ptr_array_index(tags,i);if((python_language(wb.preview_filetype) ? insight_native_python_rank(wb.index,tag,wb.source_path,scope,line) : insight_native_definition_rank_at(tag,wb.source_path,scope,line))>=0) { found=TRUE;break; } }
 g_free(scope);g_free(owner);return found;
}
static gboolean preview_links_step(gpointer data) {
 (void)data; gint64 deadline=g_get_monotonic_time()+6000;
 gsize count=insight_index_count(wb.index);
 while(wb.preview_names_position<count) {
  const InsightSymbol *symbol=insight_index_at(wb.index,wb.preview_names_position++);
  if(symbol && definition_language(wb.source_path,wb.preview_filetype,symbol->path) && !definition_keyword(wb.preview_filetype,symbol->name)) {
   GPtrArray *tags=g_hash_table_lookup(wb.preview_candidates,symbol->name);
   if(!tags) { tags=g_ptr_array_new();g_hash_table_insert(wb.preview_candidates,g_strdup(symbol->name),tags); }
   g_ptr_array_add(tags,(gpointer)symbol);
  }
  if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 SSM(wb.preview,SCI_SETINDICATORCURRENT,INSIGHT_LINK,0);
 gint length=(gint)wb.preview_links_length;
 while(wb.preview_links_position<length) {
  gint position=wb.preview_links_position;if(position>=wb.preview_styled_position) { wb.preview_styled_position=MIN(length,position+16384);SSM(wb.preview,SCI_COLOURISE,position,wb.preview_styled_position); }gint end=(gint)SSM(wb.preview,SCI_WORDENDPOSITION,position,TRUE);
  if(end<=position) wb.preview_links_position=(gint)SSM(wb.preview,SCI_POSITIONAFTER,position,0);
  else {
   gint start=(gint)SSM(wb.preview,SCI_WORDSTARTPOSITION,position,TRUE);
   if(start>=0 && end<=length && end-start<=1024) { gchar *name=g_strndup(wb.preview_links_text+start,(gsize)(end-start)); if(preview_resolvable(name,start)) { SSM(wb.preview,SCI_SETINDICATORCURRENT,INSIGHT_LINK,0);SSM(wb.preview,SCI_INDICATORFILLRANGE,start,end-start); } g_free(name); }
   wb.preview_links_position=end;
  }
  if(wb.preview_links_position<=position) wb.preview_links_position=position+1;
  if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 wb.preview_links_idle=0; return G_SOURCE_REMOVE;
}
static void preview_links(void) {
 if(!wb.preview) return;
 refresh_index();
 gchar *text=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1);
 if(wb.preview_links_valid && wb.preview_links_text && !strcmp(text,wb.preview_links_text) && !g_strcmp0(wb.source_path,wb.preview_links_path) && wb.preview_filetype==wb.preview_links_filetype && wb.index_version==wb.preview_links_version) { g_free(text);return; }
 if(wb.preview_links_idle) { g_source_remove(wb.preview_links_idle); wb.preview_links_idle=0; }
 g_free(wb.preview_links_path);wb.preview_links_path=g_strdup(wb.source_path);wb.preview_links_filetype=wb.preview_filetype;wb.preview_links_version=wb.index_version;wb.preview_links_valid=TRUE;
 g_clear_pointer(&wb.preview_candidates,g_hash_table_destroy);wb.preview_candidates=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,(GDestroyNotify)g_ptr_array_unref);wb.preview_names_position=0;
 SSM(wb.preview,SCI_INDICSETSTYLE,INSIGHT_LINK,INDIC_PLAIN); SSM(wb.preview,SCI_INDICSETFORE,INSIGHT_LINK,0xcc5500);
 SSM(wb.preview,SCI_SETINDICATORCURRENT,INSIGHT_LINK,0); SSM(wb.preview,SCI_INDICATORCLEARRANGE,0,sci_get_length(wb.preview));
 preview_scopes_clear();gboolean calls=member_calls_language(wb.preview_filetype),python=python_language(wb.preview_filetype);
 if(!wb.preview_links_text || strcmp(wb.preview_links_text,text) || calls!=wb.preview_calls || python!=wb.preview_python) { g_clear_pointer(&wb.preview_code_mask,g_free);if(calls || python) wb.preview_code_mask=insight_source_code_mask(text,strlen(text),python ? INSIGHT_CODE_PYTHON : INSIGHT_CODE_C,NULL); }
 g_free(wb.preview_links_text);wb.preview_links_text=text;wb.preview_links_length=strlen(text);wb.preview_calls=calls;wb.preview_python=python; wb.preview_links_position=0;wb.preview_styled_position=0;
 wb.preview_links_idle=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,preview_links_step,NULL,NULL);
}
static gboolean preview_motion(GtkWidget *widget,GdkEventMotion *event,gpointer data) {
 (void)widget; (void)data; gint position=(gint)SSM(wb.preview,SCI_POSITIONFROMPOINTCLOSE,(uptr_t)event->x,(sptr_t)event->y);
 /* Embedded Scintilla Platform::Window::Cursor::hand is 8. */
 SSM(wb.preview,SCI_SETCURSOR,position>=0 && SSM(wb.preview,SCI_INDICATORVALUEAT,INSIGHT_LINK,position) ? 8 : SC_CURSORNORMAL,0); return FALSE;
}
static void preview_notify(ScintillaObject *sci,gint message,SCNotification *notification,gpointer data) {
 (void)message; (void)data;
 if(notification->nmhdr.code==SCN_UPDATEUI || notification->nmhdr.code==SCN_PAINTED) decorate_selection(sci);
}
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
