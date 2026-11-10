/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Native extension commands and workspace events.
 * RU: Команды расширений и события рабочей области. */

static const InsightEditorAPI native_host_api={.position=native_get_position,.goto_line=native_go_line,.message=native_message,.insert=native_insert,.replace_range=native_replace_range,.line_text=native_line_text,.line_count=native_line_count,.set_selection=native_set_selection,.selection_range=native_selection_range,.filename=native_filename,.save_file=native_save_file,.open_file=native_open_file,.open_waveforms=native_waveforms};
static void execute_native(const char *id,const char *source,gsize length) {
 if(wb.lua_running || insight_session_is_running(insight_extensions_session(wb.extensions))) { ui_set_statusbar(TRUE,"Lua command already running"); return; }
 GeanyDocument *doc=document_get_current(); if(!doc) return;
 refresh_index(); InsightIndex *snapshot=insight_index_clone(wb.index);
 if(!snapshot) { ui_set_statusbar(TRUE,"Lua: unable to snapshot the project index"); return; }
 NativeHost h={.doc=doc,.undo_doc=doc};
 InsightHost host={.context=&h,.text=native_get_text,.selection=native_get_selection,.replace_selection=native_replace_selection,.index=snapshot,.api=&native_host_api};
 char error[512]; wb.lua_running=TRUE; sci_start_undo_action(doc->editor->sci);
 int result=id ? insight_session_invoke(insight_extensions_session(wb.extensions),id,&host,error,sizeof error) : insight_run(&host,source,length,error,sizeof error);
 if(h.undo_doc && h.undo_doc->is_valid) sci_end_undo_action(h.undo_doc->editor->sci);
 wb.lua_running=FALSE; g_free(h.scratch); insight_index_free(snapshot);
 if(result || !h.message_set) ui_set_statusbar(TRUE,"Lua: %s",result ? (error[0] ? error : "runtime unavailable") : "completed");
 insight_workbench_refresh();
}
static void choose_native_script(GtkMenuItem *item,gpointer data) {
 (void)item;
 if(wb.lua_running) return;
 GtkWidget *dialog=gtk_file_chooser_dialog_new(data ? "Run Lua script" : "Load Lua extension",GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 gchar *path=NULL; if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
 gtk_widget_destroy(dialog); if(!path) return;
 GError *error=NULL;
 if(data) {
  char *source; gsize length;
  if(insight_read_lua_file(path,&source,&length,&error)) { execute_native(NULL,source,length); g_free(source); }
 } else if(insight_extensions_select(wb.extensions,path,&error)) native_commands_rebuild();
 if(error) { ui_set_statusbar(TRUE,"Lua: %s",error->message); g_error_free(error); }
 g_free(path);
}
static void reload_native_script(GtkMenuItem *item,gpointer data) {
 (void)item; GError *error=NULL;
 gboolean ok=data ? insight_extensions_unload(wb.extensions,&error) : insight_extensions_restore(wb.extensions,&error);
 if(ok) native_commands_rebuild();
 else { ui_set_statusbar(TRUE,"Lua: %s",error ? error->message : "unable to load extension"); g_clear_error(&error); }
}
static void invoke_native_command(GtkMenuItem *item,gpointer data) {
 (void)data; execute_native(g_object_get_data(G_OBJECT(item),"native-command-id"),NULL,0);
}
typedef struct { GtkListStore *store; GtkWidget *view; } NativePalette;
static void native_palette_filter(GtkEditable *entry,gpointer data) {
 NativePalette *palette=data; gtk_list_store_clear(palette->store);
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(entry));
 InsightSession *session=insight_extensions_session(wb.extensions);
 for(size_t i=0;i<insight_session_command_count(session);i++) {
  const char *id=insight_session_command_name(session,i),*label=insight_session_command_label(session,i);
  if(!g_utf8_validate(label,-1,NULL)) label=id;
  gchar *text=g_strconcat(label," ",id,NULL);
  if(insight_lookup_text_match(text,query)) { GtkTreeIter iter; gtk_list_store_append(palette->store,&iter); gtk_list_store_set(palette->store,&iter,0,label,1,id,-1); }
  g_free(text);
 }
 GtkTreeIter first;
 if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(palette->store),&first)) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(palette->view)),&first);
}
static void palette_activate(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)view; (void)path; (void)column; gtk_dialog_response(GTK_DIALOG(data),GTK_RESPONSE_ACCEPT);
}
static void show_native_palette(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data; if(wb.lua_running) return;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Lua command palette",GTK_WINDOW(main_widgets.window),GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Run",GTK_RESPONSE_ACCEPT,NULL);
 gtk_window_set_default_size(GTK_WINDOW(dialog),650,400); gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);
 GtkWidget *entry=gtk_search_entry_new(),*box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gtk_widget_set_name(entry,"insight-command-query");gtk_widget_set_tooltip_text(entry,"Prefix with ~ for fuzzy subsequence matching"); gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE);
 GtkListStore *store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING); GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
 for(int i=0;i<2;i++) gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes(i ? "ID" : "Command",gtk_cell_renderer_text_new(),"text",i,NULL));
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),view);
 gtk_box_pack_start(GTK_BOX(box),entry,FALSE,FALSE,4); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);
 NativePalette palette={store,view}; g_signal_connect(entry,"changed",G_CALLBACK(native_palette_filter),&palette); g_signal_connect(view,"row-activated",G_CALLBACK(palette_activate),dialog);
 native_palette_filter(GTK_EDITABLE(entry),&palette); gtk_widget_show_all(dialog); gchar *id=NULL;
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { GtkTreeIter iter; if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&iter)) gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,1,&id,-1); }
 gtk_widget_destroy(dialog); g_object_unref(store); if(id) execute_native(id,NULL,0); g_free(id);
}
static void native_commands_rebuild(void) {
 if(!wb.lua_commands) return;
 GList *children=gtk_container_get_children(GTK_CONTAINER(wb.lua_commands)); for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data); g_list_free(children);
 if(wb.lua_accels) { guint old_key; GdkModifierType old_mods; gtk_accelerator_parse("<Primary><Shift>p",&old_key,&old_mods); gtk_widget_remove_accelerator(wb.palette_item,wb.lua_accels,old_key,old_mods); gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.lua_accels); g_object_unref(wb.lua_accels); }
 wb.lua_accels=gtk_accel_group_new(); gtk_window_add_accel_group(GTK_WINDOW(main_widgets.window),wb.lua_accels);
 guint palette_key; GdkModifierType palette_mods; gtk_accelerator_parse("<Primary><Shift>p",&palette_key,&palette_mods);
 gtk_widget_add_accelerator(wb.palette_item,"activate",wb.lua_accels,palette_key,palette_mods,GTK_ACCEL_VISIBLE);
 guint keys[128]={0}; GdkModifierType modifiers[128]={0}; InsightSession *session=insight_extensions_session(wb.extensions);
 for(size_t i=0;i<insight_session_command_count(session);i++) {
  const char *id=insight_session_command_name(session,i),*label=insight_session_command_label(session,i),*shortcut=insight_session_command_shortcut(session,i);
  GtkWidget *item=gtk_menu_item_new_with_label(g_utf8_validate(label,-1,NULL) ? label : id);g_object_set_data(G_OBJECT(item),"insight-no-i18n",GINT_TO_POINTER(1));
  g_object_set_data_full(G_OBJECT(item),"native-command-id",g_strdup(id),g_free); gtk_container_add(GTK_CONTAINER(wb.lua_commands),item); g_signal_connect(item,"activate",G_CALLBACK(invoke_native_command),NULL);
  guint key; GdkModifierType mods; gtk_accelerator_parse(shortcut,&key,&mods); gboolean duplicate=key==palette_key && mods==palette_mods;
  for(size_t j=0;j<i;j++) if(keys[j]==key && modifiers[j]==mods) duplicate=TRUE;
  if(key && !duplicate && gtk_accelerator_valid(key,mods)) { gtk_widget_add_accelerator(item,"activate",wb.lua_accels,key,mods,GTK_ACCEL_VISIBLE); keys[i]=key; modifiers[i]=mods; }
 }
 gtk_widget_show_all(wb.lua_commands);
}
static gboolean file_event_refresh(gpointer data) { (void)data;wb.file_event_idle=0;if(wb.alive) { files_filter(NULL,NULL); tree_refresh(); file_types_refresh(); categories_filter(NULL,NULL);disk_current_file(); }return G_SOURCE_REMOVE; }
static void native_document_event(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object;
 if(DOC_VALID(doc) && insight_slang_source(doc->file_name)) {
  if(!strcmp(data,"document_activate")) {hdl_queue_paint(doc->editor->sci);hdl_schedule_colors();}
  else hdl_invalidate_colors();
 }
 if(!strcmp(data,"document_close")) insight_search_links_closed(wb.search_links,doc);
 if(!strcmp(data,"document_close") && doc && doc->is_valid && doc->file_name && wb.notes) for(guint i=0;i<notes_for(INSIGHT_BOOKMARK)->items->len;i++) { const InsightNote *note=g_ptr_array_index(notes_for(INSIGHT_BOOKMARK)->items,i);if(note->kind==INSIGHT_BOOKMARK && !g_strcmp0(note->path,doc->file_name)) { bookmark_prepare_tags(doc);break; } }
 insight_window_list_document_event(doc ? doc->id : 0,data); if(!strcmp(data,"document_close") || !strcmp(data,"document_save")) insight_workbench_bookmarks_changed();
 if(!strcmp(data,"document_close")) bookmark_file_closed(doc);
 if(!strcmp(data,"document_open") || !strcmp(data,"document_activate")) bookmark_restore_document(doc);
 if(wb.alive&&!strcmp(data,"document_activate")) files_filter(NULL,NULL);
 if(wb.alive && strcmp(data,"document_activate") && !wb.file_event_idle) wb.file_event_idle=g_idle_add(file_event_refresh,NULL);
 if(!strcmp(data,"document_save") && doc && doc->file_name && wb.membership && !g_strcmp0(doc->file_name,wb.membership->master_file)) insight_master_sync_poll(wb.master_sync);
 if(strcmp(data,"document_activate")) mark_index_dirty();
 insight_workbench_refresh();
 /* Disk snapshots are worker-owned/immutable; saving schedules a coalesced rescan. */
 if(!strcmp(data,"document_save") && doc && doc->is_valid && doc->file_name && wb.root && g_ptr_array_find_with_equal_func(wb.files,doc->file_name,(GEqualFunc)g_str_equal,NULL)) {
  wb.watch_changed=TRUE;
  if(!wb.scan && !wb.scan_worker && !wb.watch_idle) wb.watch_idle=g_timeout_add(2000,rescan_changed,NULL);
 }
 if(!wb.extensions || wb.lua_running || !doc || !doc->is_valid || insight_session_is_running(insight_extensions_session(wb.extensions))) return;

 refresh_index(); InsightIndex *snapshot=insight_index_clone(wb.index); if(!snapshot) return;
 NativeHost h={.doc=doc}; InsightHost host={.context=&h,.text=native_get_text,.selection=native_get_selection,.replace_selection=native_replace_selection,.index=snapshot,.api=&native_host_api};
 char error[512]; wb.lua_running=TRUE;
 int result=insight_session_emit(insight_extensions_session(wb.extensions),data,doc->file_name,&host,error,sizeof error);
 wb.lua_running=FALSE; g_free(h.scratch); insight_index_free(snapshot); if(result) ui_set_statusbar(TRUE,"Lua event: %s",error);
 if(!strcmp(data,"document_close")) mark_index_dirty();
}
static void native_project_update(GObject *object,GKeyFile *config,gpointer data) {
 (void)object; if(!wb.preview || !app->project) return;
 gchar *utf8=project_get_base_path(); if(!utf8) return;
 gchar *native=utils_get_locale_from_utf8(utf8),*root=insight_canonicalize_filename(native,NULL); g_free(native);
 gboolean changed=GPOINTER_TO_INT(data) || g_strcmp0(root,wb.geany_root) || g_strcmp0(wb.membership_project,app->project->file_name);
 if(changed) {
  bookmarks_project_load(app->project->file_name,utf8,FALSE);clips_project_load(app->project->file_name,FALSE);
  invalidate_search_context();
  GKeyFile *loaded=NULL; GError *error=NULL;
  if(!config) { loaded=g_key_file_new(); gchar *filename=utils_get_locale_from_utf8(app->project->file_name); if(!g_key_file_load_from_file(loaded,filename,G_KEY_FILE_NONE,&error) && g_error_matches(error,G_FILE_ERROR,G_FILE_ERROR_NOENT)) g_clear_error(&error); g_free(filename); config=loaded; }
  insight_git_bind(config,root);
  wb.git_manual=g_key_file_get_boolean(config,"insight-project-files","manual-selection",NULL);
  InsightProjectFiles *policy=error ? NULL : (insight_git_active() && !wb.git_manual ? insight_git_membership(&error) : insight_project_files_read_project(config,utf8,app->project->file_name,&error));
  wb.membership_valid=policy!=NULL;
  if(policy) insight_scroll_links_membership(policy,root);
  if(!policy) { policy=insight_project_files_new(); policy->automatic=FALSE; ui_set_statusbar(TRUE,"Project membership could not be restored: %s",error ? error->message : "invalid settings"); }
  insight_project_files_free(wb.membership); wb.membership=policy; g_free(wb.membership_project); wb.membership_project=g_strdup(app->project->file_name); g_clear_error(&error); if(loaded) { g_key_file_unref(loaded); config=NULL; }
  wb.workspace_from_project=TRUE;wb.membership_generation++;insight_master_sync_set(wb.master_sync,app->project->file_name,utf8,wb.membership);
  initial_membership_pending=project_is_creating();
  if(initial_membership_pending) {
   stop_scan();
   g_free(wb.geany_root);
   wb.geany_root=g_strdup(root);
  }
  else if(start_scan(root)) { g_free(wb.geany_root); wb.geany_root=g_strdup(root); browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); categories_filter(NULL,NULL); file_types_refresh(); focus_widget_panel(wb.file_entry); }
  else ui_set_statusbar(TRUE,"Project source folder cannot be indexed: %s",root);
 }
 if(!GPOINTER_TO_INT(data) && config && wb.membership_valid) { GError *error=NULL; if(!insight_project_files_write(wb.membership,config,utf8,&error)) ui_set_statusbar(TRUE,"Cannot save project membership: %s",error ? error->message : "invalid rules"); g_clear_error(&error); }
 g_free(root); g_free(utf8);
}
static void membership_apply_snapshot(const InsightProjectFiles *candidate,const gchar *root) {
 insight_scroll_links_membership(candidate,root);if(wb.usage_idle || wb.search_worker) { stop_usage(); gtk_label_set_text(GTK_LABEL(wb.search_status),"Project files changed; run the search again"); } insight_project_files_free(wb.membership); wb.membership=insight_project_files_clone(candidate); wb.membership_valid=TRUE;wb.membership_generation++; initial_membership_pending=FALSE; start_scan(root); files_filter(NULL,NULL);
}
static gboolean membership_store(const InsightProjectFiles *candidate) {
 if(!app->project || !wb.workspace_from_project) { ui_set_statusbar(TRUE,"Open a Geany project before changing its files"); return FALSE; }
 gchar *root=project_get_base_path(),*filename=utils_get_locale_from_utf8(app->project->file_name); GError *error=NULL;
 GFile *file=g_file_new_for_path(filename); GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_SIZE,G_FILE_QUERY_INFO_NONE,NULL,&error);
 gboolean ok=info && g_file_info_get_size(info)<=8*1024*1024;
 if(info) g_object_unref(info);
 if(!ok && !error) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Project configuration exceeds 8 MiB");
 GKeyFile *config=g_key_file_new();
 if(ok) ok=g_key_file_load_from_file(config,filename,G_KEY_FILE_KEEP_COMMENTS,&error);
 if(ok) {ok=insight_project_files_write(candidate,config,root,&error);if(ok && insight_git_active()) g_key_file_set_boolean(config,"insight-project-files","manual-selection",TRUE);}
 if(ok) { gsize length; gchar *data=g_key_file_to_data(config,&length,&error); ok=data!=NULL && length<=8*1024*1024; if(!ok && !error) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Project configuration exceeds 8 MiB"); if(ok) ok=g_file_replace_contents(file,data,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error); g_free(data); }
 if(ok) { if(insight_git_active()) wb.git_manual=TRUE; membership_apply_snapshot(candidate,root); }
 else ui_set_statusbar(TRUE,"Project files not changed: %s",error ? error->message : "cannot save configuration");
 g_clear_error(&error); g_key_file_unref(config); g_object_unref(file); g_free(filename); g_free(root); return ok;
}
static gboolean master_membership_applied(const InsightProjectFiles *policy,gpointer data) { (void)data;return wb.alive && membership_store(policy); }
static void master_sync_report(const GError *error,gpointer data) { (void)data;if(wb.alive) ui_set_statusbar(TRUE,"Master file list: %s",insight_tr(error->message)); }
static gboolean membership_apply(InsightProjectFiles *candidate) {
 if(!app->project || !wb.workspace_from_project) { ui_set_statusbar(TRUE,"Open a Geany project before changing its files");return FALSE; }
 if(candidate->master_file && *candidate->master_file) {
  GeanyDocument *master=document_find_by_filename(candidate->master_file);if(master && master->changed) { ui_set_statusbar(TRUE,"Save or reload the open master file list before changing project membership");return FALSE; }
  GError *error=NULL;gboolean attach=g_strcmp0(candidate->master_file,wb.membership->master_file)!=0;
  gboolean queued=attach ? insight_master_sync_attach(wb.master_sync,candidate,&error) : insight_master_sync_write(wb.master_sync,candidate,&error);
  if(queued) ui_set_statusbar(TRUE,attach ? "Loading master file list…" : "Saving master file list…");else if(error) ui_set_statusbar(TRUE,"Master file list: %s",insight_tr(error->message));g_clear_error(&error);return queued;
 }
 gboolean saved=membership_store(candidate);if(saved) { gchar *root=project_get_base_path();insight_master_sync_set(wb.master_sync,app->project->file_name,root,wb.membership);g_free(root); }return saved;
}
static void project_master_command(GtkMenuItem *item,gpointer data) {
 (void)item;if(!app->project || !wb.workspace_from_project || !wb.membership->master_file || !*wb.membership->master_file) { ui_set_statusbar(TRUE,"Choose a master file list in Add and Remove Project Files first");return; }
 if(GPOINTER_TO_INT(data)) { gchar *path=utils_get_locale_from_utf8(wb.membership->master_file);document_open_file(path,FALSE,NULL,NULL);g_free(path); }
 else { insight_master_sync_poll(wb.master_sync);ui_set_statusbar(TRUE,"Synchronizing master file list…"); }
}
typedef struct { GtkWidget *dialog,*query,*automatic,*masks,*master,*master_status,*view; GtkListStore *store; InsightProjectFiles *candidate; GPtrArray *paths,*trees; GHashTable *exact_rules; gchar *root,*project_file;guint64 generation;gboolean master_valid; GtkWidget *filename,*known,*available,*directory_view,*directory_label,*browser_status,*browser_controls;GtkListStore *available_store;GtkTreeStore *directory_store;GtkTreeIter directory_node;GPtrArray *entries;gchar *directory;GCancellable *read_cancel;guint64 read_serial; } MembershipDialog;
static void membership_browser_refresh(MembershipDialog *);
/* EN: Render the draft selection, sorting once after bulk insertion.
 * RU: Показывает черновик списка и сортирует его после пакетной вставки. */
