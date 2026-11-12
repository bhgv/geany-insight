/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Workbench initialization, updates and shutdown.
 * RU: Запуск, обновление и завершение работы панелей. */

static void constrain_message_width(GtkWidget *page) {
 if(!GTK_IS_BOX(page) || g_object_get_data(G_OBJECT(page),"insight-width-constrained")) return;
 GtkWidget *body=gtk_box_new(gtk_orientable_get_orientation(GTK_ORIENTABLE(page)),gtk_box_get_spacing(GTK_BOX(page))),*host=g_object_new(insight_width_host_get_type(),NULL);
 gtk_widget_set_name(host,"insight-message-width-host");gtk_box_set_homogeneous(GTK_BOX(body),gtk_box_get_homogeneous(GTK_BOX(page)));
 GList *children=gtk_container_get_children(GTK_CONTAINER(page));
 for(GList *p=children;p;p=p->next) { GtkWidget *child=p->data;gboolean expand,fill;guint padding;GtkPackType pack;gtk_box_query_child_packing(GTK_BOX(page),child,&expand,&fill,&padding,&pack);g_object_ref(child);gtk_container_remove(GTK_CONTAINER(page),child);if(pack==GTK_PACK_START) gtk_box_pack_start(GTK_BOX(body),child,expand,fill,padding);else gtk_box_pack_end(GTK_BOX(body),child,expand,fill,padding);g_object_unref(child); }
 g_list_free(children);panel_scroll_minimum(body);GtkWidget *viewport=gtk_viewport_new(NULL,NULL);gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport),GTK_SHADOW_NONE);gtk_container_add(GTK_CONTAINER(viewport),body);gtk_container_add(GTK_CONTAINER(host),viewport);gtk_widget_show(viewport);gtk_box_pack_start(GTK_BOX(page),host,TRUE,TRUE,0);g_object_set_data(G_OBJECT(page),"insight-width-constrained",GINT_TO_POINTER(1));gtk_widget_show(body);gtk_widget_show(host);
}
/* EN: Initialize editor workbench panels, restore settings and connect application signals.
 * RU: Создаёт панели редактора, восстанавливает настройки и подключает сигналы приложения. */
