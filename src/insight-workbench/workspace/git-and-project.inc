/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Git project actions and workspace tools.
 * RU: Действия Git и инструменты проекта. */

static void git_project_refresh(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(app->project) native_project_update(NULL,NULL,GINT_TO_POINTER(1));
}
static void vivado_synchronize(GtkMenuItem *item,gpointer data) {
 (void)item;(void)data;
 if(!app->project || !wb.workspace_from_project || !wb.root) {
  ui_set_statusbar(TRUE,"Open a linked Vivado project first");
  return;
 }
 gchar *filename=utils_get_locale_from_utf8(app->project->file_name);
 GError *error=NULL;
 InsightProjectFiles *policy=insight_vivado_sync(filename,&error);
 g_free(filename);
 if(policy) {
  gchar *root=project_get_base_path();
  hdl_cancel_colors();
  wb.force_parse=TRUE;
  membership_apply_snapshot(policy,root);
  insight_master_sync_set(wb.master_sync,app->project->file_name,root,wb.membership);
  hdl_schedule_colors();
  g_free(root);
  insight_project_files_free(policy);
  ui_set_statusbar(TRUE,"Synchronized Vivado sources and HDL settings");
 } else ui_set_statusbar(TRUE,"Vivado synchronization failed: %s",error ? error->message : "unknown error");
 g_clear_error(&error);
}
static void project_synchronize(GtkMenuItem *item,gpointer data) {
 (void)item;if(!wb.root) { ui_set_statusbar(TRUE,"Open a project first");return; }wb.force_parse=GPOINTER_TO_INT(data)!=0;wb.cache_validation=FALSE;start_scan(wb.root);
}
static void navigate_relations(GtkMenuItem *item,gpointer data) {
 (void)item;GeanyDocument *doc=document_get_current();if(!doc) {ui_set_statusbar(TRUE,"%s",insight_tr("Open a source file first"));return;}
 gchar word[1025]={0};editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);gint requested=GPOINTER_TO_INT(data);if(!*word && requested!=12) {ui_set_statusbar(TRUE,"%s",insight_tr("Place the caret on a symbol first"));return;}
 if(requested==12) *word=0;if((requested==0 || requested==7) && insight_clang_ui_result(doc->editor->sci)) data=GINT_TO_POINTER(requested==0 ? 8:10);
 gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"list");relation_stack_changed(G_OBJECT(wb.relation_stack),NULL,NULL);
 wb.relation_setting=TRUE;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_buffer),FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),GPOINTER_TO_INT(data));gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),word);gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry),"");wb.relation_setting=FALSE;relation_controls(NULL,NULL);focus_widget_panel(wb.usage_entry);
}
/* Move original command widgets, preserving actions, native shared submenus,
 * accelerators, checked state and owner lookup data. Late popup additions are
 * categorized on the next show; unknown plugin commands remain accessible. */
