/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Search commands and toolbar setup.
 * RU: Команды поиска и настройка панели инструментов. */

static void search_scope_command(GtkWidget *widget,gpointer data) {
 (void)widget;gint scope=GPOINTER_TO_INT(data);if(scope==0) { search_command_dialog(NULL,NULL);return; }if(scope<2) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.search_scope),scope);search_toolbar_focus(NULL,NULL);if(scope>=2) search_choose_files(NULL,GINT_TO_POINTER(scope==2));
}
static void create_search_toolbars(void) {
 wb.search_history=g_ptr_array_new_with_free_func(g_free);
 GtkWidget *parent=ui_lookup_widget(main_widgets.window,"vbox1");
 gint position=gtk_widget_get_parent(main_widgets.toolbar)==parent ? 2 : 1;
 for(guint i=0;i<2;i++) {
  GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*field=i ? NULL : gtk_search_entry_new(),*sensitive=gtk_check_button_new_with_label("Match case"),*word=gtk_check_button_new_with_label("Whole word"),*regex=gtk_check_button_new_with_label("Regex");
  if(i) { wb.editor_history_combo=gtk_combo_box_text_new_with_entry();gtk_widget_set_name(wb.editor_history_combo,"insight-file-search-history");field=gtk_bin_get_child(GTK_BIN(wb.editor_history_combo));ui_entry_add_clear_icon(GTK_ENTRY(field)); }
  gtk_widget_set_tooltip_text(field,"Use ~term for fuzzy search when Regex is off; characters match in order within a line"); gtk_entry_set_max_length(GTK_ENTRY(field),1024); gtk_widget_set_hexpand(field,TRUE); gtk_widget_set_size_request(field,180,-1);
  gtk_box_pack_start(GTK_BOX(row),gtk_label_new(i ? "File Search:" : "Search project:"),FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(row),i ? wb.editor_history_combo : field,TRUE,TRUE,0);
  if(i) {
   wb.editor_search_row=row; wb.editor_query=field;gtk_widget_set_name(field,"insight-file-search-query"); wb.editor_case=sensitive; wb.editor_word=word; wb.editor_regex=regex;
   GtkWidget *previous=gtk_button_new_with_label("Previous"),*next=gtk_button_new_with_label("Next");
   gtk_widget_set_name(previous,"insight-file-search-previous");gtk_widget_set_name(next,"insight-file-search-next");gtk_button_set_always_show_image(GTK_BUTTON(next),TRUE);gtk_button_set_always_show_image(GTK_BUTTON(previous),TRUE);gtk_button_set_image(GTK_BUTTON(next),gtk_image_new_from_icon_name("go-down",GTK_ICON_SIZE_MENU));gtk_button_set_image(GTK_BUTTON(previous),gtk_image_new_from_icon_name("go-up",GTK_ICON_SIZE_MENU));gtk_box_pack_start(GTK_BOX(row),next,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),previous,FALSE,FALSE,0);
   GtkWidget *files=gtk_button_new_with_label("Search Files…");gtk_widget_set_name(files,"insight-file-search-files");gtk_button_set_image(GTK_BUTTON(files),gtk_image_new_from_icon_name("system-search",GTK_ICON_SIZE_MENU));gtk_button_set_always_show_image(GTK_BUTTON(files),TRUE);gtk_box_pack_start(GTK_BOX(row),files,FALSE,FALSE,0);g_signal_connect(files,"clicked",G_CALLBACK(search_scope_command),GINT_TO_POINTER(0));
   g_signal_connect(previous,"clicked",G_CALLBACK(editor_text_search),GINT_TO_POINTER(-1)); g_signal_connect(next,"clicked",G_CALLBACK(editor_text_search),NULL); g_signal_connect(field,"activate",G_CALLBACK(editor_text_search),NULL);
  } else {
   wb.project_search_row=row; wb.project_query=field; gtk_widget_set_name(field,"insight-project-query"); wb.project_case=sensitive; wb.project_word=word; wb.project_regex=regex;
   GtkWidget *history=gtk_button_new_with_label("History"); gtk_widget_set_name(history,"insight-search-history"); gtk_widget_set_tooltip_text(history,"Last 32 valid search queries; choosing a query retains the current scope and options"); gtk_box_pack_start(GTK_BOX(row),history,FALSE,FALSE,0); g_signal_connect(history,"clicked",G_CALLBACK(search_history_menu),NULL);
   GtkWidget *search=gtk_button_new_with_label("Search"); gtk_box_pack_start(GTK_BOX(row),search,FALSE,FALSE,0); g_signal_connect(search,"clicked",G_CALLBACK(project_text_search),NULL); g_signal_connect(field,"activate",G_CALLBACK(project_text_search),NULL);
  }
  if(i) {
   GtkWidget *options=gtk_menu_button_new();gtk_button_set_label(GTK_BUTTON(options),"Options");gtk_button_set_image(GTK_BUTTON(options),gtk_image_new_from_icon_name("preferences-system",GTK_ICON_SIZE_MENU));gtk_button_set_always_show_image(GTK_BUTTON(options),TRUE);gtk_widget_set_name(options,"insight-file-search-options");GtkWidget *popover=gtk_popover_new(options),*checks=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_container_set_border_width(GTK_CONTAINER(checks),8);gtk_container_add(GTK_CONTAINER(popover),checks);gtk_menu_button_set_popover(GTK_MENU_BUTTON(options),popover);gtk_box_pack_start(GTK_BOX(row),options,FALSE,FALSE,0);
   wb.editor_wrap=gtk_check_button_new_with_label("Wrap around");wb.editor_select=gtk_check_button_new_with_label("Select text when found");wb.editor_incremental=gtk_check_button_new_with_label("Search while typing");wb.editor_load_word=gtk_check_button_new_with_label("Load word at cursor when buttons are pressed");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_wrap),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_select),TRUE);
   GtkWidget *items[]={sensitive,word,regex,wb.editor_wrap,wb.editor_select,wb.editor_incremental,wb.editor_load_word};const gchar *names[]={"case","word","regex","wrap","select","incremental","load-word"};for(guint n=0;n<G_N_ELEMENTS(items);n++) { gtk_box_pack_start(GTK_BOX(n==0 || n==3 ? row : checks),items[n],FALSE,FALSE,0);gchar *name=g_strconcat("insight-file-search-",names[n],NULL);gtk_widget_set_name(items[n],name);g_free(name);if(n<3) g_signal_connect(items[n],"toggled",G_CALLBACK(editor_search_sync),NULL); }const gchar *actions[]={"Find All in Document","Find All in Open Documents","Mark all matches"};for(guint n=0;n<3;n++) { GtkWidget *action=gtk_button_new_with_label(actions[n]);gtk_box_pack_start(GTK_BOX(checks),action,FALSE,FALSE,0);g_signal_connect(action,"clicked",G_CALLBACK(editor_search_all),GINT_TO_POINTER(n)); }gtk_box_reorder_child(GTK_BOX(row),sensitive,-1);gtk_widget_show_all(checks);g_signal_connect(field,"changed",G_CALLBACK(editor_search_sync),NULL);
  } else { gtk_box_pack_start(GTK_BOX(row),sensitive,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),word,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),regex,FALSE,FALSE,0); }
  gtk_box_pack_end(GTK_BOX(row),insight_panel_help_button(i ? "panel-find-file.md" : "panel-search-results.md"),FALSE,FALSE,0);
  GtkWidget *close=gtk_button_new_from_icon_name("window-close",GTK_ICON_SIZE_BUTTON);gtk_widget_set_tooltip_text(close,"Close search panel (Escape)");gtk_box_pack_end(GTK_BOX(row),close,FALSE,FALSE,0);g_signal_connect(close,"clicked",G_CALLBACK(search_panel_close),row);
  g_signal_connect(field,"key-press-event",G_CALLBACK(search_panel_key),row);gtk_box_pack_start(GTK_BOX(parent),row,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(parent),row,position+(gint)i);gtk_widget_set_name(row,i ? "insight-editor-search-panel" : "insight-project-search-panel");gtk_widget_show_all(row);gtk_widget_set_no_show_all(row,TRUE);gtk_widget_hide(row);
 }
 GtkWidget *search_menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"search1")));
 GtkWidget *quick=gtk_menu_item_new_with_label("Quick find in file…");gtk_menu_shell_append(GTK_MENU_SHELL(search_menu),quick);g_signal_connect(quick,"activate",G_CALLBACK(search_toolbar_focus),GINT_TO_POINTER(1));gtk_widget_show(quick);
 create_search_results();
 const gchar *link_names[]={"Jump To Link","Go To First Link","Go To Previous Link","Go To Next Link","Go To Last Link"};const gint link_directions[]={0,-3,-1,1,3};for(guint i=0;i<G_N_ELEMENTS(link_names);i++) { GtkWidget *item=gtk_menu_item_new_with_label(link_names[i]);gchar *name=g_strdup_printf("insight-source-link-command-%u",i);gtk_widget_set_name(item,name);g_free(name);gtk_menu_shell_append(GTK_MENU_SHELL(search_menu),item);g_signal_connect(item,"activate",i ? G_CALLBACK(search_result_move) : G_CALLBACK(search_jump_to_link),GINT_TO_POINTER(link_directions[i]));gtk_widget_show(item); }
 GtkWidget *lookup=gtk_menu_item_new_with_label("Lookup References…");gtk_widget_set_name(lookup,"insight-lookup-references-command");gtk_menu_shell_append(GTK_MENU_SHELL(search_menu),lookup);g_signal_connect(lookup,"activate",G_CALLBACK(search_command_dialog),GINT_TO_POINTER(1));gtk_widget_show(lookup);
 gtk_menu_item_set_label(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"search1")),"Поиск/замена");
 const gchar *scope_names[]={"Search project…","Search open documents…","Search folder…","Search selected files…"};
 for(guint i=0;i<4;i++) { GtkWidget *item=gtk_menu_item_new_with_label(scope_names[i]);gtk_widget_set_name(item,i==0 ? "insight-search-project-command" : (i==2 ? "insight-search-folder-command" : "insight-search-scope-command"));gtk_menu_shell_append(GTK_MENU_SHELL(search_menu),item);g_signal_connect(item,"activate",G_CALLBACK(search_scope_command),GINT_TO_POINTER(i));gtk_widget_show(item); }
 GtkWidget *close=gtk_button_new_from_icon_name("window-close",GTK_ICON_SIZE_BUTTON);gtk_widget_set_tooltip_text(close,"Close search results");gtk_box_pack_start(GTK_BOX(wb.search_page),close,FALSE,FALSE,0);g_signal_connect(close,"clicked",G_CALLBACK(search_panel_close),wb.search_page);gtk_widget_show(close);
}
static void toolbar_relations(GtkToolButton *button,gpointer data) { (void)button;focus_relations(NULL,data); }
static void add_insight_tool(GtkToolbar *toolbar,GtkToolItem *item,gint position) {
 toolbar_item_ref(item);gtk_toolbar_insert(toolbar,item,position);
}
static GtkCssProvider *panel_font_css;
void insight_workbench_panel_font(void) {
 if(!wb.project_notebook) return;
 if(!panel_font_css) {panel_font_css=gtk_css_provider_new();gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(main_widgets.window),GTK_STYLE_PROVIDER(panel_font_css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+3);}
 gchar *css=g_strdup_printf(".insight-panel-font {font-size: %dpt;} .insight-panel-font * {font-size: inherit;} .insight-panel-font .insight-compare-toolbar {font-size: %d%%;}",CLAMP(toolbar_prefs.panel_font_size,6,28),INSIGHT_COMPARE_CONTROL_PERCENT);gtk_css_provider_load_from_data(panel_font_css,css,-1,NULL);g_free(css);
}
static gboolean panel_font_scroll(GtkWidget *widget,GdkEventScroll *event,gpointer data) {
 (void)widget;(void)data;if(!(event->state&GDK_CONTROL_MASK)) return FALSE;
 gdouble dx=0,dy=0;if(event->direction==GDK_SCROLL_UP) dy=-1;else if(event->direction==GDK_SCROLL_DOWN) dy=1;else if(!gdk_event_get_scroll_deltas((GdkEvent *)event,&dx,&dy) || ABS(dy)<0.05) return TRUE;
 toolbar_prefs.panel_font_size=CLAMP(toolbar_prefs.panel_font_size+(dy<0 ? 1:-1),6,28);insight_workbench_panel_font();return TRUE;
}
static void panel_font_watch(GtkWidget *widget,gpointer data);
static void panel_font_added(GtkContainer *container,GtkWidget *child,gpointer data) {(void)container;panel_font_watch(child,data);}
static void panel_font_watch(GtkWidget *widget,gpointer data) {
 if(g_object_get_data(G_OBJECT(widget),"insight-panel-font-watch")) return;g_object_set_data(G_OBJECT(widget),"insight-panel-font-watch",GINT_TO_POINTER(1));gtk_widget_add_events(widget,GDK_SCROLL_MASK|GDK_SMOOTH_SCROLL_MASK);g_signal_connect(widget,"scroll-event",G_CALLBACK(panel_font_scroll),NULL);
 if(GTK_IS_CONTAINER(widget)) {gtk_container_foreach(GTK_CONTAINER(widget),panel_font_watch,data);g_signal_connect(widget,"add",G_CALLBACK(panel_font_added),data);}
}
static void create_browse_toolbar(void) {
 insight_git_toolbar();
 GtkToolbar *toolbar=GTK_TOOLBAR(main_widgets.toolbar);
 for(guint i=0;i<2;i++) {const gchar *name=i ? "Redo":"Undo";GtkWidget *existing=toolbar_get_widget_by_name(name);if(!existing) {GtkAction *action=toolbar_get_action_by_name(name);if(action) add_insight_tool(toolbar,GTK_TOOL_ITEM(gtk_action_create_tool_item(action)),0);}}
 GtkWidget *search_field=toolbar_get_widget_by_name("SearchEntry");if(search_field) { gtk_widget_set_no_show_all(search_field,TRUE);gtk_widget_hide(search_field); }
 add_insight_tool(toolbar,gtk_separator_tool_item_new(),0);
 const char *icons[]={"go-previous","go-next","go-jump","edit-find","system-search","folder-open","view-refresh"};
 const char *labels[]={"Navigate back a location","Navigate forward a location","Go to definition","Project symbols","Project files","Open source workspace","Reindex workspace"};
 for(guint i=0;i<7;i++) {
  if(i<2 && toolbar_get_widget_by_name(i ? "NavFor":"NavBack")) continue;
  GtkToolItem *item=i<2 ? GTK_TOOL_ITEM(gtk_action_create_tool_item(toolbar_get_action_by_name(i ? "NavFor":"NavBack"))):gtk_tool_button_new(NULL,NULL);gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(item),icons[i]);gtk_widget_set_tooltip_text(GTK_WIDGET(item),labels[i]);
  if(i==2) g_signal_connect(item,"clicked",G_CALLBACK(toolbar_definition),NULL);
  else if(i>=3 && i<5) g_signal_connect(item,"clicked",G_CALLBACK(toolbar_focus),GINT_TO_POINTER(i==4 ? 1 : 0));
  else if(i==5) g_signal_connect(item,"clicked",G_CALLBACK(choose_root),NULL);
  else if(i==6) g_signal_connect(item,"clicked",G_CALLBACK(reindex_root),NULL);
  g_object_set_data(G_OBJECT(item),"insight-toolbar-order",GINT_TO_POINTER(i<3 ? 300+i:500+i));add_insight_tool(toolbar,item,(gint)i);
 }
 gtk_toolbar_set_style(toolbar,GTK_TOOLBAR_ICONS);gtk_toolbar_set_icon_size(toolbar,GTK_ICON_SIZE_SMALL_TOOLBAR);toolbar_apply_settings(); gtk_widget_show_all(GTK_WIDGET(toolbar));
 GtkToolItem *templates=gtk_tool_button_new(insight_command_icon("snippets"),NULL); gtk_widget_set_name(GTK_WIDGET(templates),"insight-templates-tool");g_object_set_data(G_OBJECT(templates),"insight-toolbar-order",GINT_TO_POINTER(206)); gtk_widget_set_tooltip_text(GTK_WIDGET(templates),"Code templates / Clips"); g_signal_connect(templates,"clicked",G_CALLBACK(focus_templates),NULL); add_insight_tool(toolbar,templates,-1);
 GtkToolItem *book=gtk_tool_button_new(insight_command_icon("bookmarks"),NULL);g_object_set_data(G_OBJECT(book),"insight-toolbar-order",GINT_TO_POINTER(304));gtk_widget_set_tooltip_text(GTK_WIDGET(book),"Bookmarks — all files and lines");g_signal_connect(book,"clicked",G_CALLBACK(show_bookmarks),NULL);add_insight_tool(toolbar,book,-1);
 GtkToolItem *relations=gtk_tool_button_new(insight_command_icon("relations"),NULL);g_object_set_data(G_OBJECT(relations),"insight-toolbar-order",GINT_TO_POINTER(510));gtk_widget_set_tooltip_text(GTK_WIDGET(relations),"Relation Window — calls, call sites, members and inheritance");g_signal_connect(relations,"clicked",G_CALLBACK(toolbar_relations),NULL);add_insight_tool(toolbar,relations,-1);
 GtkToolItem *call_paths=gtk_menu_tool_button_new(insight_command_icon("relations"),insight_tr("Call paths"));gtk_widget_set_name(GTK_WIDGET(call_paths),"insight-call-paths-tool");g_object_set_data(G_OBJECT(call_paths),"insight-toolbar-order",GINT_TO_POINTER(511));insight_i18n_key(G_OBJECT(call_paths),"label","Call paths");insight_i18n_key(G_OBJECT(call_paths),"tooltip-text","Call paths — open graph; arrow offers endpoints, search and tutorial");gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(call_paths),call_paths_menu("insight-call-tool-menu"));g_signal_connect(call_paths,"clicked",G_CALLBACK(call_paths_command),GINT_TO_POINTER(INSIGHT_CALL_SHOW));add_insight_tool(toolbar,call_paths,-1);
 GtkToolItem *diff=gtk_tool_button_new(insight_command_icon("diff"),NULL);gtk_widget_set_name(GTK_WIDGET(diff),"insight-diff-tool");g_object_set_data(G_OBJECT(diff),"insight-toolbar-order",GINT_TO_POINTER(800));gtk_widget_set_tooltip_text(GTK_WIDGET(diff),"Diff / Merge — internal comparison and merging");g_signal_connect(diff,"clicked",G_CALLBACK(show_diff),NULL);add_insight_tool(toolbar,diff,-1);
 const gchar *search_icons[]={"edit-find","edit-find-replace","system-search"};const gchar *search_tips[]={"Find in file (Ctrl+F)","Replace in file (Ctrl+H)","Search project / folder / open files"};
 for(guint i=0;i<3;i++) { GtkToolItem *button=gtk_tool_button_new(insight_command_icon(search_icons[i]),NULL);gtk_widget_set_tooltip_text(GTK_WIDGET(button),search_tips[i]);gtk_widget_set_name(GTK_WIDGET(button),i==0 ? "insight-find-tool" : (i==1 ? "insight-replace-tool" : "insight-project-search-tool"));g_object_set_data(G_OBJECT(button),"insight-toolbar-order",GINT_TO_POINTER(400+i));g_signal_connect(button,"clicked",i<2 ? G_CALLBACK(search_native_command) : G_CALLBACK(search_scope_command),i<2 ? (gpointer)(i ? "replace1" : "find1") : GINT_TO_POINTER(0));add_insight_tool(toolbar,button,-1); }
 const gchar *help_icons[]={"manual","tutorial"};const gchar *help_tips[]={"Geany Insight — documentation","Geany Insight — tutorials"};
 for(guint i=0;i<2;i++) { GtkToolItem *button=gtk_tool_button_new(insight_command_icon(help_icons[i]),NULL);gtk_widget_set_name(GTK_WIDGET(button),i ? "insight-tutorial-tool" : "insight-help-tool");gtk_widget_set_tooltip_text(GTK_WIDGET(button),help_tips[i]);g_object_set_data(G_OBJECT(button),"insight-toolbar-order",GINT_TO_POINTER(1000+i));g_signal_connect(button,"clicked",G_CALLBACK(insight_help_show),(gpointer)(i ? "tutorials.md" : "guide.md"));add_insight_tool(toolbar,button,-1); }
 GtkToolItem *preview=gtk_tool_button_new(insight_command_icon("text-x-generic"),NULL);gtk_widget_set_name(GTK_WIDGET(preview),"insight-markup-preview-tool");g_object_set_data(G_OBJECT(preview),"insight-toolbar-order",GINT_TO_POINTER(801));gtk_widget_set_tooltip_text(GTK_WIDGET(preview),"Preview current Markdown / RST — including unsaved edits");g_signal_connect(preview,"clicked",G_CALLBACK(insight_markup_preview),NULL);add_insight_tool(toolbar,preview,-1);
 add_insight_tool(toolbar,gtk_separator_tool_item_new(),-1);
 const char *layouts[]={"layout-a","layout-b","layout-c","layout-d"};
 const char *tips[]={"Load layout A","Load layout B","Load layout C","Load layout D"};
 for(guint i=0;i<4;i++) { GtkToolItem *item=gtk_tool_button_new(insight_command_icon(layouts[i]),NULL); g_object_set_data(G_OBJECT(item),"insight-toolbar-order",GINT_TO_POINTER(900+i));gtk_widget_set_tooltip_text(GTK_WIDGET(item),tips[i]); g_signal_connect(item,"clicked",G_CALLBACK(apply_layout),GINT_TO_POINTER(i)); add_insight_tool(toolbar,item,-1); }
 toolbar_apply_settings();gtk_widget_show_all(GTK_WIDGET(toolbar));
}
static void create_help_menu(void) {
 GtkWidget *menu=ui_lookup_widget(main_widgets.window,"menu_help1_menu");const gchar *labels[]={"Geany Insight — Documentation…","Geany Insight — Tutorials…","Lua scripting guide…","Markdown / reStructuredText viewer…","Preview current Markdown / RST…"};
 const gchar *topics[]={"guide.md","tutorials.md","scripting.md",NULL,NULL};
 for(guint i=0;i<5;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,i==0 ? "insight-help-command" : (i==1 ? "insight-tutorial-command" : (i==4 ? "insight-markup-preview-command" : "insight-guide-command")));gtk_menu_shell_insert(GTK_MENU_SHELL(menu),item,i);g_signal_connect(item,"activate",i==3 ? G_CALLBACK(insight_markup_open) : (i==4 ? G_CALLBACK(insight_markup_preview) : G_CALLBACK(insight_help_show)),(gpointer)topics[i]);gtk_widget_show(item); }
}
static void window_list_preview(guint id) {
 GeanyDocument *doc=document_find_by_id(id);if(!doc || !wb.preview) return;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);show_location(doc->file_name,doc->id,sci_get_current_line(doc->editor->sci)+1,TRUE);
}
static void window_activate(GtkMenuItem *item,gpointer data) {
 (void)item; GeanyDocument *doc=document_find_by_id(GPOINTER_TO_UINT(data));
 if(doc) { gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.notebook),document_get_notebook_page(doc)); document_grab_focus(doc); }
}
static void window_menu_refresh(GtkWidget *menu,gpointer data) {
 (void)data; GList *items=gtk_container_get_children(GTK_CONTAINER(menu)); for(GList *p=items;p;p=p->next) if(!g_object_get_data(G_OBJECT(p->data),"insight-window-persistent")) gtk_widget_destroy(p->data); g_list_free(items);
 GtkWidget *windows=gtk_menu_item_new_with_label("Window List…");gtk_widget_set_name(windows,"insight-window-list-command");g_signal_connect(windows,"activate",G_CALLBACK(insight_window_list_show),NULL);gtk_menu_shell_insert(GTK_MENU_SHELL(menu),windows,0);gtk_widget_show(windows);
 GtkWidget *separator=gtk_separator_menu_item_new();gtk_menu_shell_insert(GTK_MENU_SHELL(menu),separator,1);gtk_widget_show(separator);
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i); if(!doc->is_valid) continue;
  GtkWidget *item=gtk_check_menu_item_new_with_label(doc->file_name ? doc->file_name : "untitled");g_object_set_data(G_OBJECT(item),"insight-no-i18n",GINT_TO_POINTER(1));
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),doc==document_get_current());
  g_signal_connect(item,"activate",G_CALLBACK(window_activate),GUINT_TO_POINTER(doc->id)); gtk_container_add(GTK_CONTAINER(menu),item); gtk_widget_show(item);
 }
}
static void arrange_main_menus(void) {
 GtkWidget *bar=ui_lookup_widget(main_widgets.window,"menubar1"),*options=gtk_menu_item_new_with_mnemonic("_Options"),*menu=gtk_menu_new();
 gtk_menu_item_set_submenu(GTK_MENU_ITEM(options),menu); gtk_menu_shell_insert(GTK_MENU_SHELL(bar),options,4);
 GtkWidget *preferences=ui_lookup_widget(main_widgets.window,"preferences1");
 g_object_ref(preferences); gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(preferences)),preferences); gtk_container_add(GTK_CONTAINER(menu),preferences); g_object_unref(preferences); gtk_widget_set_name(menu,"insight-options-menu");insight_i18n_menu(menu);GtkWidget *languages_item=gtk_menu_item_new_with_label("Language profiles…");gtk_widget_set_name(languages_item,"insight-language-options");g_signal_connect(languages_item,"activate",G_CALLBACK(insight_languages_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),languages_item);GtkWidget *filetype_options=gtk_menu_item_new_with_label("File Type Options…");gtk_widget_set_name(filetype_options,"insight-filetype-options");g_signal_connect(filetype_options,"activate",G_CALLBACK(insight_languages_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),filetype_options);insight_formatting_menu(ui_lookup_widget(main_widgets.window,"menu_view1_menu"),menu);insight_themes_menu(menu);insight_completion_menu(menu);insight_paste_menu(menu);insight_paste_line_menu(ui_lookup_widget(main_widgets.window,"edit1_menu"));insight_paste_tools_menu(ui_lookup_widget(main_widgets.window,"edit1_menu"));insight_navigation_menu(ui_lookup_widget(main_widgets.window,"edit1_menu"));gtk_widget_show_all(options);
 GtkWidget *document_menu=ui_lookup_widget(main_widgets.window,"menu_document1");
 g_object_ref(document_menu); gtk_container_remove(GTK_CONTAINER(bar),document_menu); gtk_container_add(GTK_CONTAINER(menu),document_menu); g_object_unref(document_menu);
 GtkWidget *windows=gtk_menu_item_new_with_mnemonic("_Window"),*window_menu=gtk_menu_new();
 insight_scroll_links_init(window_menu);
 gtk_widget_set_name(window_menu,"insight-window-menu");gtk_menu_item_set_submenu(GTK_MENU_ITEM(windows),window_menu); g_signal_connect(window_menu,"show",G_CALLBACK(window_menu_refresh),NULL); gtk_menu_shell_insert(GTK_MENU_SHELL(bar),windows,7); gtk_widget_show(windows);
 GtkWidget *items[]={ui_lookup_widget(main_widgets.window,"file1"),ui_lookup_widget(main_widgets.window,"edit1"),ui_lookup_widget(main_widgets.window,"search1"),ui_lookup_widget(main_widgets.window,"menu_project1"),options,ui_lookup_widget(main_widgets.window,"tools1"),ui_lookup_widget(main_widgets.window,"menu_view1"),windows,ui_lookup_widget(main_widgets.window,"menu_help1"),g_object_get_data(G_OBJECT(main_widgets.window),"insight-navigate")};
 for(guint i=0;i<G_N_ELEMENTS(items);i++) {
  GtkWidget *item=items[i];
  g_object_ref(item); gtk_container_remove(GTK_CONTAINER(bar),item); gtk_menu_shell_insert(GTK_MENU_SHELL(bar),item,(gint)i); g_object_unref(item);
 }
}
void insight_workbench_semantic_changed(guint document) {if(!wb.alive) return;GeanyDocument *doc=document_find_by_id(document);if(!DOC_VALID(doc) || doc!=document_get_current()) return;wb.relation_dirty=TRUE;wb.follow_doc=0;queue_definition(TRUE);insight_workbench_refresh();}
void insight_workbench_index_changed(void) { if(wb.alive && wb.bookmark_handles && !wb.bookmark_sync_timer) wb.bookmark_sync_timer=g_idle_add(bookmark_sync_idle,NULL);stop_definition(); wb.follow_doc=0; mark_index_dirty(); insight_workbench_refresh(); }
static gboolean remote_workspace(const char *path) { gboolean previous=wb.workspace_from_project; wb.workspace_from_project=FALSE; if(start_scan(path)) return TRUE; wb.workspace_from_project=previous; return FALSE; }
static void follow_changed(GtkToggleButton *button,gpointer data) {
 (void)data;
 if(gtk_toggle_button_get_active(button)) { stop_file_preview(); wb.follow_doc=0; queue_definition(TRUE); insight_workbench_refresh(); }
 else stop_definition();
}
static void wrap_changed(GtkToggleButton *button,gpointer data) {
 (void)data; if(wb.preview) SSM(wb.preview,SCI_SETWRAPMODE,gtk_toggle_button_get_active(button) ? SC_WRAP_WORD : SC_WRAP_NONE,0);
}
static void copy_location(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 if(!wb.source_path || !wb.source_line) { ui_set_statusbar(TRUE,"Context has no saved file location"); return; }
 gchar *address=g_strdup_printf("%s:%d",wb.source_path,sci_get_current_line(wb.preview)+1);
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),address,-1); g_free(address);
 ui_set_statusbar(TRUE,"Context file:line copied");
}
static void show_diff(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; insight_diff_show(main_widgets.window);
}
static void context_tools_toggle(GtkToggleButton *button,gpointer row) {
 gboolean visible=gtk_toggle_button_get_active(button);gtk_widget_set_visible(GTK_WIDGET(row),visible);
 if(visible && row==wb.context_find_row) gtk_widget_grab_focus(wb.find_entry);
 GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(main_widgets.window));
 if(!visible && focus && gtk_widget_is_ancestor(focus,GTK_WIDGET(row))) gtk_widget_grab_focus(GTK_WIDGET(wb.preview));
}
static void context_icon(GtkWidget *button,const gchar *icon,const gchar *tip,const gchar *name) {
 gtk_button_set_label(GTK_BUTTON(button),NULL);gtk_button_set_image(GTK_BUTTON(button),gtk_image_new_from_icon_name(icon,GTK_ICON_SIZE_MENU));
 gtk_button_set_always_show_image(GTK_BUTTON(button),TRUE);gtk_button_set_relief(GTK_BUTTON(button),GTK_RELIEF_NONE);gtk_widget_set_tooltip_text(button,tip);gtk_widget_set_name(button,name);
}
static InsightReportSnapshot *capture_project_report(GError **error)
{
 if(!wb.alive || !app->project || !wb.workspace_from_project || !wb.root) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_INITIALIZED,"Open a Geany project before generating a project report.");return NULL;
 }
 refresh_index();
 if(wb.scan || wb.scan_worker || wb.snapshot_worker || wb.index_dirty || !wb.index || wb.scan_failed || !wb.report_scan_completed) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"Project indexing is not ready. Wait for a completed scan before generating a report.");return NULL;
 }
 gchar *root=utils_get_utf8_from_locale(wb.root);
 InsightReportSnapshot *snapshot=insight_report_snapshot_new(app->project->name,root,app->project->file_name,wb.files,wb.index,wb.skipped,wb.files->len>=10000 || wb.skipped>0 || wb.watch_incomplete,error);if(snapshot) snapshot->calls=insight_index_ref(wb.disk_calls);g_free(root);return snapshot;
}
GPtrArray *insight_workbench_source_paths(gboolean (*eligible)(const gchar *),guint limit) {
 GPtrArray *paths=g_ptr_array_new_with_free_func(g_free);
 if(wb.alive && app->project && wb.workspace_from_project && wb.files)
  for(guint i=0;i<wb.files->len && paths->len<limit;i++) {const gchar *path=g_ptr_array_index(wb.files,i);if(!eligible || eligible(path)) g_ptr_array_add(paths,g_strdup(path));}
 return paths;
}
InsightReportSnapshot *insight_workbench_source_snapshot(GError **error) {
 if(app->project) return capture_project_report(error);
 if(!wb.alive) {g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_INITIALIZED,"Source workspace is not ready.");return NULL;}
 InsightIndex *symbols=insight_index_new(),*calls=insight_index_new();GPtrArray *paths=g_ptr_array_new_with_free_func(g_free);guint skipped=0;gboolean ok=TRUE;
 /* Copy GUI-owned tags before handing immutable snapshots to workers. Do not
  * use wb.base: a previously selected directory is not this temporary project. */
 for(guint i=0;i<documents_array->len;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!DOC_VALID(doc) || !doc->file_name) continue;
  if(paths->len>=256) {skipped++;continue;}
  g_ptr_array_add(paths,g_strdup(doc->file_name));
  if(!insight_native_import(symbols,doc->tm_file,doc->file_name)) ok=FALSE;
  InsightClangResult *r=insight_clang_ui_result(doc->editor->sci);
  if(insight_clang_result_usable(r) && !insight_clang_import(r,doc->file_name,symbols,calls)) ok=FALSE;
 }
 InsightReportSnapshot *snapshot=NULL;
 if(!paths->len) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_INITIALIZED,"Open a named source file to create a temporary workspace.");
 else if(!ok) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Open-source workspace exceeds the symbol limit.");
 else {gchar *root=g_path_get_dirname(g_ptr_array_index(paths,0));snapshot=insight_report_snapshot_new(insight_tr("Open files (temporary workspace)"),root,NULL,paths,symbols,skipped,skipped>0,error);g_free(root);if(snapshot) snapshot->calls=insight_index_ref(calls);}
 insight_index_free(symbols);insight_index_free(calls);g_ptr_array_unref(paths);return snapshot;
}
/* GtkPaned shifts an undersized first child left to preserve its minimum.
 * A width-constrained host keeps the source workspace at x=0 and forwards the
 * available width to its vertical/editor splitters. Preserve height requests:
 * allowing the same underallocation vertically breaks notebook tab gadgets. */
