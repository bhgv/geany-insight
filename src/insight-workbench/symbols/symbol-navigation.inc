/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Symbol location, queries and navigation.
 * RU: Поиск расположения символов, запросы и навигация. */

static gboolean symbol_locate_row(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 SymbolLocate *target=data;gchar *name=NULL;gulong line=0;gtk_tree_model_get(model,iter,SYMBOL_NAME,&name,SYMBOL_LINE,&line,-1);
 gboolean match=line==target->tag->line && !g_strcmp0(name,target->tag->name);g_free(name);if(match) target->path=gtk_tree_path_copy(path);return match;
}
static void symbols_locate(GtkWidget *button,gpointer data) {
 (void)button;(void)data;GeanyDocument *doc=document_get_current();
 if(!doc || !wb.local_view || !gtk_widget_get_sensitive(wb.local_view) || doc->id!=wb.local_doc_id) { ui_set_statusbar(TRUE,"File symbols are still updating");return; }
 TMTag *tag=relation_enclosing_function(doc);
 if(!tag && doc->tm_file && doc->tm_file->tags_array) {
  gulong line=(gulong)sci_get_current_line(doc->editor->sci)+1;
  for(guint i=0;i<doc->tm_file->tags_array->len;i++) { TMTag *candidate=g_ptr_array_index(doc->tm_file->tags_array,i);if(candidate && candidate->line && candidate->line<=line && (!tag || candidate->line>tag->line)) tag=candidate; }
 }
 if(!tag) { ui_set_statusbar(TRUE,"No indexed declaration at or before the caret");return; }
 SymbolLocate target={.tag=tag};gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(wb.local_view)),symbol_locate_row,&target);
 if(!target.path) { ui_set_statusbar(TRUE,"The caret symbol is outside the loaded outline or hidden by a filter");return; }
 GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view));guint signal=g_signal_lookup("changed",GTK_TYPE_TREE_SELECTION);
 g_signal_handlers_block_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL);
 gtk_tree_view_expand_to_path(GTK_TREE_VIEW(wb.local_view),target.path);gtk_tree_view_set_cursor(GTK_TREE_VIEW(wb.local_view),target.path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(wb.local_view),target.path,NULL,TRUE,0.5,0);
 g_signal_handlers_unblock_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL);gtk_tree_path_free(target.path);
}
static void symbols_load_more(GtkWidget *button,gpointer data) {
 (void)button;GtkWidget *view=data;if(!gtk_widget_get_sensitive(view)) return;
 if(view==wb.local_view) { wb.local_symbol_limit=MIN(250000u,wb.local_symbol_limit+2000u);local_filter(NULL,NULL); }
 else if(view==wb.symbol_view) { wb.project_symbol_limit=MIN(250000u,wb.project_symbol_limit+500u);browser_filter(NULL,NULL); }
}
static void symbol_action(GtkTreeView *view,guint action) {
 if(action!=SP_HELP && !gtk_widget_get_sensitive(GTK_WIDGET(view))) return;
 GtkTreeModel *model=gtk_tree_view_get_model(view); GtkTreeIter iter;
 gboolean selected=gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),NULL,&iter);
 gchar *name=NULL,*path=NULL,*scope=NULL; gulong line=0;
 if(selected) gtk_tree_model_get(model,&iter,SYMBOL_NAME,&name,SYMBOL_PATH,&path,SYMBOL_SCOPE,&scope,SYMBOL_LINE,&line,-1);
 switch(action) {
 case SP_COPY_TEXT: case SP_CUT_TEXT: case SP_DUPLICATE: case SP_PASTE_TEXT: symbol_text_from_outline(view,action-SP_COPY_TEXT);break;
 case SP_OPEN: if(selected) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter); browser_activated(view,row,NULL,NULL); gtk_tree_path_free(row); } break;
 case SP_INFO: if(line) browser_symbol_info(NULL,view); break;
 case SP_REFERENCES:
  if(line && name) { wb.relation_setting=TRUE; gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_buffer),FALSE); gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),0); gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),name); gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry),""); wb.relation_setting=FALSE; relation_controls(NULL,NULL); focus_widget_panel(wb.usage_entry); } break;
 case SP_QUALIFIED:
  if(line && name) { gchar *native=path && path[0]!='@' ? utils_get_locale_from_utf8(path) : NULL;GeanyDocument *owner=native ? document_find_by_filename(native) : document_get_current();g_free(native);GeanyFiletype *ft=owner ? owner->file_type : (path ? filetypes_detect_from_extension(path) : NULL);const gchar *separator=owner && owner->tm_file ? tm_parser_scope_separator_printable(owner->tm_file->lang) : (ft ? tm_parser_scope_separator_printable(ft->lang) : NULL);gchar *qualified=scope && *scope ? g_strconcat(scope,separator && *separator ? separator : "::",name,NULL) : g_strdup(name);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),qualified,-1);g_free(qualified); }break;
 case SP_PATH: if(line && path && path[0]!='@') gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),path,-1);break;
 case SP_LINE: if(line) { gchar *number=g_strdup_printf("%lu",line);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),number,-1);g_free(number); }break;
 case SP_FILTER_NAME: case SP_FILTER_SCOPE: case SP_FILTER_FILE:
  if(line) { const gchar *value=action==SP_FILTER_NAME ? name : (action==SP_FILTER_SCOPE ? scope : path);if(value && (action!=SP_FILTER_FILE || value[0]!='@')) { gchar *query=g_strdup_printf("%s\"%s\"",action==SP_FILTER_SCOPE ? "scope:" : (action==SP_FILTER_FILE ? "file:" : ""),value);if(strlen(query)<=1024) { GtkWidget *entry=GTK_WIDGET(view)==wb.local_view ? wb.local_entry : wb.symbol_entry;gtk_entry_set_text(GTK_ENTRY(entry),query);gtk_widget_grab_focus(entry); }else ui_set_statusbar(TRUE,"Selected value is too long for a lookup filter");g_free(query); } }break;
 case SP_CLEAR: { GtkWidget *entry=GTK_WIDGET(view)==wb.local_view ? wb.local_entry : wb.symbol_entry;gtk_entry_set_text(GTK_ENTRY(entry),"");gtk_widget_grab_focus(entry); }break;
 case SP_EXPAND_ONE: case SP_COLLAPSE_ONE:
  if(selected && insight_symbol_view_grouped(view)) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter);while(gtk_tree_path_get_depth(row)>1) gtk_tree_path_up(row);if(action==SP_EXPAND_ONE) gtk_tree_view_expand_row(view,row,TRUE);else gtk_tree_view_collapse_row(view,row);gtk_tree_path_free(row); }break;
 case SP_INVERT: insight_symbol_view_set_types(view,~insight_symbol_view_types_mask(view));break;
 case SP_NAME: if(name) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),name,-1); break;
 case SP_LOCATION: if(line && path && path[0]!='@') { gchar *location=g_strdup_printf("%s:%lu",path,line); gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),location,-1); g_free(location); } break;
 case SP_ONLY: case SP_HIDE: insight_symbol_view_filter_selected(view,action==SP_ONLY); break;
 case SP_ALL: insight_symbol_view_set_types(view,G_MAXUINT); break;
 case SP_EXPAND: gtk_tree_view_expand_all(view); break;
 case SP_COLLAPSE: gtk_tree_view_collapse_all(view); break;
 case SP_MORE: insight_symbol_view_load_more(view); break;
 case SP_COLUMNS: insight_symbol_view_columns_reset(view);break;
 case SP_LOCATE: if(GTK_WIDGET(view)==wb.local_view) symbols_locate(NULL,NULL); break;
 case SP_HELP: insight_help_show(NULL,GTK_WIDGET(view)==wb.local_view ? "panel-file-symbols.md" : "panel-project-symbols.md"); break;
 default: break;
 }
 g_free(name); g_free(path);g_free(scope);
}
static void symbol_menu_action(GtkMenuItem *item,gpointer data) { guint action=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"insight-symbol-action")); if(action==SP_GROUP) insight_symbol_view_set_grouped(GTK_TREE_VIEW(data),gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))); else symbol_action(GTK_TREE_VIEW(data),action); }
static GtkWidget *symbol_menu(GtkTreeView *view) {
 GtkWidget *menu=g_object_get_data(G_OBJECT(view),"insight-symbol-popup");
 if(!menu) {
  menu=gtk_menu_new(); gtk_widget_set_name(menu,"insight-symbol-popup"); gtk_menu_attach_to_widget(GTK_MENU(menu),GTK_WIDGET(view),NULL); g_object_set_data_full(G_OBJECT(view),"insight-symbol-popup",g_object_ref_sink(menu),g_object_unref);
  const gchar *labels[]={"Open definition","Symbol Info…","Find references to this symbol","Copy symbol name","Copy source location","Show only this category","Hide this category","Show all symbol types","Group by symbol type","Expand all symbol groups","Collapse all symbol groups","Help for this panel","Load more symbols","Locate symbol at caret","Reset symbol columns","Copy qualified symbol name","Copy source file path","Copy source line number","Filter by this symbol name","Filter by this scope","Filter by this source file","Clear symbol query","Expand selected symbol group","Collapse selected symbol group","Invert symbol type selection","Visible symbol columns","Copy Symbol","Cut Symbol","Duplicate Symbol","Paste Symbol…"};
  const gchar *icons[]={"go-jump","dialog-information","relations","edit-copy","edit-copy","view-filter-symbolic","view-filter-symbolic","view-list-symbolic","view-list-symbolic","list-add","list-remove","panel-help","list-add-symbolic","find-location-symbolic","view-refresh","edit-copy","edit-copy","edit-copy","edit-find","view-filter-symbolic","view-filter-symbolic","edit-clear","list-add","list-remove","object-flip-horizontal","view-list-symbolic","edit-copy","edit-cut","edit-copy","edit-paste"};
  for(guint i=0;i<SP_COUNT;i++) {
   if(i==SP_ONLY || i==SP_GROUP || i==SP_HELP) gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
   GtkWidget *item;
   if(i==SP_GROUP) { item=gtk_check_menu_item_new_with_label(labels[i]); GtkWidget *label=gtk_bin_get_child(GTK_BIN(item)),*box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); g_object_ref(label); gtk_container_remove(GTK_CONTAINER(item),label); gtk_box_pack_start(GTK_BOX(box),insight_command_icon(icons[i]),FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),label,TRUE,TRUE,0); g_object_unref(label); gtk_container_add(GTK_CONTAINER(item),box); g_object_set_data(G_OBJECT(item),"insight-icon-added",GINT_TO_POINTER(1)); }
   else if(i==SP_VISIBILITY) { item=gtk_image_menu_item_new_with_label(labels[i]);gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),insight_command_icon(icons[i]));gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE);gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),insight_symbol_view_columns_menu(view)); }
   else { item=gtk_image_menu_item_new_with_label(labels[i]); gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),insight_command_icon(icons[i])); gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE); }
   gchar *key=g_strdup_printf("insight-symbol-command-%u",i); gtk_widget_set_name(item,key); g_object_set_data(G_OBJECT(menu),key,item); g_free(key); g_object_set_data(G_OBJECT(item),"insight-symbol-action",GUINT_TO_POINTER(i)); g_signal_connect_object(item,i==SP_GROUP ? "toggled" : "activate",G_CALLBACK(symbol_menu_action),view,0); if(i>=SP_MORE) { GList *children=gtk_container_get_children(GTK_CONTAINER(menu));gint position=(gint)g_list_length(children)-2;g_list_free(children);gtk_menu_shell_insert(GTK_MENU_SHELL(menu),item,position); }else gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
  }
 }
 insight_list_tools_menu(view,menu);
 GtkTreeModel *model=gtk_tree_view_get_model(view); GtkTreeIter iter; gboolean selected=gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),NULL,&iter); gchar *path=NULL;gulong line=0;
 if(selected) gtk_tree_model_get(model,&iter,SYMBOL_PATH,&path,SYMBOL_LINE,&line,-1);
 gboolean ready=gtk_widget_get_sensitive(GTK_WIDGET(view)),grouped=insight_symbol_view_grouped(view);
 for(guint i=0;i<SP_COUNT;i++) { gchar *key=g_strdup_printf("insight-symbol-command-%u",i); GtkWidget *item=g_object_get_data(G_OBJECT(menu),key);g_free(key); gboolean enabled=ready;
  if(i==SP_OPEN || i==SP_INFO || i==SP_REFERENCES) enabled &= line!=0;
  if(i==SP_NAME || i==SP_ONLY || i==SP_HIDE) enabled &= selected;
  if(i==SP_LOCATION || i==SP_PATH || i==SP_FILTER_FILE) enabled &= line && path && path[0]!='@';
  if(i==SP_QUALIFIED || i==SP_LINE || i==SP_FILTER_NAME || i==SP_FILTER_SCOPE) enabled &= line!=0;
  if(i==SP_EXPAND_ONE || i==SP_COLLAPSE_ONE) enabled &= grouped && selected;
  if(i==SP_EXPAND || i==SP_COLLAPSE) enabled &= grouped;
  if(i==SP_MORE) enabled &= insight_symbol_view_has_more(view);
  if(i==SP_LOCATE) { enabled &= GTK_WIDGET(view)==wb.local_view;gtk_widget_set_no_show_all(item,GTK_WIDGET(view)!=wb.local_view);gtk_widget_set_visible(item,GTK_WIDGET(view)==wb.local_view); }
  if(i>=SP_COPY_TEXT) { gboolean local=GTK_WIDGET(view)==wb.local_view;gtk_widget_set_no_show_all(item,!local);gtk_widget_set_visible(item,local);enabled &= local && line && document_get_current() && document_get_current()->id==wb.local_doc_id;if(i!=SP_COPY_TEXT && document_get_current()) enabled &= !document_get_current()->readonly && !SSM(document_get_current()->editor->sci,SCI_GETREADONLY,0,0); }
  if(i==SP_HELP) enabled=TRUE;
  gtk_widget_set_sensitive(item,enabled); if(i==SP_GROUP) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),grouped);
 }
 g_free(path); insight_i18n_apply(menu); gtk_widget_show_all(menu); return menu;
}
static gboolean symbol_popup_key(GtkWidget *widget,gpointer data) { (void)data;GtkWidget *menu=symbol_menu(GTK_TREE_VIEW(widget));GdkEvent *event=gtk_get_current_event();gtk_menu_popup_at_widget(GTK_MENU(menu),widget,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,event);if(event) gdk_event_free(event); return TRUE; }
static gboolean symbol_popup_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data; if(event->button!=3 || event->type!=GDK_BUTTON_PRESS) return FALSE;
 GtkTreeView *view=GTK_TREE_VIEW(widget);GtkTreeSelection *selection=gtk_tree_view_get_selection(view); GtkTreePath *path=NULL; guint signal=g_signal_lookup("changed",GTK_TYPE_TREE_SELECTION);g_signal_handlers_block_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL);
 if(gtk_tree_view_get_path_at_pos(view,(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)) { gtk_tree_view_set_cursor(view,path,NULL,FALSE);gtk_tree_path_free(path); }else gtk_tree_selection_unselect_all(selection);
 g_signal_handlers_unblock_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL); gtk_widget_grab_focus(widget); gtk_menu_popup_at_pointer(GTK_MENU(symbol_menu(view)),(GdkEvent*)event); return TRUE;
}
static gboolean symbol_key_global(GObject *object,GdkEventKey *event,gpointer data) { (void)object; (void)data;if(!wb.alive) return FALSE;if(insight_workbench_source_link_key(event)) return TRUE;GtkWidget *relation_view=wb.usage_view && gtk_widget_has_focus(wb.usage_view) ? wb.usage_view : (wb.relation_tree && gtk_widget_has_focus(GTK_WIDGET(insight_relation_tree_view(wb.relation_tree))) ? GTK_WIDGET(insight_relation_tree_view(wb.relation_tree)) : NULL);if(relation_view && relation_node_key(relation_view,event,NULL)) return TRUE;if(wb.marks_view && gtk_widget_has_focus(wb.marks_view)) return bookmark_key(wb.marks_view,event,NULL);if(wb.marks_entry && gtk_widget_has_focus(wb.marks_entry)) return bookmark_key(wb.marks_entry,event,NULL);if(wb.tree_view && gtk_widget_has_focus(wb.tree_view)) return folder_key(wb.tree_view,event,NULL);if(wb.tree_entry && gtk_widget_has_focus(wb.tree_entry)) return folder_key(wb.tree_entry,event,NULL);GroupedPanel *panels[]={&wb.type_panel,&wb.category_panel};for(guint i=0;i<G_N_ELEMENTS(panels);i++) { GroupedPanel *p=panels[i];if(p->view && gtk_widget_has_focus(p->view)) return grouped_key_press(p->view,event,p);if(p->entry && gtk_widget_has_focus(p->entry)) return grouped_key_press(p->entry,event,p); }GtkWidget *view=wb.local_view && gtk_widget_has_focus(wb.local_view) ? wb.local_view : (wb.symbol_view && gtk_widget_has_focus(wb.symbol_view) ? wb.symbol_view : (wb.file_view && gtk_widget_has_focus(wb.file_view) ? wb.file_view : NULL));return view ? browser_key(view,event,NULL) : FALSE; }
static void symbol_context_attach(GtkWidget *view) { g_signal_connect_after(view,"button-release-event",G_CALLBACK(browser_symbol_clicked),NULL); g_signal_connect(view,"button-press-event",G_CALLBACK(symbol_popup_button),NULL); g_signal_connect(view,"popup-menu",G_CALLBACK(symbol_popup_key),NULL); }
static void create_browser(void) {
 wb.browser=gtk_box_new(GTK_ORIENTATION_VERTICAL,4); wb.symbol_entry=gtk_search_entry_new(); gtk_widget_set_name(wb.symbol_entry,"insight-symbol-query");
 gtk_entry_set_placeholder_text(GTK_ENTRY(wb.symbol_entry),"Find project symbol…");
 wb.symbols=gtk_list_store_new(SYMBOL_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING);
 wb.symbol_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.symbols)); gtk_widget_set_name(wb.symbol_view,"insight-project-symbols");g_object_set_data(G_OBJECT(wb.symbol_view),"insight-bold-symbol-names",GINT_TO_POINTER(1));
 const char *titles[]={"Symbol","Scope","Project-relative file","Line","Kind"};
 for(int i=0;i<SYMBOL_COLUMNS;i++) {
  GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(titles[i],gtk_cell_renderer_text_new(),"text",i,NULL);
  gtk_tree_view_column_set_sort_column_id(column,i); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.symbol_view),column);
 }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.symbol_view);
 scroll=insight_virtual_list_wrap(scroll,GTK_TREE_VIEW(wb.symbol_view),wb.symbols,virtual_reload,NULL);
 wb.outline=gtk_check_button_new_with_label("Current file outline");
 gtk_widget_set_no_show_all(wb.outline,TRUE);
 g_signal_connect(wb.outline,"toggled",G_CALLBACK(outline_changed),NULL);
 gtk_box_pack_start(GTK_BOX(wb.browser),wb.outline,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(wb.browser),symbol_query_controls(wb.symbol_entry,FALSE),FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(wb.browser),scroll,TRUE,TRUE,0);
 gtk_box_pack_start(GTK_BOX(wb.browser),sort_selector(wb.symbols),FALSE,FALSE,0);
 GtkWidget *group=insight_symbol_view_attach(GTK_TREE_VIEW(wb.symbol_view),wb.symbols,NULL); gtk_widget_set_name(group,"insight-project-group-types"); gtk_box_pack_start(GTK_BOX(wb.browser),group,FALSE,FALSE,0);
 GtkWidget *scopes=insight_symbol_view_scopes_control(GTK_TREE_VIEW(wb.symbol_view));gtk_widget_set_name(scopes,"insight-project-scope-hierarchy");gtk_box_pack_start(GTK_BOX(wb.browser),scopes,FALSE,FALSE,0);
 GtkWidget *types=insight_symbol_view_types_control(GTK_TREE_VIEW(wb.symbol_view)); gtk_widget_set_name(types,"insight-project-type-filters"); gtk_box_pack_start(GTK_BOX(wb.browser),types,FALSE,FALSE,0); insight_symbol_view_more_loader(GTK_TREE_VIEW(wb.symbol_view),G_CALLBACK(symbols_load_more),500);
 GtkWidget *info=gtk_button_new_with_label("Symbol Info…");g_signal_connect(info,"clicked",G_CALLBACK(browser_symbol_info),wb.symbol_view);gtk_box_pack_start(GTK_BOX(wb.browser),info,FALSE,FALSE,0);
 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),wb.browser,gtk_label_new("Project Symbols"));
 g_signal_connect(wb.symbol_entry,"changed",G_CALLBACK(browser_filter),NULL);
 g_signal_connect(wb.symbol_entry,"activate",G_CALLBACK(browser_entry_activate),NULL);
 g_signal_connect(wb.symbol_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(wb.symbol_view,"key-press-event",G_CALLBACK(browser_key),NULL); symbol_context_attach(wb.symbol_view);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.symbol_view)),"changed",G_CALLBACK(browser_selected),NULL);
 g_signal_connect(wb.symbol_view,"row-activated",G_CALLBACK(browser_activated),NULL);
 gtk_widget_show_all(wb.browser); browser_filter(NULL,NULL);
}
static void local_structure_query(GtkEditable *entry,gpointer data) {
 (void)data;if(wb.local_mode && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode))==0 && g_strcmp0(gtk_entry_get_text(GTK_ENTRY(entry)),gtk_entry_get_text(GTK_ENTRY(wb.local_entry)))) gtk_entry_set_text(GTK_ENTRY(wb.local_entry),gtk_entry_get_text(GTK_ENTRY(entry)));
}
static void local_mode_changed(GtkComboBox *combo,gpointer data) {
 (void)data;gboolean details=gtk_combo_box_get_active(combo)==1;gtk_stack_set_visible_child_name(GTK_STACK(wb.local_stack),details ? "details" : "structure");local_filter(GTK_EDITABLE(wb.local_entry),NULL);
}
static void local_native_sort(GtkButton *button,gpointer data) {
 (void)button;GeanyDocument *doc=document_get_current();if(doc) symbols_recreate_tag_list(doc,GPOINTER_TO_INT(data));
}
static void local_filter(GtkEditable *entry,gpointer data) {
 (void)data; if(!wb.local_symbols) return; GeanyDocument *doc=document_get_current();
 if(wb.native_symbol_query && wb.local_mode && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode))==0 && g_strcmp0(gtk_entry_get_text(GTK_ENTRY(wb.local_entry)),gtk_entry_get_text(GTK_ENTRY(wb.native_symbol_query)))) gtk_entry_set_text(GTK_ENTRY(wb.native_symbol_query),gtk_entry_get_text(GTK_ENTRY(wb.local_entry)));
 if(entry || !wb.local_symbol_limit || wb.local_doc_id!=(doc ? doc->id : 0)) wb.local_symbol_limit=2000;
 wb.local_doc_id=doc ? doc->id : 0;
 gchar *file_label=doc && doc->file_name ? g_path_get_basename(doc->file_name) : g_strdup("Current file symbols");gtk_label_set_text(GTK_LABEL(wb.local_label),file_label);gtk_widget_set_tooltip_text(wb.local_label,doc ? doc->file_name : NULL);g_free(file_label);
 insight_symbol_view_filetype(GTK_TREE_VIEW(wb.local_view),doc ? doc->file_type : NULL);
 SymbolListJob *j=symbol_list_begin(wb.local_symbols,wb.local_entry,wb.local_symbol_limit,FALSE); if(!j) return; j->local=TRUE;j->lang=doc && doc->tm_file ? doc->tm_file->lang : -1; j->path=g_strdup(doc ? doc->file_name : NULL); j->tags=g_ptr_array_new_with_free_func((GDestroyNotify)tm_tag_unref);
 /* Tags are immutable after publication and their reference count is atomic.
  * Capture references, not GTK or a mutable TMSourceFile, for the worker. */
 if(doc && doc->tm_file && doc->tm_file->tags_array) for(guint n=0;n<doc->tm_file->tags_array->len;n++) { TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,n); if(tag) g_ptr_array_add(j->tags,tm_tag_ref(tag)); }
 symbol_list_launch(wb.local_symbols,j);
}
typedef struct { GtkTreeSortable *model; GtkWidget *field,*direction; gboolean syncing;const gint *fields;guint count; } SymbolSort;
static const gint file_sort_fields[]={0,1,2,3};
static const gint symbol_sort_fields[]={SYMBOL_NAME,SYMBOL_LINE,SYMBOL_KIND,SYMBOL_SCOPE,SYMBOL_PATH};
static void symbol_sort_sync(GtkTreeSortable *model,gpointer data) {
 SymbolSort *s=g_object_get_data(G_OBJECT(data),"insight-symbol-sort-state");gint field=-1;GtkSortType direction;
 if(!s || !gtk_tree_sortable_get_sort_column_id(model,&field,&direction)) return;
 s->syncing=TRUE;
 for(guint i=0;i<s->count;i++) if(s->fields[i]==field) gtk_combo_box_set_active(GTK_COMBO_BOX(s->field),i);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->direction),direction==GTK_SORT_DESCENDING);
 gtk_button_set_image(GTK_BUTTON(s->direction),gtk_image_new_from_icon_name(direction==GTK_SORT_DESCENDING ? "view-sort-descending" : "view-sort-ascending",GTK_ICON_SIZE_MENU));
 s->syncing=FALSE;
}
static void symbol_sort_changed(GtkWidget *widget,gpointer data) {
 (void)widget;SymbolSort *s=data;if(s->syncing) return;gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(s->field));
 if(mode>=0 && mode<(gint)s->count) gtk_tree_sortable_set_sort_column_id(s->model,s->fields[mode],gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->direction)) ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}
