/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Tool menus and floating panel setup.
 * RU: Меню инструментов и настройка плавающих панелей. */

static GtkWidget *tool_command(GtkWidget *menu, const gchar *label, GCallback callback, gpointer data)
{
    GtkWidget *item = gtk_menu_item_new_with_label(insight_tr(label));
    g_signal_connect(item, "activate", callback, data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    return item;
}

static void tool_help(GtkWidget *menu, const gchar *label, const gchar *topic)
{
    GtkWidget *separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_widget_show(separator);
    tool_command(menu, label, G_CALLBACK(insight_help_show), (gpointer)topic);
}

static void create_native_lua(void)
{
    gchar *settings = g_build_filename(app->configdir, "insight", "lua-extension.conf", NULL);
    wb.extensions = insight_extensions_new(settings);
    g_free(settings);
    GError *error = NULL;
    if (!insight_extensions_restore(wb.extensions, &error))
    {
        ui_set_statusbar(TRUE, "Lua startup: %s", error->message);
        g_clear_error(&error);
    }

    GtkWidget *cpp = tool_submenu("C/C++");
    tool_command(cpp, "Debug C/C++ Executable…", G_CALLBACK(debug_native_executable), NULL);
    tool_command(cpp, "Call paths…", G_CALLBACK(call_paths_command), GINT_TO_POINTER(INSIGHT_CALL_SHOW));
    tool_help(cpp, "C/C++ Debugger Help…", "native-debugger.md");

    GtkWidget *python = tool_submenu("Python");
    tool_command(python, "Debug Current Python Buffer…", G_CALLBACK(debug_current_script), GINT_TO_POINTER(TRUE));
    tool_command(python, "Debug Python File…", G_CALLBACK(debug_select_script_entry), GINT_TO_POINTER(TRUE));
    tool_command(python, "Check Python Syntax", G_CALLBACK(script_check_start), GINT_TO_POINTER(TRUE));
    tool_help(python, "Python Debugger Help…", "python-debugger.md");

    wb.lua_menu = tool_submenu("Lua");
    tool_command(wb.lua_menu, "Debug Current Lua Buffer…", G_CALLBACK(debug_current_script), NULL);
    tool_command(wb.lua_menu, "Debug Lua File…", G_CALLBACK(debug_select_script_entry), NULL);
    tool_command(wb.lua_menu, "Check Lua Syntax", G_CALLBACK(script_check_start), NULL);
    tool_command(wb.lua_menu, "Run script…", G_CALLBACK(choose_native_script), GINT_TO_POINTER(TRUE));
    tool_command(wb.lua_menu, "Load extension…", G_CALLBACK(choose_native_script), NULL);
    tool_command(wb.lua_menu, "Reload saved extension", G_CALLBACK(reload_native_script), NULL);
    tool_command(wb.lua_menu, "Unload extension", G_CALLBACK(reload_native_script), GINT_TO_POINTER(TRUE));
    GtkWidget *commands = gtk_menu_item_new_with_label(insight_tr("Commands"));
    wb.lua_commands = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(commands), wb.lua_commands);
    gtk_menu_shell_append(GTK_MENU_SHELL(wb.lua_menu), commands);
    gtk_widget_show(commands);
    wb.palette_item = tool_command(wb.lua_menu, "Command palette…", G_CALLBACK(show_native_palette), NULL);
    native_commands_rebuild();
    tool_help(wb.lua_menu, "Lua Debugger Help…", "lua-debugger.md");
    tool_command(wb.lua_menu, "Lua Scripting Help…", G_CALLBACK(insight_help_show), "lua-api.md");

    GtkWidget *verilog = tool_submenu("Verilog / SystemVerilog");
    GtkWidget *debug_hdl = tool_command(verilog, "Debug Current Verilog File",
        G_CALLBACK(waveform_debug_current), NULL);
    gtk_widget_set_name(debug_hdl, "insight-tools-debug-current-hdl");
    tool_command(verilog, "Debug Verilog Sources with Test Devices…",
                 G_CALLBACK(waveform_run_simulation), GINT_TO_POINTER(2));
    tool_command(verilog, "Debug Verilog Testbench…", G_CALLBACK(waveform_run_simulation), GINT_TO_POINTER(TRUE));
    tool_command(verilog, "Run Verilog Testbench…", G_CALLBACK(waveform_run_simulation), NULL);
    tool_command(verilog, "VCD Waveforms…", G_CALLBACK(waveforms_menu), NULL);
    tool_help(verilog, "Verilog Debugger Help…", "verilog-debugger.md");
    GtkWidget *current_hdl = tool_command(main_widgets.editor_menu, "Debug Current Verilog File",
        G_CALLBACK(waveform_debug_current), NULL);
    gtk_widget_set_name(current_hdl, "insight-debug-current-hdl");
}