typedef struct { GtkBin parent; } InsightWidthHost;
typedef struct { GtkBinClass parent; } InsightWidthHostClass;
static GType insight_width_host_get_type(void);
G_DEFINE_TYPE(InsightWidthHost,insight_width_host,GTK_TYPE_BIN)
static void width_host_width(GtkWidget *widget,gint *minimum,gint *natural) {
 GtkWidget *child=gtk_bin_get_child(GTK_BIN(widget));gint ignored=0;*minimum=1;*natural=1;if(child && gtk_widget_get_visible(child)) gtk_widget_get_preferred_width(child,&ignored,natural);*natural=MAX(*minimum,*natural);
}
static void width_host_height(GtkWidget *widget,gint *minimum,gint *natural) {
 GtkWidget *child=gtk_bin_get_child(GTK_BIN(widget));*minimum=*natural=0;if(child && gtk_widget_get_visible(child)) gtk_widget_get_preferred_height(child,minimum,natural);if(g_object_get_data(G_OBJECT(widget),"insight-collapse-height")) *minimum=1;*natural=MAX(*minimum,*natural);
}
static void width_host_width_for_height(GtkWidget *widget,gint height,gint *minimum,gint *natural) {
 GtkWidget *child=gtk_bin_get_child(GTK_BIN(widget));gint ignored=0;*minimum=1;*natural=1;if(child && gtk_widget_get_visible(child)) { gint child_height=0;if(g_object_get_data(G_OBJECT(widget),"insight-collapse-height")) gtk_widget_get_preferred_height(child,&child_height,NULL);gtk_widget_get_preferred_width_for_height(child,MAX(height,child_height),&ignored,natural); }*natural=MAX(*minimum,*natural);
}
static void width_host_height_for_width(GtkWidget *widget,gint width,gint *minimum,gint *natural) {
 GtkWidget *child=gtk_bin_get_child(GTK_BIN(widget));*minimum=*natural=0;if(child && gtk_widget_get_visible(child)) { gint child_width=1;gtk_widget_get_preferred_width(child,&child_width,NULL);gtk_widget_get_preferred_height_for_width(child,MAX(width,child_width),minimum,natural); }if(g_object_get_data(G_OBJECT(widget),"insight-collapse-height")) *minimum=1;*natural=MAX(*minimum,*natural);
}
static void width_host_allocate(GtkWidget *widget,GtkAllocation *allocation) {
 gtk_widget_set_allocation(widget,allocation);GtkWidget *child=gtk_bin_get_child(GTK_BIN(widget));if(child && gtk_widget_get_visible(child)) gtk_widget_size_allocate(child,allocation);
}
static void insight_width_host_class_init(InsightWidthHostClass *klass) {
 GtkWidgetClass *widget=GTK_WIDGET_CLASS(klass);widget->get_preferred_width=width_host_width;widget->get_preferred_height=width_host_height;widget->get_preferred_width_for_height=width_host_width_for_height;widget->get_preferred_height_for_width=width_host_height_for_width;widget->size_allocate=width_host_allocate;
}
static void insight_width_host_init(InsightWidthHost *host) { gtk_widget_set_has_window(GTK_WIDGET(host),FALSE);gtk_widget_set_name(GTK_WIDGET(host),"insight-editor-width-host"); }
static GtkWidget *narrow_panel_host(GtkWidget *child) {
 GtkWidget *host=g_object_new(insight_width_host_get_type(),NULL);g_object_set_data(G_OBJECT(host),"insight-collapse-height",GINT_TO_POINTER(1));gtk_widget_set_name(host,"insight-panel-width-host");GtkWidget *viewport=gtk_viewport_new(NULL,NULL);gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport),GTK_SHADOW_NONE);gtk_container_add(GTK_CONTAINER(viewport),child);gtk_container_add(GTK_CONTAINER(host),viewport);return host;
}
/* Give each row its own clipping boundary: a wide checkbox must not force
 * an unrelated entry/button row beyond the shelf's visible edge. */