static GtkWidget *sort_selector(GtkListStore *model) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);SymbolSort *s=g_new0(SymbolSort,1);s->model=GTK_TREE_SORTABLE(model);s->fields=model==wb.file_store ? file_sort_fields : symbol_sort_fields;s->count=model==wb.file_store ? G_N_ELEMENTS(file_sort_fields) : G_N_ELEMENTS(symbol_sort_fields);s->field=gtk_combo_box_text_new();s->direction=gtk_toggle_button_new();
 gtk_widget_set_name(box,model==wb.file_store ? "insight-files-sort" : (model==wb.local_symbols ? "insight-local-sort" : "insight-project-sort"));
 gtk_widget_set_name(s->field,"insight-symbol-sort-field");gtk_widget_set_name(s->direction,"insight-symbol-sort-descending");
 const char *labels[]={"Sort: name","Sort: source line","Sort: symbol type","Sort: scope","Sort: file path"};
 const gchar *file_labels[]={"Sort: file name","Sort: full path","Sort: directory","Sort: size"};for(guint i=0;i<s->count;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->field),model==wb.file_store ? file_labels[i] : labels[i]);
 gtk_widget_set_tooltip_text(s->direction,"Descending order");gtk_button_set_relief(GTK_BUTTON(s->direction),GTK_RELIEF_NONE);
 gtk_box_pack_start(GTK_BOX(box),s->field,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(box),s->direction,FALSE,FALSE,0);
 g_object_set_data_full(G_OBJECT(box),"insight-symbol-sort-state",s,g_free);
 g_signal_connect(s->field,"changed",G_CALLBACK(symbol_sort_changed),s);g_signal_connect(s->direction,"toggled",G_CALLBACK(symbol_sort_changed),s);
 g_signal_connect_object(model,"sort-column-changed",G_CALLBACK(symbol_sort_sync),box,0);
 gtk_combo_box_set_active(GTK_COMBO_BOX(s->field),0);compact_panel_control(s->field);return box;
}
static void symbol_sort_write(GKeyFile *settings,GtkListStore *model,const gchar *prefix) {
 gint column=-1;GtkSortType direction;if(!gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(model),&column,&direction)) return;
 gchar *key=g_strconcat(prefix,"-sort-column",NULL);g_key_file_set_integer(settings,"workbench",key,column);g_free(key);
 key=g_strconcat(prefix,"-sort-descending",NULL);g_key_file_set_boolean(settings,"workbench",key,direction==GTK_SORT_DESCENDING);g_free(key);
}
static void symbol_sort_read(GKeyFile *settings,GtkListStore *model,const gchar *prefix) {
 gchar *key=g_strconcat(prefix,"-sort-column",NULL);GError *error=NULL;gint column=g_key_file_get_integer(settings,"workbench",key,&error);g_free(key);
 if(error || column<0 || column>=gtk_tree_model_get_n_columns(GTK_TREE_MODEL(model))) { g_clear_error(&error);return; }
 key=g_strconcat(prefix,"-sort-descending",NULL);gboolean descending=g_key_file_get_boolean(settings,"workbench",key,NULL);g_free(key);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model),column,descending ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}