static void membership_dialog_refresh(MembershipDialog *d)
{
 g_clear_pointer(&d->exact_rules,g_hash_table_unref);
 d->exact_rules=insight_project_files_exact_index(d->candidate);
 GtkTreeSortable *sort=GTK_TREE_SORTABLE(d->store);
 gtk_tree_sortable_set_sort_column_id(sort,GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,GTK_SORT_ASCENDING);
 gtk_list_store_clear(d->store);
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(d->query));
 GFile *base=g_file_new_for_path(d->root);
 for(guint kind=0;kind<2;kind++) {
  GPtrArray *paths=kind ? d->trees : d->paths;
  for(guint i=0;i<paths->len;i++) {
   const gchar *path=g_ptr_array_index(paths,i);
   gboolean included=kind ? g_ptr_array_find_with_equal_func(d->candidate->trees,path,(GEqualFunc)g_str_equal,NULL) :
    insight_project_files_contains_indexed(d->candidate,d->root,path,d->exact_rules);
   if(!included || !insight_lookup_text_match(path,query)) continue;
   GFile *file=g_file_new_for_path(path);
   gchar *name=g_file_get_relative_path(base,file);
   gtk_list_store_insert_with_values(d->store,NULL,-1,0,name ? name : path,1,path,
     2,kind!=0,3,kind ? "Folder (indexed on Apply)" : "File",-1);
   g_free(name);
   g_object_unref(file);
  }
 }
 g_object_unref(base);
 gtk_tree_sortable_set_sort_column_id(sort,0,GTK_SORT_ASCENDING);
 membership_browser_refresh(d);
}
static void membership_dialog_changed(GtkWidget *widget,gpointer data) {
 (void)widget; MembershipDialog *d=data;const gchar *master=gtk_entry_get_text(GTK_ENTRY(d->master));GError *error=NULL;gboolean valid=insight_project_files_set_master(d->candidate,master,d->root,d->project_file,&error);
 d->master_valid=valid;gtk_dialog_set_response_sensitive(GTK_DIALOG(d->dialog),GTK_RESPONSE_APPLY,valid && !d->read_cancel);gtk_label_set_text(GTK_LABEL(d->master_status),error ? insight_tr(error->message) : "");gtk_widget_set_visible(d->master_status,error!=NULL);g_clear_error(&error);
 gboolean automatic=!*master && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->automatic));
 if(!*master && d->candidate->automatic && !automatic) {
  GError *freeze_error=NULL;InsightProjectFiles *frozen=NULL;
  if(wb.scan || wb.scan_worker || !wb.report_scan_completed || wb.skipped || wb.files->len>=10000) g_set_error_literal(&freeze_error,G_IO_ERROR,G_IO_ERROR_BUSY,"Finish a complete project scan before switching to manual membership");
  else {GPtrArray *snapshot=g_ptr_array_new();for(guint i=0;i<wb.files->len;i++) g_ptr_array_add(snapshot,g_ptr_array_index(wb.files,i));for(guint i=0;i<d->paths->len;i++) g_ptr_array_add(snapshot,g_ptr_array_index(d->paths,i));frozen=insight_project_files_freeze(d->candidate,d->root,snapshot,&freeze_error);g_ptr_array_unref(snapshot);}
  if(frozen) {insight_project_files_free(d->candidate);d->candidate=frozen;for(guint i=0;i<frozen->files->len;i++) {const gchar *path=g_ptr_array_index(frozen->files,i);if(!g_ptr_array_find_with_equal_func(d->paths,path,(GEqualFunc)g_str_equal,NULL)) g_ptr_array_add(d->paths,g_strdup(path));}g_ptr_array_set_size(d->trees,0);}
  else {automatic=TRUE;g_signal_handlers_block_by_func(d->automatic,membership_dialog_changed,d);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->automatic),TRUE);g_signal_handlers_unblock_by_func(d->automatic,membership_dialog_changed,d);gtk_label_set_text(GTK_LABEL(d->browser_status),freeze_error ? insight_tr(freeze_error->message):"");}g_clear_error(&freeze_error);
 }
 d->candidate->automatic=automatic;gtk_widget_set_sensitive(d->automatic,!*master);if(*master) { g_signal_handlers_block_by_func(d->automatic,membership_dialog_changed,d);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->automatic),FALSE);g_signal_handlers_unblock_by_func(d->automatic,membership_dialog_changed,d); }g_free(d->candidate->masks); d->candidate->masks=g_strdup(gtk_entry_get_text(GTK_ENTRY(d->masks))); membership_dialog_refresh(d);
}
static void membership_master_choose(GtkButton *button,gpointer data) {
 (void)button;MembershipDialog *d=data;GtkWidget *chooser=gtk_file_chooser_dialog_new("Choose master file list",GTK_WINDOW(d->dialog),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)),*utf8=utils_get_utf8_from_locale(path);gchar *escaped=insight_project_files_escape_master(utf8);gtk_entry_set_text(GTK_ENTRY(d->master),escaped);g_free(escaped);g_free(utf8);g_free(path); }gtk_widget_destroy(chooser);
}
static void membership_dialog_file_list(GtkWidget *widget,gpointer data) {
 MembershipDialog *d=data; gboolean export=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"membership-export"));
 GtkWidget *chooser=gtk_file_chooser_dialog_new(export ? "Export project file list" : "Add from file list",GTK_WINDOW(d->dialog),export ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,export ? "Export" : "Add",GTK_RESPONSE_ACCEPT,NULL);
 if(export) { gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser),TRUE); gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser),"project-files.txt"); }
 if(gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)); GError *error=NULL;
  if(export) {
   GString *text=g_string_new(NULL); GFile *base=g_file_new_for_path(d->root); GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
   for(guint kind=0;kind<2;kind++) { GPtrArray *paths=kind ? d->candidate->trees : d->paths; for(guint i=0;i<paths->len;i++) { const char *p=g_ptr_array_index(paths,i); if(g_hash_table_contains(seen,p) || (!kind && !insight_project_files_contains(d->candidate,d->root,p))) continue; g_hash_table_add(seen,(gpointer)p); GFile *file=g_file_new_for_path(p); gchar *relative=g_file_get_relative_path(base,file); if(relative) for(gchar *c=relative;*c;c++) if(G_IS_DIR_SEPARATOR(*c)) *c='/'; g_string_append_printf(text,"%s\n",relative ? relative : (g_file_equal(base,file) ? "." : p)); g_free(relative); g_object_unref(file); } }
   GFile *file=g_file_new_for_path(path); if(!g_file_replace_contents(file,text->str,text->len,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error)) ui_set_statusbar(TRUE,"Cannot export project list: %s",error ? error->message : "write failed"); g_object_unref(file); g_hash_table_destroy(seen); g_object_unref(base); g_string_free(text,TRUE);
  } else {
   gchar *utf8=utils_get_utf8_from_locale(path),*text=read_disk_source(utf8,TRUE); g_free(utf8);
   if(!text) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"File list must be decodable text, at most 2 MiB");
   else {
    InsightProjectFiles *next=insight_project_files_clone(d->candidate); GPtrArray *added=g_ptr_array_new_with_free_func(g_free),*trees=g_ptr_array_new_with_free_func(g_free); gchar **lines=g_strsplit(g_str_has_prefix(text,"\xEF\xBB\xBF") ? text+3 : text,"\n",-1); guint count=0;
    for(guint i=0;lines[i] && !error;i++) { gsize length=strlen(lines[i]); if(length && lines[i][length-1]=='\r') lines[i][--length]=0; if(!length) continue;
     if(++count>10000) { g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"File list exceeds 10000 entries"); break; }
     gchar *p=insight_canonicalize_filename(lines[i],d->root),*native=utils_get_locale_from_utf8(p); gboolean tree=g_file_test(native,G_FILE_TEST_IS_DIR); g_free(native);
     if(insight_project_files_set(next,p,tree,TRUE,&error)) g_ptr_array_add(tree ? trees : added,p); else g_free(p);
    }
    if(!error) { insight_project_files_free(d->candidate); d->candidate=next; next=NULL; GPtrArray *from[]={added,trees},*to[]={d->paths,d->trees}; for(guint kind=0;kind<2;kind++) for(guint i=0;i<from[kind]->len;i++) { const char *p=g_ptr_array_index(from[kind],i); if(!g_ptr_array_find_with_equal_func(to[kind],p,(GEqualFunc)g_str_equal,NULL)) g_ptr_array_add(to[kind],g_strdup(p)); } membership_dialog_refresh(d); }
    insight_project_files_free(next); g_ptr_array_free(added,TRUE); g_ptr_array_free(trees,TRUE); g_strfreev(lines); g_free(text);
   }
   if(error) ui_set_statusbar(TRUE,"Project list not imported: %s",error->message);
  }
  g_clear_error(&error); g_free(path);
 }
 gtk_widget_destroy(chooser);
}