static void constrain_panel_rows(GtkWidget *widget) {
 if(g_object_get_data(G_OBJECT(widget),"insight-rows-constrained")) return;
 gboolean vertical=GTK_IS_BOX(widget) && gtk_orientable_get_orientation(GTK_ORIENTABLE(widget))==GTK_ORIENTATION_VERTICAL;
 if(!vertical && !GTK_IS_STACK(widget)) return;
 g_object_set_data(G_OBJECT(widget),"insight-rows-constrained",GINT_TO_POINTER(1));
 GList *children=gtk_container_get_children(GTK_CONTAINER(widget));
 for(GList *p=children;p;p=p->next) {
  GtkWidget *child=p->data;
  gboolean nested=GTK_IS_STACK(child) || (GTK_IS_BOX(child) && gtk_orientable_get_orientation(GTK_ORIENTABLE(child))==GTK_ORIENTATION_VERTICAL);
  if(nested) constrain_panel_rows(child);
  else if(vertical && !GTK_IS_SCROLLED_WINDOW(child)) {
   gboolean expand,fill;guint padding;GtkPackType pack;gint position;
   gtk_box_query_child_packing(GTK_BOX(widget),child,&expand,&fill,&padding,&pack);
   gtk_container_child_get(GTK_CONTAINER(widget),child,"position",&position,NULL);
   g_object_ref(child);gtk_container_remove(GTK_CONTAINER(widget),child);
   GtkWidget *host=narrow_panel_host(child);
   if(pack==GTK_PACK_START) gtk_box_pack_start(GTK_BOX(widget),host,expand,fill,padding);else gtk_box_pack_end(GTK_BOX(widget),host,expand,fill,padding);
   gtk_box_reorder_child(GTK_BOX(widget),host,position);g_object_unref(child);gtk_widget_show_all(host);
  }
 }
 g_list_free(children);
}