static void create_local_browser(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3); wb.local_label=gtk_label_new("Current file symbols");
 gtk_label_set_ellipsize(GTK_LABEL(wb.local_label),PANGO_ELLIPSIZE_MIDDLE);PangoAttrList *title_style=pango_attr_list_new();pango_attr_list_insert(title_style,pango_attr_weight_new(PANGO_WEIGHT_BOLD));gtk_label_set_attributes(GTK_LABEL(wb.local_label),title_style);pango_attr_list_unref(title_style);
 wb.local_entry=gtk_search_entry_new(); gtk_widget_set_name(wb.local_entry,"insight-local-query"); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.local_entry),"Symbol Name…");
 wb.local_symbols=gtk_list_store_new(SYMBOL_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING);
 wb.local_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.local_symbols)); gtk_widget_set_name(wb.local_view,"insight-local-symbols");g_object_set_data(G_OBJECT(wb.local_view),"insight-source-nesting",GINT_TO_POINTER(1)); g_object_set_data(G_OBJECT(wb.local_view),"insight-bold-symbol-names",GINT_TO_POINTER(1)); gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view)),GTK_SELECTION_BROWSE);
 gint columns[]={SYMBOL_NAME,SYMBOL_LINE,SYMBOL_KIND}; const char *labels[]={"Symbol","Line","Type"};
 for(guint i=0;i<3;i++) { GtkTreeViewColumn *c=gtk_tree_view_column_new_with_attributes(labels[i],gtk_cell_renderer_text_new(),"text",columns[i],NULL); gtk_tree_view_column_set_sort_column_id(c,columns[i]); gtk_tree_view_append_column(GTK_TREE_VIEW(wb.local_view),c); }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.local_view);
 scroll=insight_virtual_list_wrap(scroll,GTK_TREE_VIEW(wb.local_view),wb.local_symbols,virtual_reload,NULL);
 gtk_box_pack_start(GTK_BOX(box),wb.local_label,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),symbol_query_controls(wb.local_entry,TRUE),FALSE,FALSE,0);
 GtkWidget *details=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_box_pack_start(GTK_BOX(details),scroll,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(details),sort_selector(wb.local_symbols),FALSE,FALSE,0);
 GtkWidget *group=insight_symbol_view_attach(GTK_TREE_VIEW(wb.local_view),wb.local_symbols,NULL); gtk_widget_set_name(group,"insight-local-group-types");compact_panel_control(group); gtk_box_pack_start(GTK_BOX(details),group,FALSE,FALSE,0);
 GtkWidget *scopes=insight_symbol_view_scopes_control(GTK_TREE_VIEW(wb.local_view));gtk_widget_set_name(scopes,"insight-local-scope-hierarchy");compact_panel_control(scopes);gtk_box_pack_start(GTK_BOX(details),scopes,FALSE,FALSE,0);
 GtkWidget *types=insight_symbol_view_types_control(GTK_TREE_VIEW(wb.local_view)); gtk_widget_set_name(types,"insight-local-type-filters"); gtk_box_pack_start(GTK_BOX(details),types,FALSE,FALSE,0); insight_symbol_view_more_loader(GTK_TREE_VIEW(wb.local_view),G_CALLBACK(symbols_load_more),2000);
 GtkWidget *locate=gtk_button_new();gtk_widget_set_name(locate,"insight-local-locate");gtk_button_set_image(GTK_BUTTON(locate),insight_command_icon("find-location-symbolic"));gtk_widget_set_tooltip_text(locate,"Locate symbol at caret");g_signal_connect(locate,"clicked",G_CALLBACK(symbols_locate),NULL);gtk_box_pack_start(GTK_BOX(types),locate,FALSE,FALSE,0);
 compact_panel_control(types);
 GtkWidget *info=gtk_button_new_with_label("Symbol Info…");compact_panel_control(info);g_signal_connect(info,"clicked",G_CALLBACK(browser_symbol_info),wb.local_view);gtk_box_pack_start(GTK_BOX(details),info,FALSE,FALSE,0);
 GtkWidget *home=gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),0),*native=ui_lookup_widget(main_widgets.window,"scrolledwindow2");
 wb.native_symbol_query=ui_lookup_widget(main_widgets.window,"entry_tagfilter");gtk_widget_set_no_show_all(wb.native_symbol_query,TRUE);gtk_widget_hide(wb.native_symbol_query);
 wb.local_mode=gtk_combo_box_text_new();gtk_widget_set_name(wb.local_mode,"insight-file-symbol-mode");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.local_mode),"Structure");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.local_mode),"Detailed symbols");gtk_combo_box_set_active(GTK_COMBO_BOX(wb.local_mode),0);compact_panel_control(wb.local_mode);gtk_box_pack_start(GTK_BOX(box),wb.local_mode,FALSE,FALSE,0);
 wb.local_stack=gtk_stack_new();gtk_widget_set_name(wb.local_stack,"insight-file-symbol-stack");gtk_stack_set_homogeneous(GTK_STACK(wb.local_stack),FALSE);
 GtkWidget *structure=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);g_object_ref(native);gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(native)),native);gtk_box_pack_start(GTK_BOX(structure),native,TRUE,TRUE,0);g_object_unref(native);
 GtkWidget *sortbar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);const gchar *sorts[]={"Name","Source order"};for(guint i=0;i<2;i++) { GtkWidget *sort=gtk_button_new_with_label(sorts[i]);compact_panel_control(sort);gtk_box_pack_start(GTK_BOX(sortbar),sort,TRUE,TRUE,0);g_signal_connect(sort,"clicked",G_CALLBACK(local_native_sort),GINT_TO_POINTER(i ? SYMBOLS_SORT_BY_APPEARANCE : SYMBOLS_SORT_BY_NAME)); }gtk_box_pack_start(GTK_BOX(structure),sortbar,FALSE,FALSE,0);
 gtk_stack_add_named(GTK_STACK(wb.local_stack),structure,"structure");gtk_stack_add_named(GTK_STACK(wb.local_stack),details,"details");gtk_box_pack_start(GTK_BOX(box),wb.local_stack,TRUE,TRUE,0);gtk_stack_set_visible_child_name(GTK_STACK(wb.local_stack),"structure");gtk_box_pack_start(GTK_BOX(home),box,TRUE,TRUE,0);gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(main_widgets.sidebar_notebook),home,"File Symbols");
 g_signal_connect(wb.local_mode,"changed",G_CALLBACK(local_mode_changed),NULL);g_signal_connect(wb.native_symbol_query,"changed",G_CALLBACK(local_structure_query),NULL);
 gtk_notebook_set_show_tabs(GTK_NOTEBOOK(main_widgets.sidebar_notebook),TRUE);
 g_signal_connect(wb.local_entry,"changed",G_CALLBACK(local_filter),NULL); g_signal_connect(wb.local_entry,"activate",G_CALLBACK(browser_entry_activate),NULL);
 g_signal_connect(wb.local_entry,"key-press-event",G_CALLBACK(browser_key),NULL); g_signal_connect(wb.local_view,"key-press-event",G_CALLBACK(browser_key),NULL); symbol_context_attach(wb.local_view);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view)),"changed",G_CALLBACK(browser_selected),NULL);
 g_signal_connect(wb.local_view,"row-activated",G_CALLBACK(browser_activated),NULL); gtk_widget_show_all(box);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.local_symbols),SYMBOL_LINE,GTK_SORT_ASCENDING);
 gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),0); local_filter(NULL,NULL);
}
static gint folder_compare(GtkTreeModel *model,GtkTreeIter *a,GtkTreeIter *b,gpointer data) {
 (void)data; gchar *an,*bn; gboolean ad,bd; gtk_tree_model_get(model,a,0,&an,2,&ad,-1); gtk_tree_model_get(model,b,0,&bn,2,&bd,-1);
 gint result=ad!=bd ? (ad ? -1 : 1) : g_utf8_collate(an,bn); g_free(an); g_free(bn); return result;
}
static GtkTreeIter *tree_directory(GHashTable *nodes,const char *path,const char *root,guint depth) {
 GtkTreeIter *iter=g_hash_table_lookup(nodes,path); if(iter) return iter;
 if(depth>=64 || !strcmp(path,root)) return NULL;
 gchar *parent=g_path_get_dirname(path); GtkTreeIter *above=strcmp(parent,path) ? tree_directory(nodes,parent,root,depth+1) : NULL;
 iter=g_new(GtkTreeIter,1); gtk_tree_store_append(wb.project_tree,iter,above); gchar *name=g_path_get_basename(path);
 gtk_tree_store_set(wb.project_tree,iter,0,name,1,path,2,TRUE,-1); g_hash_table_insert(nodes,g_strdup(path),iter); g_free(name); g_free(parent); return iter;
}
/* Keep the selected source and expanded folders stable across manifest/filter
 * rebuilds. Model paths alone are not stable when siblings are sorted. */
