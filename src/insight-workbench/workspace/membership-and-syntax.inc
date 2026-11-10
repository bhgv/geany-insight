/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Project membership dialogs and script syntax checks.
 * RU: Диалоги состава проекта и проверка синтаксиса скриптов. */

static void membership_dialog_remove(GtkWidget *widget,gpointer data) {
 MembershipDialog *d=data; gint mode=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),"membership-remove"));
 if(mode==2) { gchar *master=g_strdup(d->candidate->master_file),*expression=g_strdup(d->candidate->master_expression);insight_project_files_free(d->candidate); d->candidate=insight_project_files_new();g_free(d->candidate->master_file);d->candidate->master_file=master;g_free(d->candidate->master_expression);d->candidate->master_expression=expression; d->candidate->automatic=FALSE; gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->automatic),FALSE); gtk_entry_set_text(GTK_ENTRY(d->masks),""); g_ptr_array_set_size(d->paths,0); g_ptr_array_set_size(d->trees,0); membership_dialog_refresh(d); return; }
 if(mode==1) {
  GtkTreeModel *model=NULL;GList *rows=gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(d->available)),&model);gboolean removed=FALSE;GError *error=NULL;
  for(GList *p=rows;p && !error;p=p->next) {GtkTreeIter it;gchar *path=NULL;gboolean tree=FALSE;if(gtk_tree_model_get_iter(model,&it,p->data)) {gtk_tree_model_get(model,&it,1,&path,2,&tree,-1);if(tree) {removed=TRUE;insight_project_files_set(d->candidate,path,TRUE,FALSE,&error);}g_free(path);}}g_list_free_full(rows,(GDestroyNotify)gtk_tree_path_free);
  if(!removed && !error) insight_project_files_set(d->candidate,d->directory,TRUE,FALSE,&error);if(error) gtk_label_set_text(GTK_LABEL(d->browser_status),error ? error->message : "Cannot remove project tree");g_clear_error(&error);
 } else {
  GtkTreeModel *model;
  GList *rows=gtk_tree_selection_get_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(d->view)),&model);
  GPtrArray *files=g_ptr_array_new_with_free_func(g_free);
  GError *error=NULL;
  for(GList *p=rows;p && !error;p=p->next) {
   GtkTreeIter iter;
   if(!gtk_tree_model_get_iter(model,&iter,p->data)) continue;
   gchar *path;
   gboolean tree;
   gtk_tree_model_get(model,&iter,1,&path,2,&tree,-1);
   if(!tree) {
    g_ptr_array_add(files,path);
    continue;
   }
   if(insight_project_files_set_batch(d->candidate,files,FALSE,&error)) {
    g_ptr_array_set_size(files,0);
    insight_project_files_set(d->candidate,path,TRUE,FALSE,&error);
   }
   for(guint i=d->trees->len;!error && i>0;i--)
    if(!g_strcmp0(path,g_ptr_array_index(d->trees,i-1))) g_ptr_array_remove_index(d->trees,i-1);
   g_free(path);
  }
  if(!error) insight_project_files_set_batch(d->candidate,files,FALSE,&error);
  if(error) ui_set_statusbar(TRUE,"Cannot remove project path: %s",error->message);
  g_clear_error(&error);
  g_ptr_array_unref(files);
  g_list_free_full(rows,(GDestroyNotify)gtk_tree_path_free);
 }
 membership_dialog_refresh(d);
}
#include "../../insight-project-membership.h"
static void manage_project_files(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; if(!app->project || !wb.workspace_from_project) { ui_set_statusbar(TRUE,"Open a Geany project before changing its files"); return; }
 MembershipDialog d={0};d.generation=wb.membership_generation; d.candidate=insight_project_files_clone(wb.membership); d.paths=g_ptr_array_new_with_free_func(g_free); d.trees=g_ptr_array_new_with_free_func(g_free); d.root=project_get_base_path();d.project_file=g_strdup(app->project->file_name);
 for(guint i=0;i<wb.files->len;i++) g_ptr_array_add(d.paths,g_strdup(g_ptr_array_index(wb.files,i)));
 for(guint i=0;i<d.candidate->files->len;i++) { const char *path=g_ptr_array_index(d.candidate->files,i); if(!g_ptr_array_find_with_equal_func(d.paths,path,(GEqualFunc)g_str_equal,NULL)) g_ptr_array_add(d.paths,g_strdup(path)); }
 for(guint i=0;i<d.candidate->trees->len;i++) g_ptr_array_add(d.trees,g_strdup(g_ptr_array_index(d.candidate->trees,i)));
 d.dialog=gtk_dialog_new_with_buttons("Add and Remove Project Files",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Apply"),GTK_RESPONSE_APPLY,NULL);gtk_window_set_default_size(GTK_WINDOW(d.dialog),1040,600);gtk_widget_set_name(d.dialog,"insight-project-membership");g_object_set_data(G_OBJECT(d.dialog),"membership-browser",&d);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(d.dialog));gtk_container_set_border_width(GTK_CONTAINER(box),8);
 GtkWidget *filename_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_pack_start(GTK_BOX(filename_row),gtk_label_new(insight_tr("File Name")),FALSE,FALSE,0);d.filename=gtk_entry_new();gtk_widget_set_name(d.filename,"insight-project-membership-filename");gtk_widget_set_tooltip_text(d.filename,insight_tr("Enter a directory or filename masks such as *.c;*.h, then press Enter"));gtk_box_pack_start(GTK_BOX(filename_row),d.filename,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(box),filename_row,FALSE,FALSE,3);g_signal_connect(d.filename,"activate",G_CALLBACK(membership_filename_activate),&d);g_signal_connect(d.filename,"changed",G_CALLBACK(membership_filename_changed),&d);
 d.directory_label=gtk_label_new(d.root);gtk_label_set_selectable(GTK_LABEL(d.directory_label),TRUE);gtk_label_set_ellipsize(GTK_LABEL(d.directory_label),PANGO_ELLIPSIZE_MIDDLE);gtk_label_set_xalign(GTK_LABEL(d.directory_label),0);gtk_box_pack_start(GTK_BOX(box),d.directory_label,FALSE,FALSE,3);
 GtkWidget *columns=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_box_pack_start(GTK_BOX(box),columns,TRUE,TRUE,3);
 GtkWidget *directories=membership_directory_list(&d);gtk_box_pack_start(GTK_BOX(columns),directories,TRUE,TRUE,0);
 GtkWidget *available=membership_list(&d.available_store,&d.available,"insight-project-membership-available",insight_tr("File Name"));gtk_box_pack_start(GTK_BOX(columns),available,TRUE,TRUE,0);g_signal_connect(d.available,"row-activated",G_CALLBACK(membership_browser_activate),&d);g_signal_connect(d.directory_view,"row-activated",G_CALLBACK(membership_browser_activate),&d);gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(d.directory_view),TRUE);
 d.store=gtk_list_store_new(4,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_STRING);d.view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(d.store));gtk_widget_set_name(d.view,"insight-project-membership-files");gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(d.view),-1,insight_tr("Project Files"),gtk_cell_renderer_text_new(),"text",0,NULL);gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(d.store),0,GTK_SORT_ASCENDING);gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(d.view)),GTK_SELECTION_MULTIPLE);gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(d.view),1);GtkWidget *project_scroll=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(project_scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(project_scroll),d.view);gtk_box_pack_start(GTK_BOX(columns),project_scroll,TRUE,TRUE,0);
 GtkWidget *buttons=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);d.browser_controls=buttons;gtk_box_pack_start(GTK_BOX(columns),buttons,FALSE,FALSE,0);
 const gchar *add_labels[]={"Add","Add All","Add Tree"};for(guint i=0;i<3;i++) {GtkWidget *button=gtk_button_new_with_label(insight_tr(add_labels[i]));gtk_widget_set_name(button,i==0 ? "insight-membership-add":i==1 ? "insight-membership-add-all":"insight-membership-add-tree");g_object_set_data(G_OBJECT(button),"browser-add",GUINT_TO_POINTER(i));g_signal_connect(button,"clicked",G_CALLBACK(membership_browser_add),&d);gtk_box_pack_start(GTK_BOX(buttons),button,FALSE,FALSE,0);}
 const gchar *remove_labels[]={"Remove File","Remove Tree","Remove All"};for(guint i=0;i<3;i++) {GtkWidget *button=gtk_button_new_with_label(insight_tr(remove_labels[i]));gtk_widget_set_name(button,i==0 ? "insight-membership-remove":i==1 ? "insight-membership-remove-tree":"insight-membership-remove-all");g_object_set_data(G_OBJECT(button),"membership-remove",GUINT_TO_POINTER(i));g_signal_connect(button,"clicked",G_CALLBACK(membership_dialog_remove),&d);gtk_box_pack_start(GTK_BOX(buttons),button,FALSE,FALSE,0);}
 GtkWidget *hint=gtk_label_new(insight_tr("Choose the project files, then click Apply to save and index them."));
 gtk_label_set_xalign(GTK_LABEL(hint),0);
 gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,3);
 GtkWidget *types_button=gtk_button_new_with_label(insight_tr("File Type Options…"));g_signal_connect(types_button,"clicked",G_CALLBACK(membership_filetypes),&d);gtk_box_pack_start(GTK_BOX(buttons),types_button,FALSE,FALSE,0);
 GtkWidget *special=gtk_button_new_with_label(insight_tr("Remove Special…"));gtk_widget_set_name(special,"insight-membership-remove-special");g_signal_connect(special,"clicked",G_CALLBACK(membership_remove_special),&d);gtk_box_pack_start(GTK_BOX(buttons),special,FALSE,FALSE,0);
 GtkWidget *from_list=gtk_button_new_with_label(insight_tr("Add from list…"));gtk_widget_set_name(from_list,"insight-membership-import");g_signal_connect(from_list,"clicked",G_CALLBACK(membership_browser_import),&d);gtk_box_pack_start(GTK_BOX(buttons),from_list,FALSE,FALSE,0);
 d.known=gtk_check_button_new_with_label(insight_tr("Show only known file types"));gtk_widget_set_name(d.known,"insight-membership-known");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.known),TRUE);gtk_box_pack_start(GTK_BOX(box),d.known,FALSE,FALSE,2);g_signal_connect(d.known,"toggled",G_CALLBACK(membership_known_changed),&d);
 d.browser_status=gtk_label_new("");gtk_widget_set_name(d.browser_status,"insight-membership-status");gtk_label_set_line_wrap(GTK_LABEL(d.browser_status),TRUE);gtk_label_set_xalign(GTK_LABEL(d.browser_status),0);gtk_box_pack_start(GTK_BOX(box),d.browser_status,FALSE,FALSE,0);
 GtkWidget *advanced=gtk_expander_new(insight_tr("Project file rules and master list")),*advanced_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_container_add(GTK_CONTAINER(advanced),advanced_box);gtk_box_pack_start(GTK_BOX(box),advanced,FALSE,FALSE,0);
 box=advanced_box;d.query=gtk_search_entry_new();gtk_widget_set_name(d.query,"insight-project-membership-query");gtk_entry_set_placeholder_text(GTK_ENTRY(d.query),insight_tr("Filter project paths…"));gtk_widget_set_tooltip_text(d.query,insight_tr("Filter project files; prefix with ~ for fuzzy matching"));gtk_box_pack_start(GTK_BOX(box),d.query,FALSE,FALSE,0);
 d.automatic=gtk_check_button_new_with_label(insight_tr("Automatically include known source files under the project base folder"));gtk_widget_set_name(d.automatic,"insight-membership-automatic");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d.automatic),d.candidate->automatic);gtk_box_pack_start(GTK_BOX(box),d.automatic,FALSE,FALSE,0);
 GtkWidget *master_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*master_choose=gtk_button_new_with_label("Browse…");d.master=gtk_entry_new();gtk_widget_set_name(d.master,"insight-project-master-list");gtk_widget_set_tooltip_text(d.master,"Path variables: %PROJECT_SOURCE_DIR%, %PROJECT_DATA_DIR%, %PROJECT_NAME%, %TEMP_DIR% and environment names; use %% for a literal percent sign");gtk_entry_set_placeholder_text(GTK_ENTRY(d.master),"Master file list (optional; existing UTF-8 file)");gchar *master_text=*d.candidate->master_expression ? g_strdup(d.candidate->master_expression) : insight_project_files_escape_master(d.candidate->master_file);gtk_entry_set_text(GTK_ENTRY(d.master),master_text);g_free(master_text);gtk_widget_set_hexpand(d.master,TRUE);gtk_box_pack_start(GTK_BOX(master_row),d.master,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(master_row),master_choose,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),master_row,FALSE,FALSE,3);g_signal_connect(master_choose,"clicked",G_CALLBACK(membership_master_choose),&d);gtk_widget_set_sensitive(d.automatic,!*d.candidate->master_file);d.master_status=gtk_label_new("");gtk_widget_set_name(d.master_status,"insight-project-master-error");gtk_label_set_line_wrap(GTK_LABEL(d.master_status),TRUE);gtk_widget_set_no_show_all(d.master_status,TRUE);gtk_box_pack_start(GTK_BOX(box),d.master_status,FALSE,FALSE,0);
 GtkWidget *lists=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); for(guint i=0;i<2;i++) { GtkWidget *button=gtk_button_new_with_label(i ? "Export file list…" : "Add from list…"); g_object_set_data(G_OBJECT(button),"membership-export",GUINT_TO_POINTER(i)); g_signal_connect(button,"clicked",G_CALLBACK(membership_dialog_file_list),&d); gtk_box_pack_start(GTK_BOX(lists),button,FALSE,FALSE,0); } gtk_box_pack_start(GTK_BOX(box),lists,FALSE,FALSE,3);
 d.masks=gtk_entry_new(); gtk_entry_set_max_length(GTK_ENTRY(d.masks),4096); gtk_entry_set_placeholder_text(GTK_ENTRY(d.masks),"Exclude masks: *.generated.c;vendor/* (case sensitive)"); gtk_entry_set_text(GTK_ENTRY(d.masks),d.candidate->masks); gtk_box_pack_start(GTK_BOX(box),d.masks,FALSE,FALSE,3);
 g_signal_connect(d.master,"changed",G_CALLBACK(membership_dialog_changed),&d);g_signal_connect(d.query,"changed",G_CALLBACK(membership_dialog_changed),&d); g_signal_connect(d.masks,"changed",G_CALLBACK(membership_dialog_changed),&d); g_signal_connect(d.automatic,"toggled",G_CALLBACK(membership_dialog_changed),&d); membership_dialog_changed(NULL,&d); gtk_widget_show_all(d.dialog);membership_browse(&d,d.root);
 while(gtk_dialog_run(GTK_DIALOG(d.dialog))==GTK_RESPONSE_APPLY) {
  if(!d.master_valid || d.read_cancel) continue;
  if(d.generation!=wb.membership_generation) {ui_set_statusbar(TRUE,"Project files changed while the dialog was open; reopen it before applying");break;}
  if(membership_apply(d.candidate)) break;
  gtk_label_set_text(GTK_LABEL(d.browser_status),insight_tr("Project files could not be saved. Check project permissions or the master file list."));
 }
 g_object_set_data(G_OBJECT(d.dialog),"membership-browser",NULL);if(d.read_cancel) g_cancellable_cancel(d.read_cancel);g_clear_object(&d.read_cancel);g_clear_pointer(&d.entries,g_ptr_array_unref);g_free(d.directory);g_object_unref(d.available_store);g_object_unref(d.directory_store);gtk_widget_destroy(d.dialog); g_object_unref(d.store); g_clear_pointer(&d.exact_rules,g_hash_table_unref); insight_project_files_free(d.candidate); g_ptr_array_free(d.paths,TRUE); g_ptr_array_free(d.trees,TRUE); g_free(d.root);g_free(d.project_file);
}
static void project_files_drop(GtkWidget *widget,GdkDragContext *context,gint x,gint y,GtkSelectionData *selection,guint info,guint time,gpointer data) {
 (void)widget; (void)x; (void)y; (void)info; (void)data; gboolean ok=FALSE;
 if(app->project && wb.workspace_from_project) { InsightProjectFiles *candidate=insight_project_files_clone(wb.membership); gchar **uris=gtk_selection_data_get_uris(selection); guint count=0; GError *error=NULL;
  for(guint i=0;uris && uris[i] && !error;i++) { gchar *path=g_filename_from_uri(uris[i],NULL,&error); if(path) { gchar *utf8=utils_get_utf8_from_locale(path); if(insight_project_files_set(candidate,utf8,g_file_test(path,G_FILE_TEST_IS_DIR),TRUE,&error)) count++; g_free(utf8); g_free(path); } }
  if(!error && count) ok=membership_apply(candidate); else if(error) ui_set_statusbar(TRUE,"Cannot add dropped project files: %s",error->message);
  g_clear_error(&error); g_strfreev(uris); insight_project_files_free(candidate);
 } else ui_set_statusbar(TRUE,"Open a Geany project before dropping project files");
 gtk_drag_finish(context,ok,FALSE,time);
}
static void native_project_close(GObject *object,gpointer data) {
 initial_membership_pending=FALSE;
 hdl_invalidate_colors();
 insight_git_close();
 wb.membership_generation++;insight_master_sync_set(wb.master_sync,NULL,NULL,NULL);
 bookmarks_project_load(NULL,NULL,FALSE);clips_project_load(NULL,FALSE);
 (void)object; (void)data; g_clear_pointer(&wb.geany_root,g_free); g_clear_pointer(&wb.membership_project,g_free); wb.membership_valid=FALSE;
 if(!wb.workspace_from_project) return;
 wb.workspace_from_project=FALSE; invalidate_search_context(); stop_scan(); insight_workspace_watch_clear(wb.watch); wb.watch_changed=FALSE; if(wb.watch_idle) { g_source_remove(wb.watch_idle); wb.watch_idle=0; } retire_index(wb.base); wb.base=insight_index_new();g_hash_table_remove_all(wb.declarations); mark_index_dirty(); g_ptr_array_set_size(wb.files,0); g_clear_pointer(&wb.root,g_free);
 /* Project databases survive project-close and application restart. */
 refresh_index(); browser_filter(NULL,NULL); files_filter(NULL,NULL); tree_refresh(); categories_filter(NULL,NULL); file_types_refresh(); insight_workbench_refresh();
}
typedef struct {
 guint document;
 guint64 epoch;
 gboolean python;
 gchar *text, *interpreter, *message;
 guint line, column;
} ScriptCheck;
static gboolean script_check_pending;
static void script_check_free(gpointer data) {
 ScriptCheck *check=data;
 g_free(check->text);
 g_free(check->interpreter);
 g_free(check->message);
 g_free(check);
}
static guint script_check_position(const gchar *text) {
 guint64 position=0;
 if(!text || !g_ascii_string_to_unsigned(text,10,1,G_MAXINT,&position,NULL)) return 0;
 return (guint)position;
}
static void script_check_run(GTask *task,gpointer object,gpointer data,GCancellable *cancel) {
 (void)object;
 ScriptCheck *check=data;
 if(check->python) {
  static const gchar code[]=
   "import sys\n"
   "try:\n"
   "    compile(sys.stdin.buffer.read().decode('utf-8'), '<buffer>', 'exec', dont_inherit=True)\n"
   "except SyntaxError as error:\n"
   "    fields = (str(error.lineno or 0), str(error.offset or 0), error.msg.replace(chr(10), ' '))\n"
   "    sys.stdout.buffer.write('\\t'.join(fields).encode('utf-8'))\n";
  GError *error=NULL;
  GSubprocess *process=g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE|G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE,
      &error,check->interpreter,"-I","-S","-c",code,NULL);
  gchar *output=NULL,*diagnostic=NULL;
  gboolean ok=process && g_subprocess_communicate_utf8(process,check->text,cancel,&output,&diagnostic,&error);
  if(ok) ok=g_subprocess_get_successful(process);
  if(ok && output && *output) {
   gchar **fields=g_strsplit(g_strchomp(output),"\t",3);
   if(g_strv_length(fields)==3) {
    check->line=script_check_position(fields[0]);
    check->column=script_check_position(fields[1]);
    check->message=g_strdup(fields[2]);
   } else check->message=g_strdup("Unexpected Python compiler response");
   g_strfreev(fields);
  } else if(!ok) check->message=g_strdup(error ? error->message :
      diagnostic && *diagnostic ? g_strstrip(diagnostic) : "Python compiler failed");
  g_free(output);
  g_free(diagnostic);
  g_clear_error(&error);
  g_clear_object(&process);
 } else {
  gchar error[1024];
  gint status=insight_lua_check(check->text,strlen(check->text),error,sizeof error);
  if(status) {
   gchar *position=g_str_has_prefix(error,"buffer:") ? error+7 : NULL;
   gchar *separator=position ? strchr(position,':') : NULL;
   if(separator) {
    *separator='\0';
    check->line=script_check_position(position);
    check->message=g_strdup(g_strstrip(separator+1));
   } else check->message=g_strdup(*error ? error : "Lua compiler could not be initialized");
  }
 }
 g_task_return_boolean(task,TRUE);
}
static void script_check_done(GObject *object,GAsyncResult *result,gpointer data) {
 (void)object;(void)data;
 GTask *task=G_TASK(result);
 ScriptCheck *check=g_task_get_task_data(task);
 script_check_pending=FALSE;
 if(!wb.alive || check->epoch!=workbench_epoch || !g_task_propagate_boolean(task,NULL)) return;
 GeanyDocument *doc=document_find_by_id(check->document);
 if(!DOC_VALID(doc)) return;
 gint length=sci_get_length(doc->editor->sci);
 if((gsize)length!=strlen(check->text)) return;
 gchar *current=sci_get_contents(doc->editor->sci,length+1);
 gboolean unchanged=!memcmp(current,check->text,length);
 g_free(current);
 if(!unchanged) return;
 const gchar *language=check->python ? "Python" : "Lua";
 if(!check->message) ui_set_statusbar(TRUE,"%s syntax check passed",language);
 else {
  gint line=check->line && check->line<=(guint)sci_get_line_count(doc->editor->sci) ? (gint)check->line : -1;
  if(line>0 && check->column)
   msgwin_msg_add(COLOR_RED,line,doc,"%s:%u:%u: %s",language,check->line,check->column,check->message);
  else if(line>0) msgwin_msg_add(COLOR_RED,line,doc,"%s:%u: %s",language,check->line,check->message);
  else msgwin_msg_add(COLOR_RED,-1,NULL,"%s: %s",language,check->message);
  msgwin_switch_tab(MSG_MESSAGE,TRUE);
 }
}
static void script_check_start(GtkMenuItem *item,gpointer data) {
 (void)item;
 GeanyDocument *doc=document_get_current();
 if(!DOC_VALID(doc) || !doc->file_type) return;
 if(script_check_pending) {ui_set_statusbar(TRUE,"Syntax check already running");return;}
 gboolean python=python_language(doc->file_type);
 if((GPOINTER_TO_INT(data) && !python) || (!GPOINTER_TO_INT(data) && doc->file_type->lang != TM_PARSER_LUA)) {
  ui_set_statusbar(TRUE, "%s", insight_tr(GPOINTER_TO_INT(data) ?
      "Open a Python file first" : "Open a Lua file first"));
  return;
 }
 gint length=sci_get_length(doc->editor->sci);
 if(length>2*1024*1024) {
  ui_set_statusbar(TRUE,"Syntax check supports buffers up to 2 MiB");
  return;
 }
 ScriptCheck *check=g_new0(ScriptCheck,1);
 check->document=doc->id;
 check->epoch=workbench_epoch;
 check->python=python;
 check->text=sci_get_contents(doc->editor->sci,length+1);
 if(memchr(check->text,0,length) || !g_utf8_validate(check->text,length,NULL)) {
  ui_set_statusbar(TRUE,"Syntax check requires a UTF-8 text buffer");
  script_check_free(check);
  return;
 }
 if(python) {
  const gchar *configured=g_getenv("GEANY_INSIGHT_PYTHON");
  check->interpreter=g_find_program_in_path(configured && *configured ? configured : "python3");
  if(!check->interpreter && (!configured || !*configured)) check->interpreter=g_find_program_in_path("python");
  if(!check->interpreter) {
   ui_set_statusbar(TRUE,"Python 3 not found; set GEANY_INSIGHT_PYTHON to its executable");
   script_check_free(check);
   return;
  }
 }
 script_check_pending=TRUE;
 GTask *task=g_task_new(NULL,NULL,script_check_done,NULL);
 g_task_set_task_data(task,check,script_check_free);
 g_task_run_in_thread(task,script_check_run);
 g_object_unref(task);
}
