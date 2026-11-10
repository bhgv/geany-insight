/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Bookmark preferences and context helpers.
 * RU: Настройки закладок и вспомогательные операции контекста. */

static void bookmark_options(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Bookmark Options",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL);
 gtk_widget_set_name(dialog,"insight-bookmark-options");gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*confirm=gtk_check_button_new_with_label("Confirm bookmark deletion"),*close=gtk_check_button_new_with_label("Delete bookmarks when file closes"),*preserve=gtk_check_button_new_with_label("Preserve bookmarks between sessions"),*font=insight_list_style_button(wb.marks_view),*help=insight_panel_help_button("panel-bookmarks.md"),*status=gtk_label_new("");
 gtk_widget_set_name(preserve,"insight-bookmark-options-preserve");gtk_widget_set_name(confirm,"insight-bookmark-options-confirm");gtk_widget_set_name(close,"insight-bookmark-options-close");gtk_widget_set_name(font,"insight-bookmark-options-font");gtk_widget_set_name(status,"insight-bookmark-options-status");
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preserve),wb.marks_preserve);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(confirm),wb.marks_confirm_delete);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(close),wb.marks_delete_on_close);
 gtk_label_set_line_wrap(GTK_LABEL(status),TRUE);gtk_box_pack_start(GTK_BOX(box),preserve,FALSE,FALSE,6);gtk_box_pack_start(GTK_BOX(box),confirm,FALSE,FALSE,6);gtk_box_pack_start(GTK_BOX(box),close,FALSE,FALSE,6);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_pack_start(GTK_BOX(row),font,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),help,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,6);gtk_box_pack_start(GTK_BOX(box),status,FALSE,FALSE,6);
 insight_i18n_apply(dialog);gtk_widget_show_all(dialog);
 while(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_APPLY) {
  gboolean a=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(confirm)),b=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(close));GError *error=NULL;
  gboolean c=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(preserve));
  if(bookmark_options_save(a,b,c,&error)) { wb.marks_confirm_delete=a;wb.marks_delete_on_close=b;wb.marks_preserve=c;break; }
  gtk_label_set_text(GTK_LABEL(status),insight_tr(error ? error->message : "Workbench settings not saved"));g_clear_error(&error);
 }
 gtk_widget_destroy(dialog);
}
static void bookmark_menu_action(GtkMenuItem *item,gpointer data) {
 (void)item;guint action=GPOINTER_TO_UINT(data);
 if(action==0) bookmark_go(NULL,NULL);
 else if(action==1) note_rename(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK));
 else if(action==2) note_delete(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK));
 else if(action==3 || action==4 || action==5) notes_transfer(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK|(action==3 ? 256 : action==4 ? 768 : 0)));
 else if(action==6) gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)));
 else if(action==7) gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)));
 else if(action==8) bookmark_delete_all();
 else if(action==9) wb.marks_confirm_delete=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
 else if(action==10) wb.marks_delete_on_close=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item));
 else if(action==11) bookmark_options(NULL,NULL);
}
static GtkWidget *bookmark_menu(void) {
 GtkWidget *menu=g_object_get_data(G_OBJECT(wb.marks_view),"insight-bookmark-popup");
 const gchar *labels[]={"Go to","Rename…","Delete selected bookmarks","Export selected…","Export all bookmarks…","Import…","Select all bookmarks","Clear bookmark selection","Delete all bookmarks…","Confirm bookmark deletion","Delete bookmarks when file closes","Bookmark Options…"};
 const gchar *icons[]={"go-jump","document-edit","edit-delete","document-save","document-save-as","document-open","edit-select-all","edit-clear","edit-delete","dialog-question","window-close","preferences-system"};
 if(!menu) { menu=gtk_menu_new();gtk_menu_attach_to_widget(GTK_MENU(menu),wb.marks_view,NULL);g_object_set_data_full(G_OBJECT(wb.marks_view),"insight-bookmark-popup",g_object_ref_sink(menu),g_object_unref);
  for(guint i=0;i<G_N_ELEMENTS(labels);i++) { GtkWidget *item=(i==9 || i==10) ? gtk_check_menu_item_new_with_label(labels[i]) : symbol_history_item(labels[i],icons[i]);if(i==9 || i==10) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),i==9 ? wb.marks_confirm_delete : wb.marks_delete_on_close);gchar *name=g_strdup_printf("insight-bookmark-command-%u",i);gtk_widget_set_name(item,name);g_object_set_data(G_OBJECT(menu),name,item);g_free(name);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(bookmark_menu_action),GUINT_TO_POINTER(i)); }
  gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
  GtkWidget *appearance=insight_list_style_menu_item(wb.marks_view);gtk_widget_set_name(appearance,"insight-bookmark-appearance");gtk_menu_shell_append(GTK_MENU_SHELL(menu),appearance);
 }
 for(guint i=9;i<=10;i++) { gchar *name=g_strdup_printf("insight-bookmark-command-%u",i);GtkWidget *item=g_object_get_data(G_OBJECT(menu),name);g_free(name);g_signal_handlers_block_by_func(item,bookmark_menu_action,GUINT_TO_POINTER(i));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),i==9 ? wb.marks_confirm_delete : wb.marks_delete_on_close);g_signal_handlers_unblock_by_func(item,bookmark_menu_action,GUINT_TO_POINTER(i)); }
 gint count=gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.marks_view)));
 for(guint i=0;i<G_N_ELEMENTS(labels);i++) { gchar *name=g_strdup_printf("insight-bookmark-command-%u",i);GtkWidget *item=g_object_get_data(G_OBJECT(menu),name);g_free(name);gtk_widget_set_sensitive(item,i<2 ? count==1 : i==2 || i==3 || i==7 ? count>0 : TRUE); }
 insight_i18n_apply(menu);gtk_widget_show_all(menu);return menu;
}
static gboolean bookmark_popup_key(GtkWidget *widget,gpointer data) {
 (void)data;GdkEvent *event=gtk_get_current_event();gtk_menu_popup_at_widget(GTK_MENU(bookmark_menu()),widget,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,event);if(event) gdk_event_free(event);return TRUE;
}
static gboolean bookmark_popup_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));GtkTreePath *path=NULL;
 g_signal_handlers_block_by_func(selection,note_selected,GINT_TO_POINTER(INSIGHT_BOOKMARK));
 if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)) { if(!gtk_tree_selection_path_is_selected(selection,path)) { gtk_tree_selection_unselect_all(selection);gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget),path,NULL,FALSE); }gtk_tree_path_free(path); }
 g_signal_handlers_unblock_by_func(selection,note_selected,GINT_TO_POINTER(INSIGHT_BOOKMARK));gtk_widget_grab_focus(widget);gtk_menu_popup_at_pointer(GTK_MENU(bookmark_menu()),(GdkEvent *)event);return TRUE;
}
static gboolean bookmark_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data;guint modifiers=event->state&gtk_accelerator_get_default_mod_mask(),key=gdk_keyval_to_lower(event->keyval);
 if(widget==wb.marks_view) {
  if((key==GDK_KEY_Menu && !modifiers) || (key==GDK_KEY_F10 && modifiers==GDK_SHIFT_MASK)) return bookmark_popup_key(widget,NULL);
  if(key==GDK_KEY_a && modifiers==GEANY_PRIMARY_MOD_MASK) { gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));return TRUE; }
  if(key==GDK_KEY_Delete && !modifiers) { note_delete(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK));return TRUE; }
 }
 if(modifiers==GEANY_PRIMARY_MOD_MASK) {
  if(key==GDK_KEY_q) { bookmark_options(NULL,NULL);return TRUE; }
  if(key==GDK_KEY_n) { bookmark_add(NULL,NULL);return TRUE; }
  if(key==GDK_KEY_x) { note_delete(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK));return TRUE; }
  if(key==GDK_KEY_o || key==GDK_KEY_s) { notes_transfer(NULL,GINT_TO_POINTER(INSIGHT_BOOKMARK|(key==GDK_KEY_s ? 256 : 0)));return TRUE; }
 }
 if(widget==wb.marks_view && !modifiers && (key==GDK_KEY_Return || key==GDK_KEY_KP_Enter)) { bookmark_go(NULL,NULL);return TRUE; }
 return browser_key(widget,event,NULL);
}
static void bookmark_file_cell(GtkTreeViewColumn *column,GtkCellRenderer *renderer,GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)column;(void)data;gchar *path=NULL,*relative=NULL;gtk_tree_model_get(model,iter,1,&path,-1);
 if(path && wb.marks_project && wb.project_marks_root) { GFile *root=g_file_new_for_path(wb.project_marks_root),*file=g_file_new_for_path(path);relative=g_file_get_relative_path(root,file);g_object_unref(root);g_object_unref(file); }
 g_object_set(renderer,"text",relative ? relative : path,NULL);g_free(relative);g_free(path);
}
static void create_note_panel(InsightNoteKind kind) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3),*field=gtk_search_entry_new(),*controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2); GtkListStore *store=gtk_list_store_new(5,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING,G_TYPE_STRING); GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
 if(kind==INSIGHT_BOOKMARK) { GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);wb.marks_scope=gtk_label_new("Global bookmarks (no project)");gtk_widget_set_name(wb.marks_scope,"insight-bookmark-scope");gtk_label_set_ellipsize(GTK_LABEL(wb.marks_scope),PANGO_ELLIPSIZE_MIDDLE);gtk_label_set_xalign(GTK_LABEL(wb.marks_scope),0);wb.marks_reload=gtk_button_new_with_label("Reload bookmarks");gtk_widget_set_name(wb.marks_reload,"insight-reload-project-bookmarks");g_signal_connect(wb.marks_reload,"clicked",G_CALLBACK(bookmarks_project_reload),NULL);gtk_box_pack_start(GTK_BOX(row),wb.marks_scope,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),wb.marks_reload,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);gtk_widget_set_name(view,"insight-bookmarks");gtk_widget_set_name(field,"insight-bookmarks-query");wb.marks_entry=field; wb.marks_store=store; wb.marks_view=view; } else { wb.clips_entry=field; wb.clips_store=store; wb.clips_view=view; gtk_widget_set_name(view,"insight-clips"); gtk_widget_set_name(field,"insight-clips-query"); }
 gtk_widget_set_tooltip_text(field,"Prefix with ~ for fuzzy subsequence matching");gtk_entry_set_placeholder_text(GTK_ENTRY(field),kind==INSIGHT_BOOKMARK ? "Bookmark name or filter…" : "Clip name or filter…"); gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Name",gtk_cell_renderer_text_new(),"text",0,NULL));
 if(kind==INSIGHT_BOOKMARK) { gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),GTK_SELECTION_MULTIPLE);GtkCellRenderer *file=gtk_cell_renderer_text_new();GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes("File",file,NULL);gtk_tree_view_column_set_cell_data_func(column,file,bookmark_file_cell,NULL,NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Symbol",gtk_cell_renderer_text_new(),"text",4,NULL)); gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Line",gtk_cell_renderer_text_new(),"text",2,NULL)); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view),1); }
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),0,GTK_SORT_ASCENDING); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),view);
 gtk_box_pack_start(GTK_BOX(box),field,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),controls,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 const char *marks[]={"Add/update","Go to","Delete"},*clips[]={"Copy selection","Cut selection","Paste","Edit","Delete"}; guint count=kind==INSIGHT_BOOKMARK ? 3 : 5;
 for(guint i=0;i<count;i++) { const char *icons[]={"edit-copy","edit-cut","edit-paste","document-edit","edit-delete"}; GtkWidget *button=kind==INSIGHT_BOOKMARK ? gtk_button_new_with_label(marks[i]) : gtk_button_new_from_icon_name(icons[i],GTK_ICON_SIZE_SMALL_TOOLBAR); gtk_widget_set_tooltip_text(button,kind==INSIGHT_BOOKMARK ? marks[i] : clips[i]); gtk_box_pack_start(GTK_BOX(controls),button,FALSE,FALSE,0);
  GCallback callback; gpointer argument=NULL;
  if(kind==INSIGHT_BOOKMARK) { callback=i==0 ? G_CALLBACK(bookmark_add) : (i==1 ? G_CALLBACK(bookmark_go) : G_CALLBACK(note_delete)); if(i==2) argument=GINT_TO_POINTER(kind); }
  else { const gchar *ids[]={"insight-clip-copy","insight-clip-cut","insight-clip-paste","insight-clip-edit","insight-clip-delete"};gtk_widget_set_name(button,ids[i]);callback=i<2 ? G_CALLBACK(clip_capture) : (i==2 ? G_CALLBACK(clip_paste) : (i==3 ? G_CALLBACK(clip_edit) : G_CALLBACK(note_delete))); argument=i==1 ? GINT_TO_POINTER(1) : (i==4 ? GINT_TO_POINTER(kind) : NULL); }
  g_signal_connect(button,"clicked",callback,argument);
 }
 if(kind==INSIGHT_CLIP) { GtkWidget *create=gtk_button_new_from_icon_name("document-new",GTK_ICON_SIZE_SMALL_TOOLBAR),*insert=gtk_button_new(); gtk_button_set_image(GTK_BUTTON(insert),insight_command_icon("snippets")); gtk_widget_set_name(create,"insight-new-template"); gtk_widget_set_name(insert,"insight-insert-template"); gtk_widget_set_tooltip_text(create,"New code template"); gtk_widget_set_tooltip_text(insert,"Insert as code template — expand variables"); gtk_box_pack_start(GTK_BOX(controls),create,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(controls),insert,FALSE,FALSE,0); g_signal_connect(create,"clicked",G_CALLBACK(clip_new_template),NULL); g_signal_connect(insert,"clicked",G_CALLBACK(clip_template),NULL); }
 GtkWidget *transfer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
 GtkWidget *rename=gtk_button_new_with_label("Rename…"); gtk_box_pack_start(GTK_BOX(transfer),rename,FALSE,FALSE,0); g_signal_connect(rename,"clicked",G_CALLBACK(note_rename),GINT_TO_POINTER(kind));
 for(guint i=0;i<2;i++) { GtkWidget *button=gtk_button_new_with_label(i && kind==INSIGHT_BOOKMARK ? "Export selected…" : i ? "Export…" : "Import…"); gtk_box_pack_start(GTK_BOX(transfer),button,FALSE,FALSE,0); g_signal_connect(button,"clicked",G_CALLBACK(notes_transfer),GINT_TO_POINTER((gint)kind|(i ? 256 : 0))); }
 if(kind==INSIGHT_BOOKMARK) { GtkWidget *options=gtk_button_new_with_label("Bookmark Options…");gtk_widget_set_name(options,"insight-bookmark-options-button");gtk_widget_set_tooltip_text(options,"Bookmark Options (Ctrl+Q in Bookmarks)");gtk_widget_set_halign(options,GTK_ALIGN_START);gtk_box_pack_start(GTK_BOX(box),options,FALSE,FALSE,0);g_signal_connect(options,"clicked",G_CALLBACK(bookmark_options),NULL); }
 if(kind==INSIGHT_CLIP) { GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*reload=gtk_button_new_from_icon_name("view-refresh",GTK_ICON_SIZE_SMALL_TOOLBAR);wb.clips_scope=gtk_combo_box_text_new();gtk_widget_set_name(wb.clips_scope,"insight-clips-store");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.clips_scope),"Global templates");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.clips_scope),"Project templates");gtk_combo_box_set_active(GTK_COMBO_BOX(wb.clips_scope),0);gtk_widget_set_name(reload,"insight-reload-project-templates");gtk_widget_set_tooltip_text(reload,"Reload project templates");gtk_box_pack_start(GTK_BOX(row),wb.clips_scope,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),reload,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);g_signal_connect(wb.clips_scope,"changed",G_CALLBACK(clips_scope_changed),NULL);g_signal_connect(reload,"clicked",G_CALLBACK(clips_project_reload),NULL); }
 if(kind==INSIGHT_CLIP) { wb.clips_all=gtk_check_button_new_with_label("Show all languages"); gtk_widget_set_name(wb.clips_all,"insight-clips-all-languages"); gtk_box_pack_start(GTK_BOX(box),wb.clips_all,FALSE,FALSE,0); g_signal_connect(wb.clips_all,"toggled",G_CALLBACK(notes_filter),GINT_TO_POINTER(INSIGHT_CLIP)); }
 gtk_box_pack_start(GTK_BOX(box),transfer,FALSE,FALSE,0); gtk_box_reorder_child(GTK_BOX(box),transfer,2);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new(kind==INSIGHT_BOOKMARK ? "Bookmarks" : "Clips")); g_signal_connect(field,"changed",G_CALLBACK(notes_filter),GINT_TO_POINTER(kind)); g_signal_connect(field,"key-press-event",kind==INSIGHT_BOOKMARK ? G_CALLBACK(bookmark_key) : G_CALLBACK(browser_key),NULL); g_signal_connect(view,"key-press-event",kind==INSIGHT_BOOKMARK ? G_CALLBACK(bookmark_key) : G_CALLBACK(browser_key),NULL);
 if(kind==INSIGHT_BOOKMARK) { g_signal_connect(field,"activate",G_CALLBACK(bookmark_go),NULL);g_signal_connect(view,"popup-menu",G_CALLBACK(bookmark_popup_key),NULL);g_signal_connect(view,"button-press-event",G_CALLBACK(bookmark_popup_button),NULL); }
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),"changed",G_CALLBACK(note_selected),GINT_TO_POINTER(kind)); g_signal_connect(view,"row-activated",G_CALLBACK(note_activated),GINT_TO_POINTER(kind)); gtk_widget_show_all(box); notes_filter(NULL,GINT_TO_POINTER(kind));
}
static void create_notes(void) {
 wb.bookmark_handles=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
 wb.marks_session_stores=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,bookmark_session_store_free);
 wb.notes_path=g_build_filename(app->configdir,"insight","notes.conf",NULL); wb.notes=insight_notes_new();wb.project_notes=insight_notes_new();wb.project_marks=insight_notes_new();
 if(g_file_test(wb.notes_path,G_FILE_TEST_EXISTS)) { bookmark_session_remember(wb.notes_path,NULL,NULL);GError *error=NULL; InsightNotes *loaded=insight_notes_load(wb.notes_path,&error); if(loaded) { insight_notes_free(wb.notes); wb.notes=loaded; } else { g_hash_table_remove(wb.marks_session_stores,wb.notes_path);ui_set_statusbar(TRUE,"Notes ignored: %s",error ? error->message : "invalid snapshot"); } g_clear_error(&error); }
 create_note_panel(INSIGHT_BOOKMARK); create_note_panel(INSIGHT_CLIP);gchar *marks_root=app->project ? project_get_base_path() : NULL;bookmarks_project_load(app->project ? app->project->file_name : NULL,marks_root,TRUE);g_free(marks_root);clips_project_load(app->project ? app->project->file_name : NULL,FALSE);
 for(guint i=0;i<documents_array->len;i++) bookmark_restore_document(g_ptr_array_index(documents_array,i));
}
static void category_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)column; (void)data; GtkTreeIter iter; GtkTreeModel *model=gtk_tree_view_get_model(view);
 if(!gtk_tree_model_get_iter(model,&iter,path)) return;
 gulong line; gtk_tree_model_get(model,&iter,SYMBOL_LINE,&line,-1);
 if(!line) { if(gtk_tree_view_row_expanded(view,path)) gtk_tree_view_collapse_row(view,path); else gtk_tree_view_expand_row(view,path,FALSE); return; }
 browser_activated(view,path,NULL,NULL);
}
static gint category_language(const gchar *path) {
 GeanyDocument *doc=path && path[0]!='@' ? document_find_by_filename(path) : NULL;
 if(doc && doc->tm_file) return doc->tm_file->lang;
 GeanyFiletype *ft=insight_languages_detect(path); if(!ft) ft=filetypes_detect_from_extension(path); return ft ? ft->lang : -1;
}
static void category_append(GtkTreeIter *row,GtkTreeIter *parent,const InsightSymbol *symbol) {
 gtk_tree_store_append(wb.categories,row,parent); gtk_tree_store_set(wb.categories,row,SYMBOL_NAME,symbol->name,SYMBOL_SCOPE,symbol->scope,SYMBOL_PATH,symbol->path,SYMBOL_LINE,(gulong)symbol->line,SYMBOL_KIND,symbol->kind,-1);
}
static void categories_filter(GtkEditable *entry,gpointer data) {
 (void)entry; (void)data; GroupedPanel *p=&wb.category_panel; if(!p->store || !gtk_widget_get_mapped(p->view)) return;
 InsightLookupFilter *filter=grouped_filter(p); if(!filter) return; refresh_index();
 GHashTable *expanded=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL); gchar *selected=grouped_capture(p,expanded); gtk_tree_store_clear(p->store);
 gsize count=insight_index_count(wb.index); const InsightSymbol **symbols=g_new(const InsightSymbol *,count); insight_index_query(wb.index,"",FALSE,NULL,NULL,NULL,0,symbols,count);
 GHashTable *groups=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free),*owners=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); GPtrArray *loaded=g_ptr_array_new(); guint shown=0,total=0;
 GeanyDocument *active=document_get_current(); InsightLanguageFilter *language=insight_language_filter_new(active ? active->file_name : NULL,active ? active->file_type : NULL);
 for(gsize i=0;i<count;i++) {
  const InsightSymbol *symbol=symbols[i]; if(!insight_language_filter_match(language,symbol->path) || !insight_lookup_filter_match(filter,symbol->name,symbol->scope,symbol->path,symbol->kind)) continue;
  total++; if(shown>=p->limit) continue;
  guint type=insight_symbol_kind_type(symbol->path,symbol->kind,NULL); gint lang=category_language(symbol->path); gchar *key=g_strdup_printf("type:%u",type); GtkTreeIter *category=g_hash_table_lookup(groups,key);
  if(!category) { category=g_new(GtkTreeIter,1); gtk_tree_store_append(p->store,category,NULL); gchar *kind=g_strdup_printf("@type:%u",type); gtk_tree_store_set(p->store,category,SYMBOL_NAME,insight_symbol_type_name(type,TRUE),SYMBOL_KIND,kind,-1); g_free(kind); g_hash_table_insert(groups,g_strdup(key),category); } g_free(key);
  GtkTreeIter *parent=category; const gchar *separator=lang>=0 ? tm_parser_scope_separator_printable(lang) : "::"; if(!separator || !*separator) separator="::";
  if(*symbol->scope) {
   gchar **parts=g_strsplit(symbol->scope,separator,64); GString *scope=g_string_new(NULL);
   for(guint part=0;parts[part];part++) {
    if(!*parts[part]) continue;
    if(scope->len) g_string_append(scope,separator);
    g_string_append(scope,parts[part]);
    key=g_strdup_printf("scope:%u:%d:%s",type,lang,scope->str); GtkTreeIter *node=g_hash_table_lookup(groups,key);
    if(!node) { node=g_new(GtkTreeIter,1); gtk_tree_store_append(p->store,node,parent); gchar *kind=g_strdup_printf("@type:%u:%d",type,lang); gtk_tree_store_set(p->store,node,SYMBOL_NAME,parts[part],SYMBOL_SCOPE,scope->str,SYMBOL_KIND,kind,-1); g_free(kind); g_hash_table_insert(groups,g_strdup(key),node); }
    parent=node; g_free(key);
   } g_string_free(scope,TRUE); g_strfreev(parts);
  }
  GtkTreeIter row; category_append(&row,parent,symbol); g_ptr_array_add(loaded,(gpointer)symbol); shown++;
  if(type&(tm_tag_class_t|tm_tag_struct_t|tm_tag_union_t|tm_tag_interface_t)) {
   gchar *qualified=*symbol->scope ? g_strconcat(symbol->scope,separator,symbol->name,NULL) : g_strdup(symbol->name); key=g_strdup_printf("%d:%s",lang,qualified); g_free(qualified);
   if(g_hash_table_contains(owners,key)) { g_hash_table_replace(owners,key,NULL); } else { GtkTreeIter *owner=g_new(GtkTreeIter,1); *owner=row; g_hash_table_insert(owners,key,owner); }
  }
 }
 /* Supplemental direct members under unambiguous indexed type declarations.
  * Their canonical category rows remain available; this is scope-based, not
  * overload/type-alias resolution. Both passes obey the same query and limit. */
 for(guint i=0;i<loaded->len;i++) {
  const InsightSymbol *symbol=g_ptr_array_index(loaded,i); if(!*symbol->scope) continue;
  gchar *key=g_strdup_printf("%d:%s",category_language(symbol->path),symbol->scope); GtkTreeIter *owner=g_hash_table_lookup(owners,key); g_free(key);
  if(owner) { GtkTreeIter row; category_append(&row,owner,symbol); }
 }
 insight_language_filter_free(language); g_ptr_array_free(loaded,TRUE); g_hash_table_destroy(owners); g_hash_table_destroy(groups); insight_lookup_filter_free(filter); g_free(symbols);
 grouped_count(p,shown,total); gtk_widget_set_visible(p->more,shown<total && p->limit<250000); grouped_restore(p,expanded,selected);
}
static gboolean refresh_visible_project_panel(gpointer data) {
 (void)data; wb.panel_idle=0; if(!wb.alive) return G_SOURCE_REMOVE; categories_filter(NULL,NULL); tree_refresh(); file_types_refresh(); return G_SOURCE_REMOVE;
}
static void project_panel_changed(GtkNotebook *book,GtkWidget *page,guint number,gpointer data) {
 (void)book; (void)page; (void)number; (void)data; if(!wb.panel_idle) wb.panel_idle=g_idle_add(refresh_visible_project_panel,NULL);
}
static void create_categories(void) {
 GroupedPanel *p=&wb.category_panel; p->symbols=TRUE; GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3); wb.category_entry=p->entry=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(p->entry),"Find symbol category members…"); gtk_box_pack_start(GTK_BOX(box),p->entry,FALSE,FALSE,0);
 wb.categories=p->store=gtk_tree_store_new(SYMBOL_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING); wb.category_view=p->view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(p->store));
 const char *titles[]={"Symbol / category","Scope","File","Line","Kind"}; for(guint i=0;i<SYMBOL_COLUMNS;i++) { GtkTreeViewColumn *c=gtk_tree_view_column_new_with_attributes(titles[i],gtk_cell_renderer_text_new(),"text",i,NULL); gtk_tree_view_column_set_sort_column_id(c,i); gtk_tree_view_append_column(GTK_TREE_VIEW(p->view),c); }
 const gint widths[]={220,160,300,70,140}; insight_symbol_view_columns_configure(GTK_TREE_VIEW(p->view),widths,G_N_ELEMENTS(widths)); insight_symbol_tree_format(GTK_TREE_VIEW(p->view)); gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(p->store),SYMBOL_NAME,GTK_SORT_ASCENDING); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(p->view),SYMBOL_PATH);
 grouped_attach(p,box); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),p->view); gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0); gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new("Categories"));
 g_signal_connect(p->view,"row-activated",G_CALLBACK(category_activated),NULL); gtk_widget_show_all(box); categories_filter(NULL,NULL);
}
typedef struct { InsightIndex *index,*semantic,*project_calls;InsightSymbol compiler_hit;gboolean compiler_used; gchar *query,*preferred,*path,*text,*digest; const InsightSymbol *matches[256]; gsize count; gint chosen,position,anchor,encoding; guint document; gulong line; GHashTable *allowed_paths;InsightLanguageFilter *language;gboolean valid_word,nominal_calls,python; guint64 generation,epoch,index_version; } DefinitionJob;
static gchar *cursor_query(GeanyDocument *doc) {
 gint a=(gint)SSM(doc->editor->sci,SCI_GETSELECTIONSTART,0,0),b=(gint)SSM(doc->editor->sci,SCI_GETSELECTIONEND,0,0);
 if(b>a && b-a<=1024) { gchar *query=sci_get_selection_contents(doc->editor->sci); return g_strstrip(query); }
 gchar word[1025]={0}; editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word); return g_strdup(word);
}
static void stop_definition(void) {
 wb.definition_generation++; if(wb.definition_timer) { g_source_remove(wb.definition_timer); wb.definition_timer=0; }
 if(wb.definition_worker) { g_cancellable_cancel(wb.definition_worker); g_clear_object(&wb.definition_worker); }
}
static void definition_job_free(gpointer data) { DefinitionJob *j=data;insight_language_filter_free(j->language); retire_index(j->index);retire_index(j->semantic);retire_index(j->project_calls);g_hash_table_destroy(j->allowed_paths); g_free(j->query); g_free(j->preferred); g_free(j->path); g_free(j->text); g_free(j->digest); g_free(j); }
static void definition_compiler_choice(DefinitionJob *j) {
 if(!j->semantic || !j->preferred || !insight_clang_dependencies_fresh(j->semantic,j->preferred)) return;gchar *usr=insight_clang_symbol_at(j->semantic,j->preferred,j->position,j->line,j->query);if(!usr) return;const InsightSymbol *definition=NULL,*target=NULL;
 if(insight_index_query(j->semantic,usr,TRUE,NULL,"@clangdef",j->preferred,0,&definition,1)!=1 && insight_index_query(j->project_calls,usr,TRUE,NULL,"@clangdef",NULL,0,&definition,1)!=1) definition=NULL;
 insight_index_query(j->semantic,usr,TRUE,NULL,"@clangtarget",j->preferred,0,&target,1);g_free(usr);const gchar *path=definition ? definition->path:target ? target->scope:NULL;gulong line=definition ? definition->line:target ? target->line:0;if(!path || !*path || !line) return;
 const InsightSymbol *found[4096];gsize n=insight_index_find(j->index,j->query,TRUE,found,G_N_ELEMENTS(found));const InsightSymbol *hit=NULL;for(gsize i=0;i<MIN(n,G_N_ELEMENTS(found));i++) if(found[i]->line==line && !strcmp(found[i]->path,path)) {hit=found[i];break;}
 if(!hit) {j->compiler_hit=(InsightSymbol){j->query,path,"",target ? target->inheritance:"",line,definition && strchr(definition->type,'(') ? definition->type:"","",""};hit=&j->compiler_hit;}
 j->matches[0]=hit;j->count=1;j->chosen=0;j->compiler_used=TRUE;
}
static void definition_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; DefinitionJob *j=data;
 if(j->valid_word && *j->query && !g_cancellable_is_cancelled(cancel)) {
  j->count=insight_relation_query(j->index,j->query,INSIGHT_RELATION_DEFINITIONS,0,j->matches,G_N_ELEMENTS(j->matches)); if(j->count==SIZE_MAX) j->count=0; j->count=MIN(j->count,G_N_ELEMENTS(j->matches));
  gchar *scope=NULL,*mask=NULL;gboolean member=FALSE,uncertain_scope=FALSE,imported=FALSE;gchar *owner=NULL;
  if(j->python && j->text) {
   gsize length=strlen(j->text);mask=insight_source_code_mask(j->text,length,INSIGHT_CODE_PYTHON,NULL);InsightNativeSource text={j->text,mask,length,NULL};text.python=insight_native_python_bindings_new(j->index,j->preferred,&text);scope=insight_native_python_scope(j->index,j->preferred,&text,j->position);
   gsize imported_count=MIN(insight_native_python_import_query(j->index,j->preferred,scope,&text,j->position,j->query,relation_eligible,j->language,j->matches,G_N_ELEMENTS(j->matches),&imported),G_N_ELEMENTS(j->matches));if(imported) j->count=imported_count;
   if(!imported) owner=insight_native_python_owner(j->index,j->preferred,scope,j->line,&text,j->position,&member,relation_eligible,j->language);
   if(!member && !imported) { gchar *binding=insight_native_python_binding_scope(j->index,j->preferred,scope,j->query,&text);g_free(scope);scope=binding;uncertain_scope=scope==NULL; }
   insight_native_python_bindings_free(text.python);
  } else if(j->nominal_calls && j->text) {
   gsize length=strlen(j->text);mask=insight_source_code_mask(j->text,length,INSIGHT_CODE_C,NULL);InsightNativeSource text={j->text,mask,length,NULL};InsightNativeScopes *scopes=insight_native_scopes_new(j->index,j->preferred,&text);scope=insight_native_scopes_at(scopes,j->line,(gsize)j->position,&uncertain_scope);
   if(!uncertain_scope) { owner=insight_native_member_owner_source_filtered(j->index,j->preferred,scope,j->line,&text,j->position,&member,mask!=NULL,relation_eligible,j->language); }insight_native_scopes_free(scopes);
  } else { scope=insight_native_function_scope(j->index,j->preferred,j->line);owner=insight_native_member_owner_calls_filtered(j->index,j->preferred,scope,j->line,j->text,j->position,&member,j->nominal_calls,relation_eligible,j->language); }
  g_free(mask);if(uncertain_scope) j->count=0;gsize eligible=0;
  if(member) j->count=MIN(insight_member_query(j->index,owner,j->query,j->preferred,relation_eligible,j->language,j->matches,G_N_ELEMENTS(j->matches),NULL,NULL),G_N_ELEMENTS(j->matches));
  for(gsize n=0;n<j->count;n++) if((imported || g_hash_table_contains(j->allowed_paths,j->matches[n]->path)) && (j->python && !member && !imported ? insight_native_python_rank(j->index,j->matches[n],j->preferred,scope,j->line) : insight_native_definition_rank_at(j->matches[n],j->preferred,scope,j->line))>=0) j->matches[eligible++]=j->matches[n];
  j->count=eligible;
  for(gsize n=0;n<j->count;n++) if(n==0 || (j->python && !member && !imported ? insight_native_python_rank(j->index,j->matches[n],j->preferred,scope,j->line) : insight_native_definition_rank_at(j->matches[n],j->preferred,scope,j->line))>(j->python && !member && !imported ? insight_native_python_rank(j->index,j->matches[j->chosen],j->preferred,scope,j->line) : insight_native_definition_rank_at(j->matches[j->chosen],j->preferred,scope,j->line))) j->chosen=(gint)n;
  g_free(scope); g_free(owner);definition_compiler_choice(j);
  if(j->count) { const InsightSymbol *hit=j->matches[j->chosen]; j->path=g_strdup(hit->path); j->line=hit->line; }
 }
 if(!j->path) j->path=g_strdup(j->preferred);
 if(g_strcmp0(j->path,j->preferred)) { g_clear_pointer(&j->text,g_free); if(!g_cancellable_is_cancelled(cancel)) j->text=read_disk_source_full(j->path,TRUE,j->encoding,cancel); }
 if(j->text) j->digest=source_digest(j->text);
 g_task_return_boolean(task,TRUE);
}
static void definition_job_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); DefinitionJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 if(!wb.alive || j->epoch!=workbench_epoch || j->generation!=wb.definition_generation) return;
 g_clear_object(&wb.definition_worker); if(j->index_version!=wb.index_version) { queue_definition(TRUE); return; } GeanyDocument *current=document_get_current();
 if(!ok || !current || current->id!=j->document || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow)) || sci_get_current_position(current->editor->sci)!=j->position || SSM(current->editor->sci,SCI_GETANCHOR,0,0)!=j->anchor) return;
 if(j->compiler_used) {InsightClangResult *semantic=insight_clang_ui_result(current->editor->sci);if(!semantic || semantic->relations!=j->semantic) {queue_definition(TRUE);return;}}
 GeanyDocument *doc=j->path ? document_find_by_filename(j->path) : current;
 if(doc) { gint length=sci_get_length(doc->editor->sci); if(length>=0 && length<=2*1024*1024) { g_free(j->text); j->text=sci_get_contents(doc->editor->sci,length+1); g_free(j->digest); j->digest=source_digest(j->text); } }
 if(!j->text || !g_utf8_validate(j->text,-1,NULL)) { ui_set_statusbar(TRUE,"Definition source unavailable or exceeds 2 MiB"); return; }
 /* Do not empty/repopulate the visible combo when Follow finds the same candidates. */
 GPtrArray *labels=g_ptr_array_new_with_free_func(g_free);
 GtkTreeModel *model=gtk_combo_box_get_model(GTK_COMBO_BOX(wb.choices));
 gboolean same=gtk_tree_model_iter_n_children(model,NULL)==(gint)j->count;
 for(gsize n=0;n<j->count;n++) {
  const InsightSymbol *hit=j->matches[n];
  gchar *label=g_strdup_printf("%s %s%s • %s — %s:%lu",hit->type,hit->name,hit->signature,hit->scope,hit->path,hit->line);g_ptr_array_add(labels,label);
  if(same) { GtkTreeIter iter;gchar *old=NULL;gtk_tree_model_iter_nth_child(model,&iter,NULL,(gint)n);gtk_tree_model_get(model,&iter,0,&old,-1);same=!g_strcmp0(old,label);g_free(old); }
 }
 wb.populating=TRUE;
 if(!same) {
  g_ptr_array_set_size(wb.definitions,0);gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(wb.choices));
  for(gsize n=0;n<j->count;n++) { const InsightSymbol *hit=j->matches[n];Location *location=g_new0(Location,1);location->path=g_strdup(hit->path);location->line=hit->line;g_ptr_array_add(wb.definitions,location);gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.choices),g_ptr_array_index(labels,n)); }
 }
 gint chosen=j->count ? (gint)j->chosen : -1;
 if(gtk_combo_box_get_active(GTK_COMBO_BOX(wb.choices))!=chosen) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.choices),chosen);
 wb.populating=FALSE;g_ptr_array_free(labels,TRUE);
 GeanyFiletype *ft=doc ? doc->file_type : preview_filetype(j->path); render_source(j->text,ft,j->line,j->path);
 g_free(wb.source_path); wb.source_path=g_strdup(j->path); wb.source_id=doc ? doc->id : 0; wb.source_line=j->line<=(gulong)sci_get_line_count(wb.preview) ? j->line : 0; g_free(wb.source_digest); wb.source_digest=g_strdup(j->digest);preview_links();
 gchar *label=j->count ? g_strdup_printf("Context — %s:%lu",j->path ? j->path : "untitled",j->line) : g_strdup_printf("Context — no indexed definition: %s • %s:%lu",j->query,j->path ? j->path : "untitled",j->line); if(g_strcmp0(gtk_label_get_text(GTK_LABEL(wb.title)),label)) gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label); if(j->count) preview_mark_symbol(j->matches[j->chosen]->name); navigation_state();
 wb.follow_doc=j->document; wb.follow_line=(gulong)sci_get_current_line(current->editor->sci)+1; g_free(wb.follow_word); wb.follow_word=g_strdup(j->query);
 if(gtk_widget_has_focus(GTK_WIDGET(current->editor->sci)) && gtk_widget_get_parent(wb.box)==main_widgets.message_window_notebook) { reveal_context(); }
}
static gboolean definition_timeout(gpointer data) {
 (void)data; wb.definition_timer=0; if(!wb.alive || !wb.preview || !wb.context_visible || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow))) return G_SOURCE_REMOVE;
 GeanyDocument *doc=document_get_current(); if(!doc) return G_SOURCE_REMOVE; refresh_index();
 DefinitionJob *j=g_new0(DefinitionJob,1); j->index=insight_index_ref(wb.index); j->query=cursor_query(doc); j->preferred=g_strdup(doc->file_name); j->document=doc->id; j->position=sci_get_current_position(doc->editor->sci); j->anchor=(gint)SSM(doc->editor->sci,SCI_GETANCHOR,0,0); j->line=(gulong)sci_get_current_line(doc->editor->sci)+1; j->encoding=file_prefs.default_open_encoding; j->generation=wb.definition_generation; j->epoch=workbench_epoch; j->index_version=wb.index_version;
 InsightClangResult *semantic=insight_clang_ui_result(doc->editor->sci);if(insight_clang_result_usable(semantic)) {j->semantic=insight_index_ref(semantic->relations);j->project_calls=insight_index_ref(wb.disk_calls);}
 j->language=insight_language_filter_new(doc->file_name,doc->file_type);j->nominal_calls=member_calls_language(doc->file_type);j->python=python_language(doc->file_type);
 j->valid_word=!definition_keyword(doc->file_type,j->query) && !insight_languages_keyword(doc->editor->sci,j->query) && definition_code(doc->editor->sci,j->position);
 j->allowed_paths=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);const InsightSymbol *candidates[4096];gsize candidates_count=insight_index_find(j->index,j->query,TRUE,candidates,G_N_ELEMENTS(candidates));
 for(gsize n=0;n<MIN(candidates_count,G_N_ELEMENTS(candidates));n++) if(definition_language(doc->file_name,doc->file_type,candidates[n]->path)) g_hash_table_add(j->allowed_paths,g_strdup(candidates[n]->path));
 gint length=sci_get_length(doc->editor->sci); if(length>=0 && length<=2*1024*1024) { j->text=sci_get_contents(doc->editor->sci,length+1);if(!g_utf8_validate(j->text,length,NULL) || memchr(j->text,0,(gsize)length)) g_clear_pointer(&j->text,g_free); }
 if(!j->text) j->valid_word=FALSE;
 wb.definition_worker=g_cancellable_new(); GTask *task=g_task_new(NULL,wb.definition_worker,definition_job_done,NULL); g_task_set_task_data(task,j,definition_job_free); g_task_run_in_thread(task,definition_job_run); g_object_unref(task); return G_SOURCE_REMOVE;
}