void insight_workbench_init(void) {
 insight_diff_set_source_opener(diff_source_open);insight_diff_set_colorizer(insight_diff_native_colorize);
 insight_directory_set_types_provider(directory_known_types);insight_directory_set_open_guard(directory_path_open);
 wb.main_geometry_handlers[0]=g_signal_connect(main_widgets.window,"configure-event",G_CALLBACK(main_geometry_changed),NULL);
 wb.main_geometry_handlers[1]=g_signal_connect(main_widgets.window,"window-state-event",G_CALLBACK(main_geometry_changed),NULL);
 for(guint i=0;i<10;i++) wb.hosts[i].opacity=100;
 wb.alive=TRUE;wb.tray_enabled=TRUE;wb.marks_preserve=TRUE; workbench_epoch++; wb.symbol_key_signal=g_signal_connect(geany_object,"key-press",G_CALLBACK(symbol_key_global),NULL);insight_window_list_init(window_list_preview);
 GtkWidget *existing=ui_lookup_widget(main_widgets.window,"vpaned1");
 GtkWidget *left_split=ui_lookup_widget(main_widgets.window,"hpaned1");gtk_container_child_set(GTK_CONTAINER(left_split),gtk_paned_get_child1(GTK_PANED(left_split)),"resize",FALSE,"shrink",TRUE,NULL);gtk_notebook_set_scrollable(GTK_NOTEBOOK(main_widgets.sidebar_notebook),TRUE);for(gint i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_widgets.sidebar_notebook));i++) { GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook),i),*label=gtk_notebook_get_tab_label(GTK_NOTEBOOK(main_widgets.sidebar_notebook),page);if(GTK_IS_LABEL(label)) { gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(label),1); } }
 GtkWidget *sidebar=gtk_paned_get_child1(GTK_PANED(left_split));g_object_ref(sidebar);gtk_container_remove(GTK_CONTAINER(left_split),sidebar);GtkWidget *sidebar_host=narrow_panel_host(sidebar);gtk_widget_set_name(sidebar_host,"insight-sidebar-width-host");gtk_paned_pack1(GTK_PANED(left_split),sidebar_host,FALSE,TRUE);g_object_bind_property(sidebar,"visible",sidebar_host,"visible",G_BINDING_SYNC_CREATE);g_object_unref(sidebar);
 GtkWidget *parent=gtk_widget_get_parent(existing); gint position=0;
 gtk_container_child_get(GTK_CONTAINER(parent),existing,"position",&position,NULL);
 wb.usage_paths=g_ptr_array_new_with_free_func(location_free);
 wb.info_parse_local=TRUE;wb.context_visible=TRUE; wb.prefs_path=g_build_filename(app->configdir,"insight","workbench.conf",NULL);
 wb.watch=insight_workspace_watch_new(1500,workspace_changed,NULL);
 wb.membership=insight_project_files_new();wb.master_sync=insight_master_sync_new(3000,master_membership_applied,master_sync_report,NULL);InsightMasterTypes *master_types=insight_languages_master_types();insight_master_sync_set_types(wb.master_sync,master_types);insight_master_types_free(master_types); wb.scan_members=g_ptr_array_new_with_free_func(g_free); wb.scan_files=g_ptr_array_new_with_free_func(g_free); wb.scan_trees=g_ptr_array_new_with_free_func(g_free); wb.scan_seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 wb.frames=g_ptr_array_new_with_free_func(frame_free); wb.files=g_ptr_array_new_with_free_func(g_free);
 wb.declarations=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,outline_free);wb.base=insight_index_new(); wb.index=insight_index_new(); mark_index_dirty();
 wb.file_stamps=insight_project_db_copy_table(NULL);wb.directory_stamps=insight_project_db_copy_table(NULL);wb.disk_calls=insight_index_new();wb.disk_declarations=insight_index_new();
 wb.definitions=g_ptr_array_new_with_free_func(location_free);
 wb.history=g_ptr_array_new_with_free_func(location_free); wb.history_position=-1;
 wb.project_notebook=gtk_notebook_new(); g_signal_connect(wb.project_notebook,"switch-page",G_CALLBACK(project_panel_changed),NULL); gtk_notebook_set_scrollable(GTK_NOTEBOOK(wb.project_notebook),TRUE);
 create_browser(); create_files_browser(); create_project_tree(); create_usage_browser(); create_categories(); create_file_types(); create_notes(); create_local_browser();
 wb.pane=gtk_paned_new(GTK_ORIENTATION_VERTICAL);
 wb.box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
 wb.title=gtk_label_new("");gtk_widget_set_name(wb.title,"insight-context-location");gtk_label_set_selectable(GTK_LABEL(wb.title),TRUE);g_signal_connect(wb.title,"populate-popup",G_CALLBACK(context_location_popup),NULL);
 gtk_label_set_xalign(GTK_LABEL(wb.title),0.0f);
 GtkWidget *header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*open=gtk_button_new_with_label("Open source");
 GtkWidget *folder=gtk_button_new_with_label("Source folder…"),*cancel=gtk_button_new_with_label("Cancel scan");
 g_signal_connect(folder,"clicked",G_CALLBACK(choose_root),NULL); g_signal_connect(cancel,"clicked",G_CALLBACK(cancel_scan),NULL);
 GtkWidget *project_controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
 GtkWidget *reindex=gtk_button_new_with_label("Reindex"); g_signal_connect(reindex,"clicked",G_CALLBACK(reindex_root),NULL);
 gtk_box_pack_start(GTK_BOX(project_controls),reindex,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(project_controls),folder,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(project_controls),cancel,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(wb.browser),project_controls,FALSE,FALSE,0); gtk_box_reorder_child(GTK_BOX(wb.browser),project_controls,0); gtk_widget_show_all(project_controls);
 GtkWidget *back=gtk_button_new_with_label("←"),*forward=gtk_button_new_with_label("→");
 wb.back=back; wb.forward=forward; wb.open=open;
 gtk_widget_set_tooltip_text(back,"Previous Context location");
 gtk_widget_set_tooltip_text(forward,"Next Context location");
 gtk_widget_set_tooltip_text(open,"Open the preview caret line in the editor (Enter)");
 navigation_state();
 g_signal_connect(back,"clicked",G_CALLBACK(history_move),GINT_TO_POINTER(-1));
 g_signal_connect(forward,"clicked",G_CALLBACK(history_move),GINT_TO_POINTER(1));

 gtk_box_pack_start(GTK_BOX(header),wb.title,TRUE,TRUE,4);gtk_label_set_ellipsize(GTK_LABEL(wb.title),PANGO_ELLIPSIZE_MIDDLE);
 wb.follow=gtk_toggle_button_new();
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE);
 g_signal_connect(wb.follow,"toggled",G_CALLBACK(follow_changed),NULL);

 wb.wrap=gtk_toggle_button_new();
 g_signal_connect(wb.wrap,"toggled",G_CALLBACK(wrap_changed),NULL);

 GtkWidget *copy=gtk_button_new_with_label("Copy location");
 gtk_widget_set_tooltip_text(copy,"Copy the preview caret address as file:line");
 g_signal_connect(copy,"clicked",G_CALLBACK(copy_location),NULL);

 g_signal_connect(open,"clicked",G_CALLBACK(open_context),NULL);
 gtk_box_pack_start(GTK_BOX(wb.box),header,FALSE,FALSE,2);
 wb.choices=gtk_combo_box_text_new();
 g_signal_connect(wb.choices,"changed",G_CALLBACK(definition_changed),NULL);
 GtkWidget *definitions_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*definitions_label=gtk_label_new("Definitions:");gtk_widget_set_name(definitions_label,"insight-context-definitions-label");gtk_widget_set_name(wb.choices,"insight-context-definitions");gtk_widget_set_tooltip_text(wb.choices,"Indexed definitions compatible with the source language");gtk_box_pack_start(GTK_BOX(definitions_row),definitions_label,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(definitions_row),wb.choices,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(wb.box),definitions_row,FALSE,FALSE,0);
 wb.context_definitions_row=definitions_row;gtk_widget_set_name(definitions_row,"insight-context-definitions-row");gtk_widget_show_all(definitions_row);gtk_widget_set_no_show_all(definitions_row,TRUE);gtk_widget_hide(definitions_row);
 create_context_find();
 wb.preview=SCINTILLA(scintilla_new()); gtk_widget_set_name(GTK_WIDGET(wb.preview),"insight-context-code");
 sci_set_codepage(wb.preview,SC_CP_UTF8);
 SSM(wb.preview,SCI_SETUNDOCOLLECTION,0,0);
 SSM(wb.preview,SCI_SETMARGINWIDTHN,0,0); SSM(wb.preview,SCI_SETMARGINWIDTHN,2,0);
 SSM(wb.preview,SCI_SETWRAPMODE,SC_WRAP_NONE,0);
 gtk_widget_set_direction(GTK_WIDGET(wb.preview),GTK_TEXT_DIR_LTR);
 g_signal_connect(wb.preview,"button-press-event",G_CALLBACK(preview_button),NULL);
 g_signal_connect(wb.preview,"motion-notify-event",G_CALLBACK(preview_motion),NULL);
 g_signal_connect(wb.preview,"sci-notify",G_CALLBACK(preview_notify),NULL);
 g_signal_connect(wb.preview,"key-press-event",G_CALLBACK(preview_key),NULL);
 gtk_widget_set_tooltip_text(GTK_WIDGET(wb.preview),"Single click / F12: definition in Context; double click / Enter: open this line in editor");
 GtkWidget *content=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0),*rail=gtk_box_new(GTK_ORIENTATION_VERTICAL,0),*rail_scroll=gtk_scrolled_window_new(NULL,NULL);
 g_object_set_data(G_OBJECT(wb.box),"insight-context-rail",rail);gtk_widget_set_name(rail,"insight-context-tools");gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rail_scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(rail_scroll),rail);
 GtkWidget *find_toggle=gtk_toggle_button_new(),*definitions_toggle=gtk_toggle_button_new();wb.context_find_toggle=find_toggle;
 context_icon(back,"go-previous-symbolic","Previous Context location","insight-context-back");context_icon(forward,"go-next-symbolic","Next Context location","insight-context-forward");
 context_icon(open,"document-open-symbolic","Open the preview caret line in the editor (Enter)","insight-context-open");
 context_icon(wb.follow,"find-location-symbolic","Follow cursor — update Context from the editor caret","insight-context-follow");
 context_icon(wb.wrap,"format-justify-fill-symbolic","Wrap long lines in Context","insight-context-wrap");context_icon(copy,"edit-copy-symbolic","Copy the preview caret address as file:line","insight-context-copy");
 context_icon(find_toggle,"edit-find-symbolic","Show or hide Context search (Ctrl+F)","insight-context-search-toggle");
 context_icon(definitions_toggle,"view-list-symbolic","Show or hide indexed definition candidates","insight-context-definitions-toggle");
 g_signal_connect(find_toggle,"toggled",G_CALLBACK(context_tools_toggle),wb.context_find_row);g_signal_connect(definitions_toggle,"toggled",G_CALLBACK(context_tools_toggle),definitions_row);
 GtkWidget *commands[]={back,forward,open,find_toggle,definitions_toggle,wb.follow,wb.wrap,copy};for(guint i=0;i<G_N_ELEMENTS(commands);i++) gtk_box_pack_start(GTK_BOX(rail),commands[i],FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(rail),insight_panel_help_button("panel-context.md"),FALSE,FALSE,0);
 /* Metadata and preview share the column to the left of the tool rail. */
 GtkWidget *source_column=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
 GtkWidget *metadata[]={header,definitions_row,wb.context_find_row};
 for(guint i=0;i<G_N_ELEMENTS(metadata);i++) { g_object_ref(metadata[i]);gtk_container_remove(GTK_CONTAINER(wb.box),metadata[i]);gtk_box_pack_start(GTK_BOX(source_column),metadata[i],FALSE,FALSE,0);g_object_unref(metadata[i]); }
 GList *choice_cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(wb.choices));for(GList *p=choice_cells;p;p=p->next) if(GTK_IS_CELL_RENDERER_TEXT(p->data)) g_object_set(p->data,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,"width-chars",12,NULL);g_list_free(choice_cells);
 gtk_box_pack_start(GTK_BOX(source_column),GTK_WIDGET(wb.preview),TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(content),source_column,TRUE,TRUE,0);gtk_box_pack_end(GTK_BOX(content),rail_scroll,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(wb.box),content,TRUE,TRUE,0);
 gtk_widget_set_size_request(wb.box,-1,0);
 /* The two side shelves surround the entire editor/Context column.
  * Keep the original split widgets so saved widths and Context height work. */
 GtkWidget *editor_book=gtk_paned_get_child2(GTK_PANED(left_split));
 g_object_ref(existing);g_object_ref(left_split);g_object_ref(editor_book);
 gtk_container_remove(GTK_CONTAINER(parent),existing);
 gtk_container_remove(GTK_CONTAINER(existing),left_split);
 gtk_container_remove(GTK_CONTAINER(left_split),editor_book);
 /* EN: Keep native editor tabs at their minimum inside a clipped host when the central column collapses.
  * RU: Сохраняет минимальный размер вкладок редактора внутри обрезаемой области при сжатии центра. */
 GtkWidget *editor_host = narrow_panel_host(editor_book);
 gtk_widget_set_name(editor_host, "insight-editor-notebook-host");
 gtk_widget_show(gtk_bin_get_child(GTK_BIN(editor_host)));
 g_object_bind_property(editor_book, "visible", editor_host, "visible", G_BINDING_SYNC_CREATE);
 gtk_paned_pack1(GTK_PANED(existing), editor_host, TRUE, TRUE);
 GtkWidget *source_width_host=g_object_new(insight_width_host_get_type(),NULL);gtk_widget_set_name(source_width_host,"insight-source-width-host");gtk_container_add(GTK_CONTAINER(source_width_host),existing);gtk_paned_pack2(GTK_PANED(left_split),source_width_host,TRUE,TRUE);
 /* Clip the message notebook at its native minimum instead of allocating
  * its tabs/scrollbars an invalid height when the bottom shelf is collapsed. */
 GtkWidget *messages=gtk_paned_get_child2(GTK_PANED(existing));g_object_ref(messages);gtk_container_remove(GTK_CONTAINER(existing),messages);
 GtkWidget *message_host=narrow_panel_host(messages);gtk_widget_set_name(message_host,"insight-message-height-host");g_object_set_data(G_OBJECT(message_host),"insight-collapse-height",GINT_TO_POINTER(1));
 gtk_paned_pack2(GTK_PANED(existing),message_host,FALSE,TRUE);g_object_bind_property(messages,"visible",message_host,"visible",G_BINDING_SYNC_CREATE);gtk_widget_show(message_host);g_object_unref(messages);
 g_object_unref(editor_book);g_object_unref(existing);
 wb.right_pane=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
 /* Wide editor/message controls must not collapse the Project pane. */
 GtkWidget *width_host=g_object_new(insight_width_host_get_type(),NULL);gtk_container_add(GTK_CONTAINER(width_host),left_split);gtk_paned_pack1(GTK_PANED(wb.right_pane),width_host,TRUE,TRUE);g_object_unref(left_split);
 wb.project_host=narrow_panel_host(wb.project_notebook);gtk_widget_set_name(wb.project_host,"insight-project-width-host");g_object_bind_property(wb.project_notebook,"visible",wb.project_host,"visible",G_BINDING_SYNC_CREATE);gtk_paned_pack2(GTK_PANED(wb.right_pane),wb.project_host,FALSE,TRUE);
 gtk_widget_set_size_request(wb.project_notebook,0,-1);
 gtk_widget_set_name(wb.right_pane,"insight-project-split"); gtk_widget_set_name(wb.project_notebook,"insight-project-notebook");
 wb.initial_right_split=-2;g_signal_connect(wb.right_pane,"size-allocate",G_CALLBACK(project_split_allocated),NULL);gtk_paned_set_position(GTK_PANED(wb.right_pane),850);
 gtk_paned_pack1(GTK_PANED(wb.pane),wb.right_pane,TRUE,FALSE);
 gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),wb.box,gtk_label_new("Context")); gtk_widget_show_all(wb.box);
 gtk_box_pack_start(GTK_BOX(parent),wb.pane,TRUE,TRUE,0);
 gtk_box_reorder_child(GTK_BOX(parent),wb.pane,position);
 gtk_paned_set_position(GTK_PANED(wb.pane),430);
 gtk_widget_show_all(wb.pane);
 create_search_toolbars();
 const gchar *list_titles[]={"File Symbols","Project Symbols","Project Files","Relations","Relations tree"};
 GtkWidget *list_views[]={wb.local_view,wb.symbol_view,wb.file_view,wb.usage_view,GTK_WIDGET(insight_relation_tree_view(wb.relation_tree))};for(guint i=0;i<G_N_ELEMENTS(list_views);i++) { insight_list_tools_attach(GTK_TREE_VIEW(list_views[i]));g_object_set_data(G_OBJECT(list_views[i]),"insight-list-title",(gpointer)list_titles[i]); }
 restore_workbench_settings(); restore_search_settings();
 create_native_lua();
 const char *signals[]={"document-open","document-save","document-activate","document-close"};
 const char *events[]={"document_open","document_save","document_activate","document_close"};
 for(guint i=0;i<4;i++) wb.event_handlers[i]=g_signal_connect_after(geany_object,signals[i],G_CALLBACK(native_document_event),(gpointer)events[i]);
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-editor-menu",main_widgets.editor_menu);GtkWidget *language_binding=gtk_menu_item_new_with_label("File language / profile…");gtk_widget_set_name(language_binding,"insight-language-binding-command");g_signal_connect(language_binding,"activate",G_CALLBACK(insight_languages_show),GINT_TO_POINTER(1));gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),language_binding);gtk_widget_show(language_binding);GtkWidget *filetype_options=gtk_menu_item_new_with_label("File Type Options…");gtk_widget_set_name(filetype_options,"insight-filetype-options-command");g_signal_connect(filetype_options,"activate",G_CALLBACK(insight_languages_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),filetype_options);gtk_widget_show(filetype_options);create_workbench_menus(); arrange_main_menus(); create_browse_toolbar(); create_help_menu();editor_menu_group(main_widgets.editor_menu,NULL);g_signal_connect_after(main_widgets.editor_menu,"show",G_CALLBACK(editor_menu_group),NULL);insight_menu_icons(ui_lookup_widget(main_widgets.window,"menubar1"));insight_menu_icons(main_widgets.editor_menu);register_panels();
 insight_remote_ui_init(remote_workspace);
 wb.project_handlers[0]=g_signal_connect_after(geany_object,"project-open",G_CALLBACK(native_project_update),GINT_TO_POINTER(1));
 wb.project_handlers[1]=g_signal_connect_after(geany_object,"project-save",G_CALLBACK(native_project_update),NULL);
 wb.project_handlers[2]=g_signal_connect_after(geany_object,"project-close",G_CALLBACK(native_project_close),NULL);
 native_project_update(NULL,NULL,GINT_TO_POINTER(1));
 if(wb.restore_float) { gboolean visible=gtk_widget_get_visible(wb.project_notebook); float_project(NULL,NULL); gtk_widget_set_visible(wb.project_notebook,visible); gtk_widget_set_visible(wb.float_window,visible); }
 InsightLayout session_groups={0};
 for(guint i=0;i<wb.host_count;i++) { PanelHost *host=&wb.hosts[i];session_groups.groups[i]=host->saved_group;session_groups.group_order[i]=host->saved_group_order;session_groups.group_active[i]=host->saved_group_active;session_groups.opacity[i]=host->opacity;if(host->restore) { session_groups.values[IL_DETACHED]|=1<<i;if(host->minimized) session_groups.values[IL_HOST_MINIMIZED]|=1<<i;else if(host->visible) session_groups.values[IL_HOST_VISIBLE]|=1<<i; } }
 if(!insight_layout_groups_valid(&session_groups)) { memset(session_groups.groups,0,sizeof session_groups.groups);memset(session_groups.group_order,0,sizeof session_groups.group_order);memset(session_groups.group_active,0,sizeof session_groups.group_active); }
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].restore) detach_panel(i);
 panel_groups_apply(&session_groups);
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window) { if(session_groups.values[IL_HOST_MINIMIZED]&(1<<i)) panel_minimize(i);else gtk_widget_set_visible(wb.hosts[i].window,(session_groups.values[IL_HOST_VISIBLE]&(1<<i))!=0); }
 GtkWidget *side_books[]={main_widgets.sidebar_notebook,wb.project_notebook};
 for(guint i=0;i<G_N_ELEMENTS(side_books);i++) {GtkWidget *tabs=insight_side_tabs_wrap(GTK_NOTEBOOK(side_books[i]),insight_tr);if(tabs) insight_i18n_on_refresh(G_OBJECT(tabs),insight_side_tabs_refresh,tabs);}
 gtk_style_context_add_class(gtk_widget_get_style_context(main_widgets.sidebar_notebook),"insight-panel-font");gtk_style_context_add_class(gtk_widget_get_style_context(wb.project_notebook),"insight-panel-font");for(guint n=0;n<wb.host_count;n++) if(wb.hosts[n].page && wb.hosts[n].page!=wb.box) gtk_style_context_add_class(gtk_widget_get_style_context(wb.hosts[n].page),"insight-panel-font");panel_font_watch(main_widgets.sidebar_notebook,NULL);panel_font_watch(wb.project_notebook,NULL);insight_workbench_panel_font();
 insight_workbench_set_independent(insight_appearance_independent());
 if(wb.root && !wb.scan && !wb.restore_worker && !wb.report_scan_completed) start_scan(wb.root);
 /* Notebook stack minima include inactive pages as well as Context. Keep
  * auxiliary rows from forcing the entire source workspace wider than its slot. */
 for(gint i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(main_widgets.message_window_notebook));i++) constrain_message_width(gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),i));
 constrain_message_width(wb.box);
 g_signal_connect(wb.right_pane,"notify::position",G_CALLBACK(panel_position_guard),NULL);g_signal_connect(ui_lookup_widget(main_widgets.window,"hpaned1"),"notify::position",G_CALLBACK(panel_position_guard),NULL);g_signal_connect(existing,"notify::position",G_CALLBACK(panel_position_guard),NULL);
 ui_sidebar_show_hide();
 if(ui_prefs.msgwindow_visible && !g_file_test(wb.prefs_path,G_FILE_TEST_EXISTS)) reveal_context();
 insight_workbench_refresh();
}
static gboolean accelerator_closure_matches(GtkAccelKey *key,GClosure *closure,gpointer data) { (void)key; return closure==data; }
static void remove_group_accelerators(GtkWidget *widget,GtkAccelGroup *group) {
 if(GTK_IS_MENU_ITEM(widget)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget)); if(menu) remove_group_accelerators(menu,group); }
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget)); for(GList *p=children;p;p=p->next) remove_group_accelerators(p->data,group); g_list_free(children); }
 GList *closures=gtk_widget_list_accel_closures(widget);
 for(GList *p=closures;p;p=p->next) { GtkAccelKey *key=gtk_accel_group_find(group,accelerator_closure_matches,p->data); if(key) gtk_widget_remove_accelerator(widget,group,key->accel_key,key->accel_mods); }
 g_list_free(closures);
}
void insight_workbench_finalize(void) {
 insight_clang_ui_finalize();
 palette_restore_now();
 if(palette.view_timer) {g_source_remove(palette.view_timer);palette.view_timer=0;}g_clear_pointer(&palette.views,g_ptr_array_unref);
 if(wb.main_apply_source) { g_source_remove(wb.main_apply_source);wb.main_apply_source=0; }
 for(guint i=0;i<G_N_ELEMENTS(wb.main_geometry_handlers);i++) if(wb.main_geometry_handlers[i]) { g_signal_handler_disconnect(main_widgets.window,wb.main_geometry_handlers[i]);wb.main_geometry_handlers[i]=0; }
 if(wb.symbol_key_signal) { g_signal_handler_disconnect(geany_object,wb.symbol_key_signal); wb.symbol_key_signal=0; }
 if(symbol_info) gtk_widget_destroy(symbol_info->window);
 insight_window_list_finalize();
 insight_search_links_free(wb.search_links);wb.search_links=NULL;
 insight_workbench_bookmarks_changed();
 if(wb.bookmark_sync_timer) { g_source_remove(wb.bookmark_sync_timer);wb.bookmark_sync_timer=0; }
 if(wb.bookmark_window) gtk_widget_destroy(wb.bookmark_window);
 g_clear_pointer(&wb.bookmark_handles,g_hash_table_destroy);
 wb.alive=FALSE;hdl_cancel_colors();if(wb.type_panel.timer) { g_source_remove(wb.type_panel.timer);wb.type_panel.timer=0; }if(wb.category_panel.timer) { g_source_remove(wb.category_panel.timer);wb.category_panel.timer=0; }if(wb.tree_filter_idle) { g_source_remove(wb.tree_filter_idle);wb.tree_filter_idle=0; }if(wb.file_event_idle) { g_source_remove(wb.file_event_idle);wb.file_event_idle=0; } if(wb.preview_click_timer) { g_source_remove(wb.preview_click_timer);wb.preview_click_timer=0; } stop_definition(); stop_file_preview(); mark_index_dirty();
 insight_remote_ui_finalize();
 save_search_settings(); save_workbench_settings();bookmark_session_finish(); dock_all_panels(); dock_project(NULL,NULL);
 insight_disk_browser_shutdown(wb.disk_browser);
 for(guint i=0;i<wb.host_count;i++) g_free(wb.hosts[i].label);
 for(guint i=0;i<3;i++) if(wb.project_handlers[i]) g_signal_handler_disconnect(geany_object,wb.project_handlers[i]);
 g_free(wb.geany_root);
 for(guint i=0;i<4;i++) if(wb.event_handlers[i]) g_signal_handler_disconnect(geany_object,wb.event_handlers[i]);
 if(wb.lua_accels) { remove_group_accelerators(main_widgets.window,wb.lua_accels); gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.lua_accels); g_object_unref(wb.lua_accels); }
 if(wb.navigation_accels) { remove_group_accelerators(main_widgets.window,wb.navigation_accels); gtk_window_remove_accel_group(GTK_WINDOW(main_widgets.window),wb.navigation_accels); g_object_unref(wb.navigation_accels); }
 if(wb.css) { gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(wb.css)); g_object_unref(wb.css); }
 insight_extensions_free(wb.extensions);
 if(wb.refresh) g_source_remove(wb.refresh);
 if(wb.panel_idle) g_source_remove(wb.panel_idle);
 if(wb.index_event_idle) g_source_remove(wb.index_event_idle);
 g_clear_pointer(&wb.index_event_paths,g_hash_table_destroy);
 g_clear_pointer(&wb.index_event_names,g_hash_table_destroy);
 if(wb.symbols) symbol_list_stop(symbol_list_state(wb.symbols));
 if(wb.file_store) symbol_list_stop(symbol_list_state(wb.file_store));
 if(wb.local_symbols) symbol_list_stop(symbol_list_state(wb.local_symbols));
 g_free(wb.prefs_path);g_free(wb.project_page_root);g_free(wb.project_page_profile);
 stop_usage(); g_ptr_array_free(wb.usage_paths,TRUE);
 g_free(wb.search_state_path); g_ptr_array_free(wb.search_files,TRUE); g_ptr_array_free(wb.search_history,TRUE); g_object_unref(wb.search_matches);
 if(wb.watch_idle) g_source_remove(wb.watch_idle);
 insight_workspace_watch_free(wb.watch);insight_master_sync_free(wb.master_sync);wb.master_sync=NULL;
 stop_scan(); g_ptr_array_free(wb.scan_members,TRUE); insight_project_files_free(wb.membership); g_free(wb.membership_project); g_ptr_array_free(wb.scan_files,TRUE); g_ptr_array_free(wb.scan_trees,TRUE); g_hash_table_destroy(wb.scan_seen); g_ptr_array_free(wb.frames,TRUE); g_ptr_array_free(wb.files,TRUE); g_free(wb.root); g_free(wb.cache_path);g_free(wb.db_identity);g_free(wb.db_policy);g_free(wb.restore_attempt_identity);g_hash_table_unref(wb.file_stamps);g_hash_table_unref(wb.directory_stamps);retire_index(wb.disk_calls);retire_index(wb.disk_declarations);retire_index(wb.scan_backup);
 g_free(wb.editor_find_query); g_free(wb.editor_find_digest);
 g_free(wb.follow_word); g_free(wb.relation_word);
 g_free(wb.source_path); g_free(wb.source_digest); g_free(wb.find_last_query); g_free(wb.find_last_digest);
 if(wb.preview_links_idle) { g_source_remove(wb.preview_links_idle); wb.preview_links_idle=0; }
 preview_scopes_clear();g_clear_pointer(&wb.preview_links_text,g_free);g_clear_pointer(&wb.preview_code_mask,g_free);g_clear_pointer(&wb.preview_links_path,g_free); g_clear_pointer(&wb.preview_names,g_hash_table_destroy);g_clear_pointer(&wb.preview_candidates,g_hash_table_destroy);
 g_hash_table_unref(wb.declarations);insight_index_free(wb.base); insight_index_free(wb.index);
 if(wb.symbols) g_object_unref(wb.symbols);
 if(wb.local_symbols) g_object_unref(wb.local_symbols);
 if(wb.project_tree) g_object_unref(wb.project_tree);
 if(wb.categories) g_object_unref(wb.categories);
 if(wb.file_types) g_object_unref(wb.file_types);
 if(wb.marks_store) g_object_unref(wb.marks_store);
 if(wb.clips_store) g_object_unref(wb.clips_store);
 g_clear_pointer(&wb.marks_session_stores,g_hash_table_destroy);insight_notes_free(wb.project_marks);g_free(wb.project_marks_path);g_free(wb.project_marks_root);insight_notes_free(wb.notes);insight_notes_free(wb.project_notes);g_free(wb.project_notes_path);g_free(wb.template_prefix); g_free(wb.notes_path); g_free(wb.clips_language);
 if(wb.file_store) g_object_unref(wb.file_store);
 if(wb.relation_history) g_ptr_array_unref(wb.relation_history);
 if(wb.relation_future) g_ptr_array_unref(wb.relation_future);
 if(wb.matches) g_object_unref(wb.matches);
 if(wb.history) g_ptr_array_free(wb.history,TRUE);
 if(wb.definitions) g_ptr_array_free(wb.definitions,TRUE);
 insight_languages_finalize();insight_i18n_finalize();memset(&wb,0,sizeof wb);
}