static void focus_widget_panel(GtkWidget *widget) {
 GtkWidget *owner=gtk_widget_get_toplevel(widget);for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==owner && wb.hosts[i].minimized) panel_restore(i); if(GTK_IS_WINDOW(owner) && owner!=main_widgets.window) { gtk_widget_show(owner); gtk_window_present(GTK_WINDOW(owner)); }
 /* Reveal every containing notebook, rather than just its innermost page. */
 for(GtkWidget *page=widget,*parent=gtk_widget_get_parent(page);parent;page=parent,parent=gtk_widget_get_parent(parent)) {
  if(GTK_IS_NOTEBOOK(parent)) {gtk_widget_show(page);gtk_widget_show(parent);gtk_notebook_set_current_page(GTK_NOTEBOOK(parent),gtk_notebook_page_num(GTK_NOTEBOOK(parent),page));}
  if(GTK_IS_PANED(parent)) {gboolean horizontal=gtk_orientable_get_orientation(GTK_ORIENTABLE(parent))==GTK_ORIENTATION_HORIZONTAL;gint extent=horizontal ? gtk_widget_get_allocated_width(parent):gtk_widget_get_allocated_height(parent);gint size=horizontal ? gtk_widget_get_allocated_width(page):gtk_widget_get_allocated_height(page);if(extent>100 && size<40) gtk_paned_set_position(GTK_PANED(parent),page==gtk_paned_get_child1(GTK_PANED(parent)) ? MIN(220,extent/3):MAX(0,extent-MIN(220,extent/3)));}

 }
 if(gtk_widget_is_ancestor(widget,main_widgets.message_window_notebook)) msgwin_show_hide(TRUE);
 if(gtk_widget_is_ancestor(widget,main_widgets.sidebar_notebook) && !ui_prefs.sidebar_visible) {ui_prefs.sidebar_visible=TRUE;ui_sidebar_show_hide();}
 gtk_widget_grab_focus(widget);
}
static void panel_position_guard(GObject *object,GParamSpec *spec,gpointer data) {
 (void)spec;(void)data;GtkWidget *widget=GTK_WIDGET(object);gint extent=gtk_orientable_get_orientation(GTK_ORIENTABLE(widget))==GTK_ORIENTATION_HORIZONTAL ? gtk_widget_get_allocated_width(widget) : gtk_widget_get_allocated_height(widget);
 if(extent<100) return;
 GtkWidget *first=gtk_paned_get_child1(GTK_PANED(widget)),*second=gtk_paned_get_child2(GTK_PANED(widget));
 /* A detached/hidden child has no split to protect. Forcing 16px against
  * GTK's zero-width side repeatedly schedules allocation/position updates. */
 if(!first || !second || !gtk_widget_get_visible(first) || !gtk_widget_get_visible(second)) return;
 gint minimum=0,maximum=0;g_object_get(widget,"min-position",&minimum,"max-position",&maximum,NULL);
 minimum=MAX(minimum,16);maximum=MIN(maximum,extent-16);if(minimum>maximum) return;
 gint position=gtk_paned_get_position(GTK_PANED(widget)),safe=CLAMP(position,minimum,maximum);if(position!=safe) gtk_paned_set_position(GTK_PANED(widget),safe);
}
static void project_split_allocated(GtkWidget *widget,GtkAllocation *allocation,gpointer data) {
 (void)data;if(!wb.alive || wb.initial_right_split==-1 || allocation->width<400) return;
 gint minimum=0,maximum=0;g_object_get(widget,"min-position",&minimum,"max-position",&maximum,NULL);
 if(wb.initial_right_split>maximum && maximum-minimum<120) return;
 gint requested=wb.initial_right_split;wb.initial_right_split=-1;
 if(requested==-2) { gtk_paned_set_position(GTK_PANED(widget),MAX(0,allocation->width-100));return; }
 if(requested>maximum) { gint project=CLAMP(allocation->width/3,120,450);gtk_paned_set_position(GTK_PANED(widget),CLAMP(allocation->width-project,minimum,maximum)); }
}
static void focus_templates(GtkWidget *widget,gpointer data) { (void)widget; (void)data; focus_widget_panel(wb.clips_entry); }
static void focus_browser(GtkMenuItem *item,gpointer data) {
 (void)item; gint which=GPOINTER_TO_INT(data); GtkWidget *widget=which==1 ? wb.file_entry : (which==2 ? wb.usage_entry : wb.symbol_entry);
 if(which==1) files_filter(NULL,NULL); else if(which==0) browser_filter(NULL,NULL); else if(which==2) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE); gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),0); }
 focus_widget_panel(widget);
}
gboolean insight_workbench_goto_definition(void) {
 if(wb.preview && gtk_widget_has_focus(GTK_WIDGET(wb.preview)))
  return preview_definition(sci_get_current_position(wb.preview));
 GeanyDocument *doc=document_get_current(); if(!wb.preview || !doc) return FALSE;
 if(insight_completion_definition(doc->editor)) return TRUE;
 char word[1025]={0}; editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
 const InsightSymbol *definition=word_definition(doc,word,NULL,(gulong)sci_get_current_line(doc->editor->sci)+1,doc->editor->sci,sci_get_current_position(doc->editor->sci));
 if(!definition) return FALSE;
 gchar *path=g_strdup(definition->path); gulong line=definition->line;
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); gboolean shown=show_location(path,0,line,TRUE); if(shown) open_context(NULL,NULL); g_free(path); return shown;
}
static void go_definition(GtkMenuItem *item,gpointer data) {
 (void)item;(void)data;if(insight_workbench_goto_definition()) return;GeanyDocument *doc=document_get_current();if(doc && !gtk_widget_has_focus(GTK_WIDGET(wb.preview))) {document_update_tags(doc);if(symbols_goto_tag(doc,sci_get_current_position(doc->editor->sci),TRUE)) return;}ui_set_statusbar(TRUE,"Known definition not found");
}
static void context_visible(GtkCheckMenuItem *item,gpointer data) {
 (void)data; wb.context_visible=gtk_check_menu_item_get_active(item);
 if(wb.context_visible) { gtk_widget_show(wb.box); reveal_context(); insight_workbench_refresh(); } else gtk_widget_hide(wb.box);
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].page==wb.box && wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,wb.context_visible && !wb.hosts[i].minimized);
}
static void focus_relations(GtkMenuItem *item,gpointer data) { (void)item; (void)data; focus_widget_panel(wb.usage_entry); }
gboolean insight_workbench_activate(guint action) {
 if(!wb.alive || action>=15) return FALSE;
 if(action==14) { GeanyDocument *doc=document_get_current();if(!DOC_VALID(doc)) return FALSE;gtk_window_present(GTK_WINDOW(main_widgets.window));document_grab_focus(doc);return TRUE; }
 if(action==10) { insight_window_list_show(NULL,NULL);return TRUE; }
 if(action==13) { focus_search_results(NULL,NULL);gtk_window_present(GTK_WINDOW(main_widgets.window));return TRUE; }
 GtkWidget *targets[]={wb.file_entry,wb.tree_entry,wb.symbol_entry,wb.usage_entry,wb.marks_entry,wb.clips_entry,wb.type_panel.entry,wb.category_panel.entry,wb.local_entry,GTK_WIDGET(wb.preview),NULL,wb.editor_query,wb.project_query};
 GtkWidget *target=targets[action];if(!target) return FALSE;
 if(action==8 && !wb.hosts[8].window) { ui_prefs.sidebar_visible=TRUE;interface_prefs.sidebar_symbol_visible=TRUE;ui_sidebar_show_hide(); }
 if(action==9) { wb.context_visible=TRUE;gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),TRUE);gtk_widget_show(wb.box);reveal_context(); }
 if(action==11) gtk_widget_show(wb.editor_search_row);
 if(action==12) { gtk_widget_show(wb.project_search_row);focus_search_results(NULL,NULL); }
 focus_widget_panel(target);GtkWidget *owner=gtk_widget_get_toplevel(target);if(GTK_IS_WINDOW(owner)) gtk_window_present(GTK_WINDOW(owner));return TRUE;
}
static void activate_panel(GtkMenuItem *item,gpointer data) { (void)item;insight_workbench_activate(GPOINTER_TO_UINT(data)); }
static void expand_panel(GtkMenuItem *item,gpointer data) {
 (void)item; gint action=GPOINTER_TO_INT(data);if(action<2) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),0); GtkTreeView *view=GTK_TREE_VIEW(action<2 ? wb.tree_view : (action<4 ? wb.category_view : wb.types_view));
 if(action%2) gtk_tree_view_collapse_all(view); else gtk_tree_view_expand_all(view);
}
/* A dialog may outlive project refreshes: any model change invalidates its target. */
static void tree_target_changed(gboolean *changed) { *changed=TRUE; }
static void tree_dialog_destroyed(GtkWidget *widget,gpointer data) { (void)widget;*(gboolean *)data=TRUE; }
static void tree_branch_command(GtkMenuItem *item,gpointer data) {
 (void)item;guint command=GPOINTER_TO_UINT(data),panel=command/3,action=command%3;
 GtkWidget *widget=panel==0 ? wb.tree_view : (panel==1 ? wb.category_view : wb.types_view);
 if(panel==0) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode),0);
 if(!GTK_IS_TREE_VIEW(widget)) return;
 GtkTreeView *view=GTK_TREE_VIEW(widget);GtkTreePath *path=NULL;
 gtk_tree_view_get_cursor(view,&path,NULL);
 if(!path) { ui_set_statusbar(TRUE,"%s",insight_tr("Select a tree row first"));return; }
 GtkTreeModel *model=gtk_tree_view_get_model(view);g_object_ref(model);g_object_ref(view);
 GtkTreeRowReference *row=gtk_tree_row_reference_new(model,path);gtk_tree_path_free(path);
 guint levels=32;gboolean changed=FALSE,accepted=TRUE;
 if(action==0) {
  const gchar *signals[]={"row-changed","row-inserted","row-deleted","rows-reordered"};gulong handlers[4];
  for(guint i=0;i<4;i++) handlers[i]=g_signal_connect_swapped(model,signals[i],G_CALLBACK(tree_target_changed),&changed);
  GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Expand Special"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Expand"),GTK_RESPONSE_ACCEPT,NULL);
  gtk_widget_set_name(dialog,"insight-tree-expand-dialog");
  GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *label=gtk_label_new(insight_tr("Levels below the selected row (1–32)")),*spin=gtk_spin_button_new_with_range(1,32,1);
  gtk_widget_set_name(spin,"insight-tree-expand-depth");gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin),1);
  gtk_container_set_border_width(GTK_CONTAINER(box),12);gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(box),spin,FALSE,FALSE,4);
  gboolean destroyed=FALSE;g_object_ref(dialog);g_signal_connect(dialog,"destroy",G_CALLBACK(tree_dialog_destroyed),&destroyed);
  gtk_widget_show_all(dialog);accepted=gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT && !destroyed;
  if(!destroyed) { levels=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));gtk_widget_destroy(dialog); }
  g_object_unref(dialog);
  for(guint i=0;i<4;i++) g_signal_handler_disconnect(model,handlers[i]);
 }
 path=gtk_tree_row_reference_get_path(row);
 if(accepted && (changed || !path || !wb.alive || gtk_widget_in_destruction(widget) || gtk_tree_view_get_model(view)!=model)) ui_set_statusbar(TRUE,"%s",insight_tr("Tree changed; select the row again"));
 else if(accepted) {
  if(action==2) gtk_tree_view_collapse_row(view,path);
  else if(!insight_tree_expand_levels(view,path,levels)) ui_set_statusbar(TRUE,"%s",insight_tr("Tree expansion stopped at its safety limit or the tree changed"));
 }
 if(path) gtk_tree_path_free(path);gtk_tree_row_reference_free(row);g_object_unref(model);g_object_unref(view);
}
static void project_visible(GtkCheckMenuItem *item,gpointer data) {
 (void)data; gboolean visible=gtk_check_menu_item_get_active(item); gtk_widget_set_visible(wb.project_notebook,visible); if(wb.float_window) gtk_widget_set_visible(wb.float_window,visible);
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].home==wb.project_notebook && wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,visible && !wb.hosts[i].minimized);
}
static void local_visible(GtkCheckMenuItem *item,gpointer data) { (void)data; ui_prefs.sidebar_visible=gtk_check_menu_item_get_active(item); ui_sidebar_show_hide();
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].home==main_widgets.sidebar_notebook && wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,ui_prefs.sidebar_visible && !wb.hosts[i].minimized); }
static void dock_project(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data; if(!wb.float_window) return;
 gtk_window_get_position(GTK_WINDOW(wb.float_window),&wb.float_x,&wb.float_y); gtk_window_get_size(GTK_WINDOW(wb.float_window),&wb.float_width,&wb.float_height);
 GtkWidget *window=wb.float_window; wb.float_window=NULL; gboolean visible=gtk_widget_get_visible(wb.project_notebook);
 g_object_ref(wb.project_host); gtk_container_remove(GTK_CONTAINER(window),wb.project_host); gtk_paned_pack2(GTK_PANED(wb.right_pane),wb.project_host,FALSE,TRUE); g_object_unref(wb.project_host); gtk_widget_set_visible(wb.project_notebook,visible); gtk_widget_destroy(window);
}
static gboolean float_closed(GtkWidget *widget,GdkEvent *event,gpointer data) { (void)widget; (void)event; dock_project(NULL,data); return TRUE; }
static void float_project(GtkMenuItem *item,gpointer data) {
 (void)item; (void)data; if(wb.float_window) { gtk_window_present(GTK_WINDOW(wb.float_window)); return; }
 wb.float_window=gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(wb.float_window),"Geany Insight — Project"); gtk_window_set_transient_for(GTK_WINDOW(wb.float_window),GTK_WINDOW(main_widgets.window));
 gtk_window_set_default_size(GTK_WINDOW(wb.float_window),wb.float_width>=300 ? wb.float_width : 440,wb.float_height>=200 ? wb.float_height : 620);
 g_object_ref(wb.project_host); gtk_container_remove(GTK_CONTAINER(wb.right_pane),wb.project_host); gtk_container_add(GTK_CONTAINER(wb.float_window),wb.project_host); g_object_unref(wb.project_host);
 g_signal_connect(wb.float_window,"delete-event",G_CALLBACK(float_closed),NULL);insight_appearance_decorate(wb.float_window); gtk_widget_show_all(wb.float_window);
 if(wb.float_width>=300) {
  GdkMonitor *monitor=gdk_display_get_monitor_at_point(gdk_display_get_default(),wb.float_x,wb.float_y);
  if(monitor) { GdkRectangle area; gdk_monitor_get_workarea(monitor,&area); wb.float_width=MIN(wb.float_width,MAX(300,area.width)); wb.float_height=MIN(wb.float_height,MAX(200,area.height)); wb.float_x=CLAMP(wb.float_x,area.x,area.x+MAX(0,area.width-wb.float_width)); wb.float_y=CLAMP(wb.float_y,area.y,area.y+MAX(0,area.height-wb.float_height)); gtk_window_resize(GTK_WINDOW(wb.float_window),wb.float_width,wb.float_height); }
  gtk_window_move(GTK_WINDOW(wb.float_window),wb.float_x,wb.float_y);
 }
}
/* Floating groups share one window; page ownership always follows its parent. */
static guint panel_window_owner(GtkWidget *window) {
 for(guint i=0;i<wb.host_count;i++) if(window && wb.hosts[i].window==window) return i;
 return wb.host_count;
}
static guint panel_window_count(GtkWidget *window) {
 guint count=0;for(guint i=0;i<wb.host_count;i++) if(window && wb.hosts[i].window==window) count++;return count;
}
/* Keep controls on the page so they survive reparenting and layout restore. */
static void panel_controls_update(PanelHost *host) {
 if(!host->dock_button) return;
 gboolean floating=host->window!=NULL;
 gboolean maximized=floating && gtk_window_is_maximized(GTK_WINDOW(host->window));
 gtk_button_set_image(GTK_BUTTON(host->dock_button),gtk_image_new_from_icon_name(floating ? "go-home-symbolic" : "window-new-symbolic",GTK_ICON_SIZE_MENU));
 gtk_widget_set_tooltip_text(host->dock_button,insight_tr(floating ? "Dock this panel" : "Float this panel"));
 gtk_widget_set_sensitive(host->maximize_button,floating);gtk_widget_set_visible(host->maximize_button,floating);
 if(host->minimize_button) { gtk_widget_set_sensitive(host->minimize_button,floating);gtk_widget_set_visible(host->minimize_button,floating); }
 if(host->group_button) gtk_widget_set_visible(host->group_button,floating);
 if(host->drag_handle) gtk_widget_set_visible(host->drag_handle,floating);
 if(host->ungroup_item) gtk_widget_set_sensitive(host->ungroup_item,floating && panel_window_count(host->window)>1);
 if(host->opacity_button) { gtk_widget_set_visible(host->opacity_button,floating && wb.transparent_panels);gchar *tip=insight_i18n_printf("Floating panel opacity: %d%%",host->opacity);gtk_widget_set_tooltip_text(host->opacity_button,tip);g_free(tip); }
 if(host->window) insight_appearance_panel_opacity(host->window,wb.transparent_panels ? host->opacity:100);
 if(host->opacity_button) gtk_widget_set_sensitive(host->opacity_button,insight_appearance_panel_opacity_available() && (!host->window || !g_object_get_data(G_OBJECT(host->window),"insight-independent-opacity")));
 gtk_button_set_image(GTK_BUTTON(host->maximize_button),gtk_image_new_from_icon_name(maximized ? "view-restore-symbolic" : "view-fullscreen-symbolic",GTK_ICON_SIZE_MENU));
 gtk_widget_set_tooltip_text(host->maximize_button,insight_tr(maximized ? "Restore panel size" : "Maximize floating panel"));
}
static void panel_geometry_capture(PanelHost *host) {
 if(!host->window || host->minimized) return;
 GdkWindow *window=gtk_widget_get_window(host->window);
 if(window && !(gdk_window_get_state(window)&(GDK_WINDOW_STATE_MAXIMIZED|GDK_WINDOW_STATE_FULLSCREEN|GDK_WINDOW_STATE_ICONIFIED))) {
  gtk_window_get_position(GTK_WINDOW(host->window),&host->x,&host->y);gtk_window_get_size(GTK_WINDOW(host->window),&host->width,&host->height);
 }
}
static void panel_window_refresh(GtkWidget *window) {
 if(!window) return;
 GString *title=g_string_new("Geany Insight — ");guint count=0;
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==window) { if(count++) g_string_append(title," / ");g_string_append(title,insight_tr(wb.hosts[i].label));panel_controls_update(&wb.hosts[i]); }
 if(count) gtk_window_set_title(GTK_WINDOW(window),title->str);
 g_string_free(title,TRUE);panel_tray_sync();
}
static gboolean panel_configured(GtkWidget *widget,GdkEventConfigure *event,gpointer data) {
 (void)event;(void)data;for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==widget) panel_geometry_capture(&wb.hosts[i]);return FALSE;
}
static gboolean panel_window_state(GtkWidget *widget,GdkEventWindowState *event,gpointer data) {
 (void)data;guint owner=panel_window_owner(widget);if(owner>=wb.host_count) return FALSE;
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==widget) { if(!wb.hosts[i].minimized) wb.hosts[i].maximized=(event->new_window_state&GDK_WINDOW_STATE_MAXIMIZED)!=0;panel_controls_update(&wb.hosts[i]); }
 if(event->new_window_state&GDK_WINDOW_STATE_ICONIFIED) panel_minimize(owner);
 return FALSE;
}
static void dock_panel(guint index) {
 if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index]; if(!host->window) return;
 panel_geometry_capture(host); host->visible=gtk_widget_get_visible(host->window);
 GtkWidget *window=host->window; host->window=NULL;host->minimized=FALSE;panel_tray_sync(); g_object_ref(host->page); gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(host->page)),host->page);
 if(host->pane) gtk_paned_pack2(GTK_PANED(host->home),host->page,FALSE,FALSE);
 else {
  GtkWidget *placeholder=g_object_get_data(G_OBJECT(host->page),"insight-dock-placeholder");if(placeholder) { gtk_widget_destroy(placeholder);g_object_set_data(G_OBJECT(host->page),"insight-dock-placeholder",NULL); }
  gint position=0; for(gint n=0;n<gtk_notebook_get_n_pages(GTK_NOTEBOOK(host->home));n++) { GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(host->home),n); guint order=(guint)n;
   for(guint j=0;j<wb.host_count;j++) if(wb.hosts[j].page==page && wb.hosts[j].home==host->home) { order=wb.hosts[j].order; break; }
   if(order<host->order) position++;
  }
  gtk_notebook_insert_page(GTK_NOTEBOOK(host->home),host->page,gtk_label_new(host->label),position);
 }
 g_object_unref(host->page); gtk_widget_show(host->page);if(!panel_window_count(window)) gtk_widget_destroy(window);else panel_window_refresh(window);
 panel_controls_update(host);
}