typedef struct { const gchar *wanted; GtkTreePath *found; GHashTable *expanded; } FolderVisit;
static gboolean folder_visit(GtkTreeModel *model,GtkTreePath *row,GtkTreeIter *iter,gpointer data) {
 FolderVisit *visit=data; gchar *path=NULL; gtk_tree_model_get(model,iter,1,&path,-1);
 if(visit->expanded && gtk_tree_view_row_expanded(GTK_TREE_VIEW(wb.tree_view),row)) g_hash_table_add(visit->expanded,g_strdup(path));
 if(visit->wanted && !g_strcmp0(path,visit->wanted)) { visit->found=gtk_tree_path_copy(row); g_free(path); return TRUE; }
 g_free(path); return FALSE;
}
static gboolean folder_restore_expanded(GtkTreeModel *model,GtkTreePath *row,GtkTreeIter *iter,gpointer data) {
 gchar *path=NULL; gtk_tree_model_get(model,iter,1,&path,-1);
 if(g_hash_table_contains(data,path)) gtk_tree_view_expand_row(GTK_TREE_VIEW(wb.tree_view),row,FALSE);
 g_free(path); return FALSE;
}
static gchar *folder_selected_path(gboolean *directory) {
 GtkTreeModel *model; GtkTreeIter iter;
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.tree_view)),&model,&iter)) return NULL;
 gchar *path=NULL; gtk_tree_model_get(model,&iter,1,&path,2,directory,-1); return path;
}