void insight_workbench_languages_changed(void) { if(!wb.alive) return;wb.preview_styles_ready=FALSE;wb.preview_links_valid=FALSE;InsightMasterTypes *types=insight_languages_master_types();insight_master_sync_set_types(wb.master_sync,types);insight_master_types_free(types);stop_definition();wb.follow_doc=0;if(wb.root) { wb.force_parse=TRUE;gchar *path=g_strdup(wb.root);start_scan(path);g_free(path); }else mark_index_dirty();stop_usage();browser_filter(NULL,NULL);files_filter(NULL,NULL);categories_filter(NULL,NULL);preview_links();insight_workbench_refresh(); }

void insight_workbench_locale_changed(void) { if(!wb.alive) return;for(guint i=0;i<wb.host_count;i++) { panel_controls_update(&wb.hosts[i]);if(panel_window_owner(wb.hosts[i].window)==i) panel_window_refresh(wb.hosts[i].window); }if(wb.disk_browser) insight_disk_browser_locale_changed(wb.disk_browser);if(wb.relation_tree) insight_relation_tree_locale_changed(wb.relation_tree);if(wb.type_panel.status) grouped_count(&wb.type_panel,wb.type_panel.shown,wb.type_panel.total);if(wb.category_panel.status) { grouped_count(&wb.category_panel,wb.category_panel.shown,wb.category_panel.total);categories_filter(NULL,NULL); }if(wb.tree_status) folder_count(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wb.tree_status),"insight-folder-count")),GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wb.tree_status),"insight-folder-total")));if(wb.file_status) { gchar *text=insight_i18n_printf(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wb.file_status),"insight-files-more")) ? "Files: %u+" : "Files: %u",GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wb.file_status),"insight-files-count")));gtk_label_set_text(GTK_LABEL(wb.file_status),text);g_free(text); }}


gboolean insight_workbench_focus_file_symbols(void) {
 if(!wb.alive || !wb.local_entry) return FALSE;
 ui_prefs.sidebar_visible=TRUE;interface_prefs.sidebar_symbol_visible=TRUE;ui_sidebar_show_hide();focus_widget_panel(wb.local_entry);
 GtkWidget *target=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode))==0 ? gtk_bin_get_child(GTK_BIN(ui_lookup_widget(main_widgets.window,"scrolledwindow2"))) : wb.local_view;
 if(target && gtk_widget_get_sensitive(target)) { gtk_widget_grab_focus(target); }
 return TRUE;
}