static void editor_search_command(GtkMenuItem *item,gpointer data) {(void)item;GtkWidget *target=ui_lookup_widget(main_widgets.window,data);if(target) gtk_menu_item_activate(GTK_MENU_ITEM(target));}
static void editor_menu_group(GtkWidget *root,gpointer unused) {
 (void)unused;if(g_object_get_data(G_OBJECT(root),"insight-popup-grouping")) return;g_object_set_data(G_OBJECT(root),"insight-popup-grouping",GINT_TO_POINTER(1));
 GtkWidget **groups=g_object_get_data(G_OBJECT(root),"insight-popup-groups");
 if(!groups) {groups=g_new0(GtkWidget *,6);const gchar *labels[]={"Search / Replace","Source tools","File operations","AI Assistant","View","Navigation"};const gchar *ids[]={"search","source","file","ai","view","navigation"};
  for(guint i=0;i<6;i++) {GtkWidget *item=gtk_menu_item_new_with_label(insight_tr(labels[i]));groups[i]=gtk_menu_new();gchar *name=g_strdup_printf("insight-editor-%s-group",ids[i]);gtk_widget_set_name(item,name);g_free(name);insight_i18n_key(G_OBJECT(item),"label",labels[i]);g_object_set_data(G_OBJECT(item),"insight-popup-group",GINT_TO_POINTER(1));gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),groups[i]);gtk_menu_shell_append(GTK_MENU_SHELL(root),item);gtk_widget_show(item);}
  const gchar *commands[]={"find1","replace1","find_in_files1"},*captions[]={"Find…","Replace…","Find in Files…"};for(guint i=0;i<3;i++) {GtkWidget *item=gtk_menu_item_new_with_label(insight_tr(captions[i]));insight_i18n_key(G_OBJECT(item),"label",captions[i]);g_signal_connect(item,"activate",G_CALLBACK(editor_search_command),(gpointer)commands[i]);gtk_menu_shell_append(GTK_MENU_SHELL(groups[0]),item);gtk_widget_show(item);}
  g_object_set_data_full(G_OBJECT(root),"insight-popup-groups",groups,g_free);
 }
 GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *history[2]={NULL,NULL},*strip=NULL;
 for(GList *p=children;p;p=p->next) {GtkWidget *item=p->data;if(!GTK_IS_MENU_ITEM(item) || g_object_get_data(G_OBJECT(item),"insight-popup-group")) continue;
  if(g_object_get_data(G_OBJECT(item),"insight-palette-action")) {strip=item;continue;}
  if(GTK_IS_SEPARATOR_MENU_ITEM(item)) {gtk_widget_set_no_show_all(item,TRUE);gtk_widget_hide(item);continue;}
  GtkAction *action=GTK_IS_ACTIVATABLE(item) ? gtk_activatable_get_related_action(GTK_ACTIVATABLE(item)):NULL;const gchar *action_name=action ? gtk_action_get_name(action):NULL;
  if(!g_strcmp0(action_name,"NavBack") || !g_strcmp0(action_name,"NavFor")) {guint side=!strcmp(action_name,"NavFor");history[side]=item;gtk_widget_set_name(item,side ? "insight-editor-forward":"insight-editor-back");continue;}
  const gchar *label=gtk_menu_item_get_label(GTK_MENU_ITEM(item)),*id=GTK_IS_BUILDABLE(item) ? gtk_buildable_get_name(GTK_BUILDABLE(item)):NULL;gchar *english=g_ascii_strdown(insight_i18n_english(label ? label:""),-1);const gchar *name=gtk_widget_get_name(item);gint group=-1;
  if(!g_strcmp0(id,"search2") || !g_strcmp0(id,"find_usage2") || !g_strcmp0(id,"find_document_usage2") || strstr(english,"search") || strstr(english,"find") || strstr(english,"replace") || strstr(english,"references") || strstr(english,"usage") || strstr(english,"look up selection") || strstr(name,"word-highlights")) group=0;
  else if(!g_strcmp0(id,"goto_tag_definition2") || strstr(english,"jump") || strstr(english,"call path") || strstr(english,"source route") || strstr(english,"go to")) group=5;
  else if(!g_strcmp0(name,"insight-debug-current-hdl") || !g_strcmp0(id,"commands1") || !g_strcmp0(id,"menu_format2") || !g_strcmp0(id,"insert1") || strstr(english,"symbol") || strstr(english,"revision") || strstr(english,"backup")) group=1;
  else if(!g_strcmp0(id,"menu_open_selected_file2") || strstr(english,"file manager") || strstr(english,"file type") || strstr(english,"file language")) group=2;
  else if(strstr(english,"ai") || strstr(name,"-ai-")) group=3;
  else if(GTK_IS_CHECK_MENU_ITEM(item) || !g_strcmp0(id,"show_menubar1") || strstr(english,"overview") || strstr(english,"statusbar")) group=4;
  if(!g_strcmp0(id,"search2")) insight_i18n_key(G_OBJECT(item),"label","More search commands");
  g_free(english);if(group>=0) {g_object_ref(item);gtk_container_remove(GTK_CONTAINER(root),item);gtk_menu_shell_append(GTK_MENU_SHELL(groups[group]),item);g_object_unref(item);}
 }
 if(strip) gtk_menu_reorder_child(GTK_MENU(root),strip,0);for(guint i=0;i<2;i++) if(history[i]) gtk_menu_reorder_child(GTK_MENU(root),history[i],(strip ? 1:0)+i);
 for(guint i=0;i<6;i++) {GList *entries=gtk_container_get_children(GTK_CONTAINER(groups[i]));gboolean visible=FALSE;for(GList *p=entries;p;p=p->next) if(!GTK_IS_SEPARATOR_MENU_ITEM(p->data) && !g_object_get_data(G_OBJECT(p->data),"insight-palette-action") && gtk_widget_get_visible(p->data)) visible=TRUE;g_list_free(entries);GtkWidget *parent=gtk_menu_get_attach_widget(GTK_MENU(groups[i]));if(parent) {gtk_widget_set_no_show_all(parent,TRUE);gtk_widget_set_visible(parent,visible);}}
 g_list_free(children);g_object_set_data(G_OBJECT(root),"insight-popup-grouping",NULL);
}
static void create_workbench_menus(void) {
 ui_statusbar_menu(ui_lookup_widget(main_widgets.window,"menu_view1_menu"),FALSE);ui_statusbar_menu(main_widgets.editor_menu,TRUE);
 insight_git_install(G_CALLBACK(git_project_refresh));
 GtkWidget *overview=gtk_check_menu_item_new_with_label("Overview"); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview),insight_overview_get_visible()); g_signal_connect(overview,"toggled",G_CALLBACK(overview_visible),NULL); gtk_container_add(GTK_CONTAINER(ui_lookup_widget(main_widgets.window,"menu_view1_menu")),overview); gtk_widget_show(overview);
 GtkWidget *project_menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"menu_project1"))),*manage=gtk_menu_item_new_with_label("Add and Remove Project Files…"); gtk_container_add(GTK_CONTAINER(project_menu),manage); g_signal_connect(manage,"activate",G_CALLBACK(manage_project_files),NULL); gtk_widget_show(manage);
 const gchar *master_labels[]={"Synchronize Master File List","Open Project Master File List"};for(guint i=0;i<2;i++) { GtkWidget *command=gtk_menu_item_new_with_label(master_labels[i]);gtk_widget_set_name(command,i ? "insight-open-master-list" : "insight-sync-master-list");gtk_menu_shell_append(GTK_MENU_SHELL(project_menu),command);g_signal_connect(command,"activate",G_CALLBACK(project_master_command),GINT_TO_POINTER(i));gtk_widget_show(command); }
 const gchar *sync_labels[]={"Synchronize Files","Rebuild Project"};for(guint n=0;n<2;n++) { GtkWidget *command=gtk_menu_item_new_with_label(sync_labels[n]);gtk_widget_set_name(command,n?"insight-project-rebuild":"insight-project-synchronize");g_signal_connect(command,"activate",G_CALLBACK(project_synchronize),GINT_TO_POINTER(n));gtk_menu_shell_append(GTK_MENU_SHELL(project_menu),command);gtk_widget_show(command); }
 GtkWidget *vivado_sync=gtk_menu_item_new_with_label(insight_tr("Synchronize from Vivado…"));
 g_signal_connect(vivado_sync,"activate",G_CALLBACK(vivado_synchronize),NULL);
 gtk_menu_shell_append(GTK_MENU_SHELL(project_menu),vivado_sync);
 gtk_widget_show(vivado_sync);
 insight_project_report_init(project_menu,capture_project_report);
 GtkWidget *menubar=ui_lookup_widget(main_widgets.window,"menubar1"),*navigate=gtk_menu_item_new_with_mnemonic("_Navigate"),*menu=gtk_menu_new();
 gtk_menu_item_set_submenu(GTK_MENU_ITEM(navigate),menu); gtk_menu_shell_insert(GTK_MENU_SHELL(menubar),navigate,4);
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-navigate",navigate);
 GtkWidget *clang_menu=insight_clang_ui_init(menu);
 append_call_paths_menu(menu,"insight-call-menu");append_call_paths_menu(main_widgets.editor_menu,"insight-call-popup");
 const gchar *route_labels[]={"Source routes / flow","Add caret to source routes"};for(guint i=0;i<2;i++) {GtkWidget *entry=gtk_menu_item_new_with_label(route_labels[i]);gtk_widget_set_name(entry,i ? "insight-route-capture-command":"insight-route-show-command");gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry);g_signal_connect(entry,"activate",G_CALLBACK(source_routes_show),GUINT_TO_POINTER(i));gtk_widget_show(entry);}
 GtkWidget *capture_route=gtk_menu_item_new_with_label("Add caret to source routes");gtk_widget_set_name(capture_route,"insight-route-capture-popup");gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),capture_route);g_signal_connect(capture_route,"activate",G_CALLBACK(source_routes_show),GINT_TO_POINTER(1));gtk_widget_show(capture_route);
 GtkWidget *clang_popup=gtk_menu_item_new_with_label(insight_tr("Clang references…"));gtk_widget_set_name(clang_popup,"insight-clang-references-popup");insight_i18n_key(G_OBJECT(clang_popup),"label","Clang references…");gtk_widget_set_sensitive(clang_popup,insight_clang_available());g_signal_connect(clang_popup,"activate",G_CALLBACK(navigate_relations),GINT_TO_POINTER(8));gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),clang_popup);gtk_widget_show(clang_popup);
 const gchar *relation_navigation[]={"Jump To Caller…","Lookup References…","Direct members (tags)","Derived types (tags)","Clang references…","Callees (Clang tree)","Callers (Clang tree)"};const gint relation_modes[]={7,0,2,5,8,9,10};for(guint n=0;n<G_N_ELEMENTS(relation_modes);n++) {GtkWidget *entry=gtk_menu_item_new_with_label(relation_navigation[n]);gchar *id=g_strdup_printf("insight-navigate-relation-%d",relation_modes[n]);gtk_widget_set_name(entry,id);g_free(id);g_signal_connect(entry,"activate",G_CALLBACK(navigate_relations),GINT_TO_POINTER(relation_modes[n]));gtk_menu_shell_append(GTK_MENU_SHELL(relation_modes[n]>=8 ? clang_menu:menu),entry);}
 GtkWidget *hdl_navigation=gtk_menu_item_new_with_label(insight_tr("HDL references (slang)…"));gtk_widget_set_name(hdl_navigation,"insight-navigate-hdl-references");insight_i18n_key(G_OBJECT(hdl_navigation),"label","HDL references (slang)…");g_signal_connect(hdl_navigation,"activate",G_CALLBACK(navigate_relations),GINT_TO_POINTER(11));gtk_menu_shell_append(GTK_MENU_SHELL(menu),hdl_navigation);
 GtkWidget *hdl_popup=gtk_menu_item_new_with_label(insight_tr("HDL references (slang)…"));gtk_widget_set_name(hdl_popup,"insight-hdl-references-popup");insight_i18n_key(G_OBJECT(hdl_popup),"label","HDL references (slang)…");g_signal_connect(hdl_popup,"activate",G_CALLBACK(navigate_relations),GINT_TO_POINTER(11));gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),hdl_popup);gtk_widget_show(hdl_popup);
 GtkWidget *hdl_diagnostics=gtk_menu_item_new_with_label(insight_tr("HDL diagnostics (slang)…"));gtk_widget_set_name(hdl_diagnostics,"insight-navigate-hdl-diagnostics");insight_i18n_key(G_OBJECT(hdl_diagnostics),"label","HDL diagnostics (slang)…");g_signal_connect(hdl_diagnostics,"activate",G_CALLBACK(navigate_relations),GINT_TO_POINTER(12));gtk_menu_shell_append(GTK_MENU_SHELL(menu),hdl_diagnostics);
 GtkWidget *hdl_diagnostic_popup=gtk_menu_item_new_with_label(insight_tr("HDL diagnostics (slang)…"));gtk_widget_set_name(hdl_diagnostic_popup,"insight-hdl-diagnostics-popup");insight_i18n_key(G_OBJECT(hdl_diagnostic_popup),"label","HDL diagnostics (slang)…");g_signal_connect(hdl_diagnostic_popup,"activate",G_CALLBACK(navigate_relations),GINT_TO_POINTER(12));gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),hdl_diagnostic_popup);gtk_widget_show(hdl_diagnostic_popup);


 GtkWidget *navigation_help=gtk_menu_item_new_with_label("Navigation Help…");g_signal_connect(navigation_help,"activate",G_CALLBACK(insight_help_show),"navigation.md");gtk_menu_shell_append(GTK_MENU_SHELL(menu),navigation_help);

 const gchar *history_labels[]={"Navigate back a location","Navigate forward a location"};for(guint i=0;i<2;i++) {GtkWidget *entry=gtk_action_create_menu_item(toolbar_get_action_by_name(i ? "NavFor":"NavBack"));gtk_menu_item_set_label(GTK_MENU_ITEM(entry),history_labels[i]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry);GtkWidget *popup=gtk_action_create_menu_item(toolbar_get_action_by_name(i ? "NavFor":"NavBack"));gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),popup); }
 window_lock_item(menu,wb.follow,"Lock Context Window","insight-context-lock-command");
 window_lock_item(menu,wb.relation_follow,"Lock Relation Window","insight-relation-lock-command");
 GtkWidget *refresh_relation=gtk_menu_item_new_with_label("Refresh Relation Window");gtk_widget_set_name(refresh_relation,"insight-relation-refresh-command");gtk_menu_shell_append(GTK_MENU_SHELL(menu),refresh_relation);g_signal_connect(refresh_relation,"activate",G_CALLBACK(relation_refresh_command),NULL);
 const gchar *view_labels[]={"Relation List / Tree View","Horizontal Relation Graph","Vertical Relation Graph"};
 const gchar *view_names[]={"insight-relation-list-command","insight-relation-horizontal-command","insight-relation-vertical-command"};
 for(guint i=0;i<G_N_ELEMENTS(view_labels);i++) { GtkWidget *item=gtk_menu_item_new_with_label(view_labels[i]);gtk_widget_set_name(item,view_names[i]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(relation_select_view),GUINT_TO_POINTER(i)); }
 GtkWidget *print_relation=gtk_menu_item_new_with_label("Print Relation Window…");gtk_widget_set_name(print_relation,"insight-relation-print-command");gtk_menu_shell_append(GTK_MENU_SHELL(menu),print_relation);g_signal_connect(print_relation,"activate",G_CALLBACK(relation_print_show),NULL);
 GtkWidget *next_view_command=gtk_menu_item_new_with_label("Next Relation Window View");gtk_widget_set_name(next_view_command,"insight-relation-next-view-command");gtk_menu_shell_append(GTK_MENU_SHELL(menu),next_view_command);g_signal_connect(next_view_command,"activate",G_CALLBACK(relation_next_view),NULL);
 const gchar *relation_reports[]={"View Relation Outline…","Create Bookmarks from Relation Items…"};for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(relation_reports[i]);gtk_widget_set_name(item,i ? "insight-relation-bookmark-command":"insight-relation-outline-command");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(relation_report_show),GUINT_TO_POINTER(i)); }

 insight_custom_commands_menu(main_widgets.tools_menu);
 GtkWidget *compare = tool_submenu("Compare / Merge");
 GtkWidget *diff_item = tool_command(compare, "Diff / Merge…", G_CALLBACK(show_diff), NULL);
 gtk_widget_set_name(diff_item, "insight-diff-menu");
 GtkWidget *directory_item = tool_command(compare, "Directory Compare…", G_CALLBACK(show_directory_compare), NULL);
 gtk_widget_set_name(directory_item, "insight-directory-menu");
 tool_help(compare, "Compare and Merge Help…", "internal-diff.md");
 tool_command(compare, "Directory Compare Help…", G_CALLBACK(insight_help_show), "directory-compare.md");
 GtkWidget *hex_item = tool_command(main_widgets.tools_menu, "Hex editor…", G_CALLBACK(show_hex), NULL);
 gtk_widget_set_name(hex_item, "insight-hex-menu");
 wb.navigation_accels=gtk_accel_group_new(); gtk_window_add_accel_group(GTK_WINDOW(main_widgets.window),wb.navigation_accels);
 GtkWidget *templates=gtk_image_menu_item_new_with_label("Code templates / Clips…"); gtk_widget_set_name(templates,"insight-templates-command"); gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(templates),insight_command_icon("snippets")); gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(templates),TRUE); gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.tools_menu),templates); g_signal_connect(templates,"activate",G_CALLBACK(focus_templates),NULL); gtk_widget_show(templates);
 create_structural_selection_menu(); insight_overview_install_commands();
 const gchar *target_labels[]={"Jump To Prototype…","Jump To Implementation…","Jump To Base Type…"},*target_ids[]={"insight-jump-prototype-command","insight-jump-implementation-command","insight-jump-base-type-command"},*target_popups[]={"insight-jump-prototype-popup","insight-jump-implementation-popup","insight-jump-base-type-popup"};for(guint i=0;i<3;i++) { GtkWidget *item=gtk_menu_item_new_with_label(target_labels[i]);gtk_widget_set_name(item,target_ids[i]);keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_JUMPPROTOTYPE+i,item);g_signal_connect(item,"activate",G_CALLBACK(show_function_target),GUINT_TO_POINTER(i+1));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);GtkWidget *popup=gtk_menu_item_new_with_label(target_labels[i]);gtk_widget_set_name(popup,target_popups[i]);g_signal_connect(popup,"activate",G_CALLBACK(show_function_target),GUINT_TO_POINTER(i+1));gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),popup); }
 GtkWidget *info=gtk_menu_item_new_with_label("Symbol Info…");gtk_widget_set_name(info,"insight-symbol-info-command");keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_SYMBOLINFO,info);g_signal_connect(info,"activate",G_CALLBACK(show_symbol_info),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),info);
 GtkWidget *lookup_options=gtk_menu_item_new_with_label("Symbol Info Lookup Options…");gtk_widget_set_name(lookup_options,"insight-symbol-info-options-command");g_signal_connect(lookup_options,"activate",G_CALLBACK(show_symbol_lookup_options),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),lookup_options);
 GtkWidget *search_menu=ui_lookup_widget(main_widgets.window,"search1_menu");
 const char *names[]={"Go to definition","Find symbol…","Find file…","Find usages…","Context back","Context forward"};
 const char *shortcuts[]={"F12","<Primary><Alt>s","<Primary><Alt>o","<Primary><Alt>r","<Primary><Alt>Left","<Primary><Alt>Right"};
 for(int i=0;i<6;i++) {
  GtkWidget *entry=gtk_menu_item_new_with_label(names[i]); gtk_container_add(GTK_CONTAINER(i>=1 && i<=3 ? search_menu : menu),entry);gtk_widget_show(entry);
  if(!i) {gtk_widget_set_name(entry,"insight-goto-definition-command");g_signal_connect(entry,"activate",G_CALLBACK(go_definition),NULL);}
  else if(i<4) g_signal_connect(entry,"activate",G_CALLBACK(focus_browser),GINT_TO_POINTER(i-1));
  else g_signal_connect(entry,"activate",G_CALLBACK(history_move),GINT_TO_POINTER(i==4 ? -1 : 1));
  guint key; GdkModifierType mods; gtk_accelerator_parse(shortcuts[i],&key,&mods); gtk_widget_add_accelerator(entry,"activate",wb.navigation_accels,key,mods,GTK_ACCEL_VISIBLE);
 }
 const char *function_names[]={"Function Up","Function Down"};
 const char *function_keys[]={"<Primary><Alt>Page_Up","<Primary><Alt>Page_Down"};
 for(guint i=0;i<2;i++) {
  GtkWidget *entry=gtk_menu_item_new_with_label(function_names[i]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry);
  g_signal_connect(entry,"activate",G_CALLBACK(function_navigation),GINT_TO_POINTER(i ? 1 : -1));
  guint key;GdkModifierType mods;gtk_accelerator_parse(function_keys[i],&key,&mods);gtk_widget_add_accelerator(entry,"activate",wb.navigation_accels,key,mods,GTK_ACCEL_VISIBLE);
 }
 const char *result_names[]={"Search results","Previous search result","Next search result"};
 for(guint i=0;i<3;i++) {
  GtkWidget *entry=gtk_menu_item_new_with_label(result_names[i]); gtk_container_add(GTK_CONTAINER(search_menu),entry);
  g_signal_connect(entry,"activate",i ? G_CALLBACK(search_result_move) : G_CALLBACK(focus_search_results),GINT_TO_POINTER(i==1 ? -1 : 1));
  if(i) gtk_widget_add_accelerator(entry,"activate",wb.navigation_accels,i==1 ? GDK_KEY_F8 : GDK_KEY_F9,GDK_SHIFT_MASK,GTK_ACCEL_VISIBLE);
  gtk_widget_show(entry);
 }
 wb.context_toggle=gtk_check_menu_item_new_with_label("Context window"); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),wb.context_visible);
 g_signal_connect(wb.context_toggle,"toggled",G_CALLBACK(context_visible),NULL); gtk_container_add(GTK_CONTAINER(ui_lookup_widget(main_widgets.window,"menu_view1_menu")),wb.context_toggle);
 gtk_widget_show_all(navigate); gtk_widget_show(wb.context_toggle);
 panel_menu(ui_lookup_widget(main_widgets.window,"menu_view1_menu"));
 gtk_widget_set_name(wb.box,"insight-context"); gtk_widget_set_name(wb.title,"insight-context-title"); gtk_widget_set_name(wb.browser,"insight-workspace");
 gtk_label_set_ellipsize(GTK_LABEL(wb.title),PANGO_ELLIPSIZE_MIDDLE);
 GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(wb.choices)); for(GList *p=cells;p;p=p->next) g_object_set(p->data,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,"max-width-chars",24,"width-chars",24,NULL); g_list_free(cells);
 gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(wb.symbol_view),SYMBOL_KIND),FALSE); gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.symbol_view),SYMBOL_PATH);
 gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.file_view),1);
 wb.css=gtk_css_provider_new(); gtk_css_provider_load_from_data(wb.css,"#insight-context { border-top: 2px solid @theme_selected_bg_color; } #insight-context-title { font-weight: bold; padding: 3px; } #insight-workspace { padding: 3px; }",-1,NULL);
 gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(wb.css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
 gtk_widget_hide(ui_lookup_widget(main_widgets.window,"menu_build1"));
 if(!g_file_test(wb.prefs_path,G_FILE_TEST_EXISTS)) {
  ui_prefs.sidebar_visible=TRUE; ui_sidebar_show_hide(); gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1")),100);
  gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),0);
 }
 ui_set_window_title(document_get_current());
}
static void save_workbench_settings(void) {
 if(!wb.prefs_path || !wb.pane) return;
 GKeyFile *settings=g_key_file_new();
 insight_overview_write(settings);insight_directory_write(settings);insight_diff_write(settings);
 grouped_write(&wb.type_panel,settings); grouped_write(&wb.category_panel,settings);
 g_key_file_set_boolean(settings,"workbench","project-templates",wb.clips_prefer_project);
 g_key_file_set_boolean(settings,"workbench","folder-follow-current-file",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.tree_follow)));
 insight_list_style_write(wb.tree_view,settings,"project-folders-style");
 insight_list_style_write(wb.marks_view,settings,"bookmarks-style");
 insight_disk_browser_write(wb.disk_browser,settings);g_key_file_set_integer(settings,"workbench","folder-view-mode",gtk_combo_box_get_active(GTK_COMBO_BOX(wb.folder_mode)));
 symbol_sort_write(settings,wb.file_store,"project-files");insight_symbol_view_columns_write(GTK_TREE_VIEW(wb.file_view),settings,"project-files");symbol_history_write(settings,wb.file_entry,"project-files");
 g_key_file_set_boolean(settings,"workbench","symbol-locals-opt-in-v1",TRUE);g_key_file_set_boolean(settings,"workbench","project-relative-path-v1",TRUE);
 symbol_history_write(settings,wb.local_entry,"file-symbols");symbol_history_write(settings,wb.symbol_entry,"project-symbols");
 insight_symbol_view_columns_write(GTK_TREE_VIEW(wb.local_view),settings,"file-symbols");insight_symbol_view_columns_write(GTK_TREE_VIEW(wb.symbol_view),settings,"project-symbols");
 symbol_sort_write(settings,wb.local_symbols,"file-symbols");symbol_sort_write(settings,wb.symbols,"project-symbols");
 for(guint i=0;i<4;i++) { gchar group[16]; g_snprintf(group,sizeof group,"layout-%c",'A'+i); insight_layout_write(settings,group,&wb.layouts[i]); }
 g_key_file_set_boolean(settings,"workbench","transparent-panels",wb.transparent_panels);g_key_file_set_boolean(settings,"workbench","tab-tray-visible",wb.tray_enabled);
 g_key_file_set_integer(settings,"workbench","version",1);
 for(guint i=0;i<wb.host_count;i++) { PanelHost *host=&wb.hosts[i]; gchar group[32]; g_snprintf(group,sizeof group,"panel-%u",i); if(host->window) { panel_geometry_capture(host); host->visible=gtk_widget_get_visible(host->window); }
  InsightLayout grouping={0};panel_group_capture(i,&grouping);g_key_file_set_integer(settings,group,"group",grouping.groups[i]);g_key_file_set_integer(settings,group,"group-order",grouping.group_order[i]);g_key_file_set_integer(settings,group,"group-active",grouping.group_active[i]);
  g_key_file_set_integer(settings,group,"version",5); g_key_file_set_integer(settings,group,"opacity",host->opacity); g_key_file_set_boolean(settings,group,"minimized",host->minimized); g_key_file_set_boolean(settings,group,"maximized",host->maximized); g_key_file_set_boolean(settings,group,"floating",host->window!=NULL); g_key_file_set_boolean(settings,group,"visible",host->visible); g_key_file_set_integer(settings,group,"x",host->x); g_key_file_set_integer(settings,group,"y",host->y); g_key_file_set_integer(settings,group,"width",host->width); g_key_file_set_integer(settings,group,"height",host->height);
 }
 g_key_file_set_boolean(settings,"workbench","project-visible",gtk_widget_get_visible(wb.project_notebook));
 g_key_file_set_boolean(settings,"workbench","floating",wb.float_window!=NULL);
 if(wb.float_window) { gtk_window_get_position(GTK_WINDOW(wb.float_window),&wb.float_x,&wb.float_y); gtk_window_get_size(GTK_WINDOW(wb.float_window),&wb.float_width,&wb.float_height); }
 g_key_file_set_integer(settings,"workbench","float-x",wb.float_x); g_key_file_set_integer(settings,"workbench","float-y",wb.float_y);
 g_key_file_set_integer(settings,"workbench","float-width",wb.float_width); g_key_file_set_integer(settings,"workbench","float-height",wb.float_height);
 g_key_file_set_integer(settings,"workbench","split",gtk_paned_get_position(GTK_PANED(wb.pane)));
 g_key_file_set_boolean(settings,"workbench","follow",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow)));
 g_key_file_set_boolean(settings,"workbench","visible",wb.context_visible);
 g_key_file_set_boolean(settings,"workbench","file-symbols-scopes",insight_symbol_view_scopes(GTK_TREE_VIEW(wb.local_view)));g_key_file_set_boolean(settings,"workbench","project-symbols-scopes",insight_symbol_view_scopes(GTK_TREE_VIEW(wb.symbol_view)));
 g_key_file_set_boolean(settings,"workbench","file-symbols-grouped",insight_symbol_view_grouped(GTK_TREE_VIEW(wb.local_view))); g_key_file_set_boolean(settings,"workbench","project-symbols-grouped",insight_symbol_view_grouped(GTK_TREE_VIEW(wb.symbol_view)));
 g_key_file_set_integer(settings,"workbench","file-symbol-types",insight_symbol_view_types_mask(GTK_TREE_VIEW(wb.local_view))); g_key_file_set_integer(settings,"workbench","project-symbol-types",insight_symbol_view_types_mask(GTK_TREE_VIEW(wb.symbol_view)));
 g_key_file_set_integer(settings,"workbench","info-source-scope",wb.info_scope);
 g_key_file_set_boolean(settings,"workbench","info-parse-local",wb.info_parse_local);
 g_key_file_set_boolean(settings,"workbench","info-across-languages",wb.info_across_languages);
 g_key_file_set_integer(settings,"workbench","right-split",gtk_paned_get_position(GTK_PANED(wb.right_pane)));
 g_key_file_set_integer(settings,"workbench","project-tab",project_tab_identity());
 g_key_file_set_integer(settings,"workbench","project-search-flags",text_search_flags(wb.project_case,wb.project_word,wb.project_regex));
 g_key_file_set_integer(settings,"workbench","editor-search-flags",text_search_flags(wb.editor_case,wb.editor_word,wb.editor_regex));
 g_key_file_set_boolean(settings,"workbench","relation-transitive",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_transitive)));
 g_key_file_set_integer(settings,"workbench","relation-mode",gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)));
 gboolean graph_vertical=FALSE;gint graph_zoom=100;insight_relation_graph_get_options(!strcmp(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"treegraph") ? wb.relation_tree_graph : wb.relation_graph,&graph_vertical,&graph_zoom);
 g_key_file_set_boolean(settings,"workbench","graph-vertical",graph_vertical);g_key_file_set_integer(settings,"workbench","graph-zoom",graph_zoom);
 g_key_file_set_boolean(settings,"workbench","relation-tree",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle)));
 g_key_file_set_boolean(settings,"workbench","relation-graph",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle)));
 g_key_file_set_boolean(settings,"workbench","relation-enclosing",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_enclosing)));
 g_key_file_set_boolean(settings,"workbench","relation-follow",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow)));
 g_key_file_set_boolean(settings,"workbench","confirm-bookmark-delete",wb.marks_confirm_delete);
 g_key_file_set_boolean(settings,"workbench","delete-bookmarks-on-close",wb.marks_delete_on_close);
 g_key_file_set_boolean(settings,"workbench","preserve-bookmarks",wb.marks_preserve);
 g_key_file_set_integer(settings,"workbench","references-comment-mode",gtk_combo_box_get_active(GTK_COMBO_BOX(wb.usage_comments)));
 g_key_file_set_boolean(settings,"workbench","references-code-only",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code)));
 g_key_file_set_boolean(settings,"workbench","usage-buffer",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer)));
 g_key_file_set_boolean(settings,"workbench","wrap",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.wrap)));
 gchar *encoded=wb.root ? g_base64_encode((const guchar *)wb.root,strlen(wb.root)) : g_strdup("");
 g_key_file_set_string(settings,"workbench","root-base64",encoded); g_free(encoded);
 gsize length; gchar *text=g_key_file_to_data(settings,&length,NULL),*directory=g_path_get_dirname(wb.prefs_path);
 if(!g_mkdir_with_parents(directory,0700)) {
  GFile *file=g_file_new_for_path(wb.prefs_path); GError *error=NULL;
  if(!g_file_replace_contents(file,text,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error)) { ui_set_statusbar(TRUE,"Workbench settings not saved: %s",error->message); g_clear_error(&error); }
  g_object_unref(file);
 } else ui_set_statusbar(TRUE,"Workbench settings directory could not be created");
 g_free(directory); g_free(text); g_key_file_free(settings);
}
static void restore_workbench_settings(void) {
 gchar *prefs_utf8=utils_get_utf8_from_locale(wb.prefs_path); char *text=read_disk_source(prefs_utf8,FALSE); g_free(prefs_utf8); if(!text) return;
 GKeyFile *settings=g_key_file_new();
 /* Directory defaults can include two long UTF-8 paths and masks. */
 if(strlen(text)<=262144 && g_key_file_load_from_data(settings,text,-1,G_KEY_FILE_NONE,NULL) && g_key_file_get_integer(settings,"workbench","version",NULL)==1) {
  if(g_key_file_has_key(settings,"workbench","info-parse-local",NULL)) { GError *option_error=NULL;gboolean enabled=g_key_file_get_boolean(settings,"workbench","info-parse-local",&option_error);if(!option_error) wb.info_parse_local=enabled;g_clear_error(&option_error); }
  wb.info_across_languages=g_key_file_get_boolean(settings,"workbench","info-across-languages",NULL);
  gint info_scope=g_key_file_get_integer(settings,"workbench","info-source-scope",NULL);wb.info_scope=info_scope>=0 && info_scope<=2 ? info_scope:0;
  insight_overview_read(settings);
 grouped_read(&wb.type_panel,settings); grouped_read(&wb.category_panel,settings);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.tree_follow),g_key_file_get_boolean(settings,"workbench","folder-follow-current-file",NULL));
 insight_directory_read(settings);insight_diff_read(settings);
 insight_list_style_read(wb.tree_view,settings,"project-folders-style");
 insight_list_style_read(wb.marks_view,settings,"bookmarks-style");
 gboolean disk_saved=insight_disk_browser_read(wb.disk_browser,settings);if(disk_saved && g_key_file_get_integer(settings,"workbench","folder-view-mode",NULL)==1) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),1);
 symbol_sort_read(settings,wb.file_store,"project-files");insight_symbol_view_columns_read(GTK_TREE_VIEW(wb.file_view),settings,"project-files");symbol_history_read(settings,wb.file_entry,"project-files");
  symbol_history_read(settings,wb.local_entry,"file-symbols");symbol_history_read(settings,wb.symbol_entry,"project-symbols");
  insight_symbol_view_columns_read(GTK_TREE_VIEW(wb.local_view),settings,"file-symbols");insight_symbol_view_columns_read(GTK_TREE_VIEW(wb.symbol_view),settings,"project-symbols");
  if(!g_key_file_get_boolean(settings,"workbench","project-relative-path-v1",NULL)) gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(wb.symbol_view),SYMBOL_PATH),TRUE);
  symbol_sort_read(settings,wb.local_symbols,"file-symbols");symbol_sort_read(settings,wb.symbols,"project-symbols");
  if(g_key_file_has_key(settings,"workbench","file-symbol-types",NULL)) { guint mask=(guint)g_key_file_get_integer(settings,"workbench","file-symbol-types",NULL);if(!g_key_file_get_boolean(settings,"workbench","symbol-locals-opt-in-v1",NULL)) mask &= ~(1u<<7);insight_symbol_view_set_types(GTK_TREE_VIEW(wb.local_view),mask); }
  if(g_key_file_has_key(settings,"workbench","project-symbol-types",NULL)) { guint mask=(guint)g_key_file_get_integer(settings,"workbench","project-symbol-types",NULL);if(!g_key_file_get_boolean(settings,"workbench","symbol-locals-opt-in-v1",NULL)) mask &= ~(1u<<7);insight_symbol_view_set_types(GTK_TREE_VIEW(wb.symbol_view),mask); }
  insight_symbol_view_set_scopes(GTK_TREE_VIEW(wb.local_view),g_key_file_get_boolean(settings,"workbench","file-symbols-scopes",NULL));insight_symbol_view_set_scopes(GTK_TREE_VIEW(wb.symbol_view),g_key_file_get_boolean(settings,"workbench","project-symbols-scopes",NULL));
  insight_symbol_view_set_grouped(GTK_TREE_VIEW(wb.local_view),g_key_file_get_boolean(settings,"workbench","file-symbols-grouped",NULL)); insight_symbol_view_set_grouped(GTK_TREE_VIEW(wb.symbol_view),g_key_file_get_boolean(settings,"workbench","project-symbols-grouped",NULL));
  for(guint i=0;i<4;i++) { gchar group[16]; g_snprintf(group,sizeof group,"layout-%c",'A'+i); insight_layout_read(settings,group,&wb.layouts[i]); }
  for(guint i=0;i<10;i++) { PanelHost *host=&wb.hosts[i]; gchar group[32]; g_snprintf(group,sizeof group,"panel-%u",i); GError *error=NULL; gint version=g_key_file_get_integer(settings,group,"version",&error);
   if(!error && (version>=1 && version<=5)) { if(version>=5) { host->saved_group=g_key_file_get_integer(settings,group,"group",&error);if(!error) host->saved_group_order=g_key_file_get_integer(settings,group,"group-order",&error);if(!error) host->saved_group_active=g_key_file_get_integer(settings,group,"group-active",&error); }if(!error && version>=4) host->opacity=g_key_file_get_integer(settings,group,"opacity",&error);if(!error && version>=3) host->minimized=g_key_file_get_boolean(settings,group,"minimized",&error); if(!error && version>=2) host->maximized=g_key_file_get_boolean(settings,group,"maximized",&error); if(!error) host->restore=g_key_file_get_boolean(settings,group,"floating",&error); if(!error) host->visible=g_key_file_get_boolean(settings,group,"visible",&error); if(!error) host->x=g_key_file_get_integer(settings,group,"x",&error); if(!error) host->y=g_key_file_get_integer(settings,group,"y",&error); if(!error) host->width=g_key_file_get_integer(settings,group,"width",&error); if(!error) host->height=g_key_file_get_integer(settings,group,"height",&error); }
   if(error || (version<1 || version>5) || host->opacity<40 || host->opacity>100 || host->width<300 || host->width>4000 || host->height<200 || host->height>4000 || host->x < -30000 || host->x>30000 || host->y < -30000 || host->y>30000) { host->restore=FALSE;host->opacity=100;host->maximized=FALSE;host->minimized=FALSE;host->saved_group=0;host->saved_group_order=0;host->saved_group_active=0; }
   g_clear_error(&error);
  }
  wb.transparent_panels=g_key_file_get_boolean(settings,"workbench","transparent-panels",NULL);
  if(g_key_file_has_key(settings,"workbench","tab-tray-visible",NULL)) wb.tray_enabled=g_key_file_get_boolean(settings,"workbench","tab-tray-visible",NULL);
  wb.clips_prefer_project=g_key_file_get_boolean(settings,"workbench","project-templates",NULL);clips_project_load(app->project ? app->project->file_name : NULL,TRUE);
  wb.restore_float=g_key_file_get_boolean(settings,"workbench","floating",NULL);
  wb.float_x=CLAMP(g_key_file_get_integer(settings,"workbench","float-x",NULL),-30000,30000); wb.float_y=CLAMP(g_key_file_get_integer(settings,"workbench","float-y",NULL),-30000,30000);
  gint fw=g_key_file_get_integer(settings,"workbench","float-width",NULL),fh=g_key_file_get_integer(settings,"workbench","float-height",NULL);
  wb.float_width=fw>=300 ? MIN(fw,4000) : 0; wb.float_height=fh>=200 ? MIN(fh,4000) : 0;
  if(g_key_file_has_key(settings,"workbench","project-visible",NULL)) gtk_widget_set_visible(wb.project_notebook,g_key_file_get_boolean(settings,"workbench","project-visible",NULL));
  gint split=g_key_file_get_integer(settings,"workbench","split",NULL); if(split>=150 && split<=10000) gtk_paned_set_position(GTK_PANED(wb.pane),split);
  if(g_key_file_has_key(settings,"workbench","follow",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),g_key_file_get_boolean(settings,"workbench","follow",NULL));
  if(g_key_file_has_key(settings,"workbench","visible",NULL)) wb.context_visible=g_key_file_get_boolean(settings,"workbench","visible",NULL);
  gint right=g_key_file_get_integer(settings,"workbench","right-split",NULL); if(g_key_file_has_key(settings,"workbench","right-split",NULL) && right>=0 && right<=10000) { wb.initial_right_split=right;gtk_paned_set_position(GTK_PANED(wb.right_pane),right); }
  /* Search controls are opened explicitly; old persisted visibility is ignored. */
  gint project_flags=g_key_file_get_integer(settings,"workbench","project-search-flags",NULL),editor_flags=g_key_file_get_integer(settings,"workbench","editor-search-flags",NULL);
  if(project_flags>=0 && project_flags<=7) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_case),project_flags&INSIGHT_SEARCH_CASE); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_word),project_flags&INSIGHT_SEARCH_WORD); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.project_regex),project_flags&INSIGHT_SEARCH_REGEX); }
  if(editor_flags>=0 && editor_flags<=7) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_case),editor_flags&INSIGHT_SEARCH_CASE); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_word),editor_flags&INSIGHT_SEARCH_WORD); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.editor_regex),editor_flags&INSIGHT_SEARCH_REGEX); }
  if(g_key_file_has_key(settings,"workbench","relation-transitive",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_transitive),g_key_file_get_boolean(settings,"workbench","relation-transitive",NULL));
  gint relation=g_key_file_get_integer(settings,"workbench","relation-mode",NULL); if(relation>=0 && relation<11) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),relation);
  insight_relation_graph_set_options(wb.relation_graph,g_key_file_get_boolean(settings,"workbench","graph-vertical",NULL),g_key_file_has_key(settings,"workbench","graph-zoom",NULL) ? g_key_file_get_integer(settings,"workbench","graph-zoom",NULL) : 100);
  insight_relation_graph_set_options(wb.relation_tree_graph,g_key_file_get_boolean(settings,"workbench","graph-vertical",NULL),g_key_file_has_key(settings,"workbench","graph-zoom",NULL) ? g_key_file_get_integer(settings,"workbench","graph-zoom",NULL) : 100);
  if(g_key_file_has_key(settings,"workbench","relation-tree",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle),g_key_file_get_boolean(settings,"workbench","relation-tree",NULL));
  if(g_key_file_has_key(settings,"workbench","relation-graph",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle),g_key_file_get_boolean(settings,"workbench","relation-graph",NULL));
  if(g_key_file_has_key(settings,"workbench","relation-enclosing",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_enclosing),g_key_file_get_boolean(settings,"workbench","relation-enclosing",NULL));
  if(g_key_file_has_key(settings,"workbench","relation-follow",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),g_key_file_get_boolean(settings,"workbench","relation-follow",NULL));
  if(g_key_file_has_key(settings,"workbench","references-code-only",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_code),g_key_file_get_boolean(settings,"workbench","references-code-only",NULL));
  wb.marks_confirm_delete=g_key_file_get_boolean(settings,"workbench","confirm-bookmark-delete",NULL);
  wb.marks_delete_on_close=g_key_file_get_boolean(settings,"workbench","delete-bookmarks-on-close",NULL);
  if(g_key_file_has_key(settings,"workbench","preserve-bookmarks",NULL)) { GError *error=NULL;gboolean preserve=g_key_file_get_boolean(settings,"workbench","preserve-bookmarks",&error);if(!error) wb.marks_preserve=preserve;g_clear_error(&error); }
  gint comments=g_key_file_get_integer(settings,"workbench","references-comment-mode",NULL);if(comments>=0 && comments<=2 && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code))) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments),comments);
  gint tab=g_key_file_get_integer(settings,"workbench","project-tab",NULL); if(tab>=0 && tab<8) gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),tab);
  if(g_key_file_has_key(settings,"workbench","usage-buffer",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_buffer),g_key_file_get_boolean(settings,"workbench","usage-buffer",NULL));
  if(g_key_file_has_key(settings,"workbench","wrap",NULL)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.wrap),g_key_file_get_boolean(settings,"workbench","wrap",NULL));
  gchar *encoded=g_key_file_get_string(settings,"workbench","root-base64",NULL);
  if(encoded && !wb.root) {
   gsize length; guchar *decoded=g_base64_decode(encoded,&length); gchar *canonical=g_base64_encode(decoded,length);
   if(length && length<=16384 && !memchr(decoded,0,length) && !strcmp(encoded,canonical)) { gchar *root=g_strndup((const gchar *)decoded,length); if(g_path_is_absolute(root)) wb.root=root; else g_free(root); }
   g_free(canonical); g_free(decoded);
  }
  g_free(encoded);
 }
 g_free(text); g_key_file_free(settings);
 if(!wb.context_visible) gtk_widget_hide(wb.box);
}
