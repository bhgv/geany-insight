/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Panel docking and palette placement.
 * RU: Стыковка панелей и размещение палитры. */

static void dock_all_panels(void) { for(guint i=0;i<wb.host_count;i++) dock_panel(i); }
static void dock_panel_selected(guint index) {
 dock_panel(index);if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index];
 if(!host->pane) gtk_notebook_set_current_page(GTK_NOTEBOOK(host->home),gtk_notebook_page_num(GTK_NOTEBOOK(host->home),host->page));
}
static gboolean panel_closed(GtkWidget *widget,GdkEvent *event,gpointer data) {
 (void)event;(void)data;for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==widget) dock_panel_selected(i);return TRUE;
}
static void detach_panel(guint index) {
 if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index]; if(host->window) { panel_restore(index); return; }
 GtkWidget *parent=gtk_widget_get_parent(host->page); if(parent!=host->home) return;host->minimized=FALSE;
 host->window=gtk_window_new(GTK_WINDOW_TOPLEVEL); gchar *title=g_strdup_printf("Geany Insight — %s",host->label); gtk_window_set_title(GTK_WINDOW(host->window),title); g_free(title);
 gtk_window_set_transient_for(GTK_WINDOW(host->window),GTK_WINDOW(main_widgets.window)); gtk_window_set_default_size(GTK_WINDOW(host->window),host->width>=300 ? host->width : (host->pane ? 900 : 480),host->height>=200 ? host->height : 600);
 g_object_ref(host->page); gtk_container_remove(GTK_CONTAINER(host->home),host->page); gtk_container_add(GTK_CONTAINER(host->window),host->page); g_object_unref(host->page);
 if(index==8) { GtkWidget *placeholder=gtk_label_new("File Symbols is in a floating window");gtk_notebook_insert_page(GTK_NOTEBOOK(host->home),placeholder,gtk_label_new("File Symbols"),0);g_object_set_data(G_OBJECT(host->page),"insight-dock-placeholder",placeholder);gtk_widget_show(placeholder); }
 if(wb.navigation_accels) gtk_window_add_accel_group(GTK_WINDOW(host->window),wb.navigation_accels);
 if(wb.lua_accels) gtk_window_add_accel_group(GTK_WINDOW(host->window),wb.lua_accels);
 g_signal_connect(host->window,"delete-event",G_CALLBACK(panel_closed),GUINT_TO_POINTER(index));
 g_signal_connect(host->window,"window-state-event",G_CALLBACK(panel_window_state),GUINT_TO_POINTER(index));
 g_signal_connect(host->window,"configure-event",G_CALLBACK(panel_configured),GUINT_TO_POINTER(index));
 insight_appearance_decorate(host->window);panel_controls_update(host); gtk_widget_show_all(host->window);
 if(host->width>=300) { GdkMonitor *monitor=gdk_display_get_monitor_at_point(gdk_display_get_default(),host->x,host->y); if(monitor) { GdkRectangle area; gdk_monitor_get_workarea(monitor,&area); host->width=MIN(host->width,MAX(300,area.width)); host->height=MIN(host->height,MAX(200,area.height)); host->x=CLAMP(host->x,area.x,area.x+MAX(0,area.width-host->width)); host->y=CLAMP(host->y,area.y,area.y+MAX(0,area.height-host->height)); gtk_window_resize(GTK_WINDOW(host->window),host->width,host->height); } gtk_window_move(GTK_WINDOW(host->window),host->x,host->y); }
 if(host->maximized) gtk_window_maximize(GTK_WINDOW(host->window));
}
static void panel_restore_command(GtkButton *button,gpointer data) { (void)button;panel_restore(GPOINTER_TO_UINT(data)); }
static void panel_restore(guint index) {
 if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index];if(!host->window) return;
 gboolean maximized=host->maximized;for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==host->window) wb.hosts[i].minimized=FALSE;gtk_window_deiconify(GTK_WINDOW(host->window));gtk_widget_show(host->window);if(maximized) gtk_window_maximize(GTK_WINDOW(host->window));gtk_window_present(GTK_WINDOW(host->window));panel_tray_sync();
}
static void panel_minimize(guint index) {
 if(index>=wb.host_count) return;
 PanelHost *host=&wb.hosts[index];if(!host->window || host->minimized) return;
 for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window==host->window) { panel_geometry_capture(&wb.hosts[i]);wb.hosts[i].minimized=TRUE; }
 gtk_window_deiconify(GTK_WINDOW(host->window));gtk_widget_hide(host->window);panel_tray_sync();
}
static void panel_minimize_command(GtkButton *button,gpointer data) { (void)button;panel_minimize(GPOINTER_TO_UINT(data)); }
static void panel_tray_sync(void) {
 if(!wb.tray) return;
 guint count=0;
 for(guint i=0;i<wb.host_count;i++) {
  PanelHost *host=&wb.hosts[i];gboolean minimized=host->window && host->minimized && panel_window_owner(host->window)==i;
  if(minimized && !host->tray_button) {
   host->tray_button=gtk_button_new_with_label(host->label);gchar *name=g_strdup_printf("insight-panel-tray-%u",i);gtk_widget_set_name(host->tray_button,name);g_free(name);
   gtk_widget_set_tooltip_text(host->tray_button,"Restore this floating panel");gtk_box_pack_start(GTK_BOX(wb.tray_buttons),host->tray_button,FALSE,FALSE,0);g_signal_connect(host->tray_button,"clicked",G_CALLBACK(panel_restore_command),GUINT_TO_POINTER(i));
  }
  if(host->tray_button) { guint members=panel_window_count(host->window);gchar *label=members>1 ? g_strdup_printf("%s +%u",insight_tr(host->label),members-1) : g_strdup(insight_tr(host->label));gtk_button_set_label(GTK_BUTTON(host->tray_button),label);g_free(label);gtk_widget_set_visible(host->tray_button,minimized); }
  if(minimized) count++;
 }
 gtk_widget_set_visible(wb.tray,wb.tray_enabled && count>0);
}
static void panel_tray_toggled(GtkCheckMenuItem *item,gpointer data) { (void)data;wb.tray_enabled=gtk_check_menu_item_get_active(item);panel_tray_sync(); }
static void panel_restore_all(GtkMenuItem *item,gpointer data) { (void)item;(void)data;for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].minimized) panel_restore(i); }
static void panel_tray_create(void) {
 wb.tray=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_name(wb.tray,"insight-panel-tab-tray");gtk_widget_set_no_show_all(wb.tray,TRUE);
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(wb.tray),GTK_POLICY_AUTOMATIC,GTK_POLICY_NEVER);
 wb.tray_buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_container_add(GTK_CONTAINER(wb.tray),wb.tray_buttons);gtk_widget_show(wb.tray_buttons);
 GtkWidget *box=ui_lookup_widget(main_widgets.window,"vbox1");gtk_box_pack_start(GTK_BOX(box),wb.tray,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(box),wb.tray,2);
 wb.tray_toggle=gtk_check_menu_item_new_with_label("Tab Tray");gtk_widget_set_name(wb.tray_toggle,"insight-panel-tab-tray-toggle");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.tray_toggle),wb.tray_enabled);g_signal_connect(wb.tray_toggle,"toggled",G_CALLBACK(panel_tray_toggled),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(wb.panel_commands),wb.tray_toggle);
 GtkWidget *restore=gtk_menu_item_new_with_label("Restore minimized panels");gtk_widget_set_name(restore,"insight-panel-restore-all");g_signal_connect(restore,"activate",G_CALLBACK(panel_restore_all),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(wb.panel_commands),restore);gtk_widget_show(wb.tray_toggle);gtk_widget_show(restore);
}
static void panel_toggle_docking(GtkButton *button,gpointer data) {
 (void)button;guint index=GPOINTER_TO_UINT(data);
 if(wb.hosts[index].window) dock_panel_selected(index);else detach_panel(index);
}
static void panel_toggle_maximized(GtkButton *button,gpointer data) {
 (void)button;PanelHost *host=&wb.hosts[GPOINTER_TO_UINT(data)];if(!host->window) return;
 if(gtk_window_is_maximized(GTK_WINDOW(host->window))) gtk_window_unmaximize(GTK_WINDOW(host->window));
 else gtk_window_maximize(GTK_WINDOW(host->window));
}
static void panel_opacity_changed(GtkScaleButton *button,gdouble value,gpointer data) {
 (void)button;if(wb.panel_group_setting) return;PanelHost *host=&wb.hosts[GPOINTER_TO_UINT(data)];gint opacity=CLAMP((gint)(value+0.5),40,100);
 wb.panel_group_setting=TRUE;
 for(guint i=0;i<wb.host_count;i++) if(i==GPOINTER_TO_UINT(data) || (host->window && wb.hosts[i].window==host->window)) { wb.hosts[i].opacity=opacity;gtk_scale_button_set_value(GTK_SCALE_BUTTON(wb.hosts[i].opacity_button),opacity);panel_controls_update(&wb.hosts[i]); }
 wb.panel_group_setting=FALSE;
}
static void panel_transparency_toggled(GtkCheckMenuItem *item,gpointer data) {
 (void)data;wb.transparent_panels=gtk_check_menu_item_get_active(item);for(guint i=0;i<wb.host_count;i++) panel_controls_update(&wb.hosts[i]);
}
static void panel_group_join(guint source,guint target) {
 if(source>=8 || target>=8 || source==target) return;
 PanelHost *from=&wb.hosts[source],*to=&wb.hosts[target];
 if(!from->window) detach_panel(source);
 if(!to->window) detach_panel(target);
 if(!from->window || !to->window) return;
 if(from->window==to->window) { GtkWidget *book=gtk_widget_get_parent(from->page);if(GTK_IS_NOTEBOOK(book)) gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),from->page));return; }
 GtkWidget *old=from->window,*window=to->window,*book=g_object_get_data(G_OBJECT(window),"insight-floating-group");
 if(!book) {
  book=gtk_notebook_new();gtk_widget_set_name(book,"insight-floating-group");gtk_notebook_set_scrollable(GTK_NOTEBOOK(book),TRUE);
  g_object_ref(to->page);gtk_container_remove(GTK_CONTAINER(window),to->page);gtk_container_add(GTK_CONTAINER(window),book);gtk_notebook_append_page(GTK_NOTEBOOK(book),to->page,gtk_label_new(to->label));gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(book),to->page,TRUE);g_object_unref(to->page);g_object_set_data(G_OBJECT(window),"insight-floating-group",book);
 }
 g_object_ref(from->page);gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(from->page)),from->page);from->window=window;from->minimized=to->minimized;from->maximized=to->maximized;from->opacity=to->opacity;from->x=to->x;from->y=to->y;from->width=to->width;from->height=to->height;
 gtk_notebook_append_page(GTK_NOTEBOOK(book),from->page,gtk_label_new(from->label));gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(book),from->page,TRUE);g_object_unref(from->page);gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),from->page));
 gtk_scale_button_set_value(GTK_SCALE_BUTTON(from->opacity_button),to->opacity);
 if(!panel_window_count(old)) gtk_widget_destroy(old);else panel_window_refresh(old);
 gtk_widget_show_all(book);panel_geometry_capture(from);panel_window_refresh(window);panel_restore(target);
}
static void panel_group_capture(guint index,InsightLayout *record) {
 PanelHost *host=&wb.hosts[index];if(index>=8 || panel_window_count(host->window)<2) return;
 GtkWidget *book=gtk_widget_get_parent(host->page);if(!GTK_IS_NOTEBOOK(book)) return;
 record->groups[index]=(gint)panel_window_owner(host->window)+1;record->group_order[index]=gtk_notebook_page_num(GTK_NOTEBOOK(book),host->page);
 GtkWidget *active=gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),gtk_notebook_get_current_page(GTK_NOTEBOOK(book)));
 for(guint i=0;i<8;i++) if(wb.hosts[i].page==active) record->group_active[index]=(gint)i+1;
}
static void panel_groups_apply(const InsightLayout *record) {
 for(guint i=0;i<8;i++) if(record->groups[i] && record->groups[i]!=(gint)i+1) panel_group_join(i,(guint)record->groups[i]-1);
 for(guint root=0;root<8;root++) if(record->groups[root]==(gint)root+1) {
  GtkWidget *book=gtk_widget_get_parent(wb.hosts[root].page);
  for(guint rank=0;rank<8;rank++) for(guint i=0;i<8;i++) if(record->groups[i]==(gint)root+1 && record->group_order[i]==(gint)rank) gtk_notebook_reorder_child(GTK_NOTEBOOK(book),wb.hosts[i].page,(gint)rank);
  guint active=(guint)record->group_active[root]-1;gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),wb.hosts[active].page));
 }
}
static void panel_group_command(GtkMenuItem *item,gpointer data) { (void)item;guint pair=GPOINTER_TO_UINT(data);panel_group_join(pair/16,pair%16); }
static void panel_ungroup_command(GtkMenuItem *item,gpointer data) { (void)item;guint index=GPOINTER_TO_UINT(data);if(index>=8 || panel_window_count(wb.hosts[index].window)<2) return;dock_panel(index);detach_panel(index); }
typedef struct { guint source,target;guint64 epoch; } PanelDrop;
static gboolean panel_drop_apply(gpointer data) {
 PanelDrop *drop=data;if(wb.alive && drop->epoch==workbench_epoch) panel_group_join(drop->source,drop->target);return G_SOURCE_REMOVE;
}
static void panel_drag_data(GtkWidget *widget,GdkDragContext *context,GtkSelectionData *selection,guint info,guint time,gpointer data) {
 (void)widget;(void)context;(void)info;(void)time;guchar index=(guchar)GPOINTER_TO_UINT(data);
 gtk_selection_data_set(selection,gdk_atom_intern_static_string("application/x-geany-insight-panel"),8,&index,1);
}
static void panel_drag_received(GtkWidget *widget,GdkDragContext *context,gint x,gint y,GtkSelectionData *selection,guint info,guint time,gpointer data) {
 (void)widget;(void)x;(void)y;(void)info;guint target=GPOINTER_TO_UINT(data);const guchar *bytes=gtk_selection_data_get_data(selection);gboolean valid=FALSE;guint source=8;
 if(wb.alive && target<8 && gtk_selection_data_get_length(selection)==1 && gtk_selection_data_get_format(selection)==8 && gtk_selection_data_get_data_type(selection)==gdk_atom_intern_static_string("application/x-geany-insight-panel") && bytes) {
  source=bytes[0];valid=source<8 && source!=target && wb.hosts[source].window && gtk_drag_get_source_widget(context)==wb.hosts[source].drag_handle;
 }
 gtk_drag_finish(context,valid,FALSE,time);
 if(valid) { PanelDrop *drop=g_new(PanelDrop,1);*drop=(PanelDrop){source,target,workbench_epoch};g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,panel_drop_apply,drop,g_free); }
}
static void panel_drag_attach(guint index,GtkWidget *header) {
 if(index>=8) return;
 PanelHost *host=&wb.hosts[index];GtkTargetEntry targets[]={{"application/x-geany-insight-panel",GTK_TARGET_SAME_APP,0}};
 host->drag_handle=gtk_event_box_new();gtk_widget_set_name(host->drag_handle,"insight-panel-drag-handle");gtk_widget_set_no_show_all(host->drag_handle,TRUE);gtk_widget_set_size_request(host->drag_handle,22,22);gtk_widget_set_tooltip_text(host->drag_handle,"Drag this panel onto a project panel header");
 GtkWidget *image=gtk_image_new_from_icon_name("view-grid-symbolic",GTK_ICON_SIZE_MENU);gtk_container_add(GTK_CONTAINER(host->drag_handle),image);gtk_widget_show(image);gtk_box_pack_start(GTK_BOX(header),host->drag_handle,FALSE,FALSE,0);
 gtk_drag_source_set(host->drag_handle,GDK_BUTTON1_MASK,targets,G_N_ELEMENTS(targets),GDK_ACTION_MOVE);gtk_drag_source_set_icon_name(host->drag_handle,"tab-new-symbolic");g_signal_connect(host->drag_handle,"drag-data-get",G_CALLBACK(panel_drag_data),GUINT_TO_POINTER(index));
 host->drag_target=gtk_event_box_new();gtk_event_box_set_visible_window(GTK_EVENT_BOX(host->drag_target),TRUE);gtk_widget_set_name(host->drag_target,"insight-panel-drag-target");
 g_object_ref(header);gtk_container_remove(GTK_CONTAINER(host->page),header);gtk_container_add(GTK_CONTAINER(host->drag_target),header);g_object_unref(header);gtk_box_pack_start(GTK_BOX(host->page),host->drag_target,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(host->page),host->drag_target,0);
 gtk_drag_dest_set(host->drag_target,GTK_DEST_DEFAULT_ALL,targets,G_N_ELEMENTS(targets),GDK_ACTION_MOVE);g_signal_connect(host->drag_target,"drag-data-received",G_CALLBACK(panel_drag_received),GUINT_TO_POINTER(index));
 GtkCssProvider *style=gtk_css_provider_new();gtk_css_provider_load_from_data(style,"#insight-panel-drag-target:drop(active) { background-color: @theme_selected_bg_color; }",-1,NULL);gtk_style_context_add_provider(gtk_widget_get_style_context(host->drag_target),GTK_STYLE_PROVIDER(style),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);g_object_unref(style);
 gtk_widget_show_all(host->drag_target);
}
static void panel_controls_attach(guint index) {
 PanelHost *host=&wb.hosts[index];GtkWidget *header=NULL;
 GList *children=gtk_container_get_children(GTK_CONTAINER(host->page));
 for(GList *p=children;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-panel-help-header")) { header=p->data;break; }
 g_list_free(children);
 if(index==9) header=g_object_get_data(G_OBJECT(wb.box),"insight-context-rail");
 if(!header) { header=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_widget_set_name(header,"insight-panel-controls-header");gtk_box_pack_start(GTK_BOX(host->page),header,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(host->page),header,0); }
 const gchar *opacity_icons[]={"display-brightness-symbolic",NULL};
 host->opacity_button=gtk_scale_button_new(GTK_ICON_SIZE_MENU,40,100,5,opacity_icons);gtk_widget_set_name(host->opacity_button,"insight-panel-opacity");gtk_widget_set_tooltip_text(host->opacity_button,"Floating panel opacity");gtk_widget_set_no_show_all(host->opacity_button,TRUE);gtk_scale_button_set_value(GTK_SCALE_BUTTON(host->opacity_button),host->opacity);gtk_button_set_relief(GTK_BUTTON(host->opacity_button),GTK_RELIEF_NONE);gtk_widget_set_size_request(host->opacity_button,22,22);gtk_box_pack_end(GTK_BOX(header),host->opacity_button,FALSE,FALSE,0);g_signal_connect(host->opacity_button,"value-changed",G_CALLBACK(panel_opacity_changed),GUINT_TO_POINTER(index));
 host->dock_button=gtk_button_new();host->maximize_button=gtk_button_new();host->minimize_button=gtk_button_new();gtk_widget_set_no_show_all(host->maximize_button,TRUE);gtk_widget_set_no_show_all(host->minimize_button,TRUE);
 GtkWidget *buttons[]={host->dock_button,host->maximize_button,host->minimize_button};
 for(guint n=0;n<G_N_ELEMENTS(buttons);n++) { gtk_button_set_relief(GTK_BUTTON(buttons[n]),GTK_RELIEF_NONE);gtk_widget_set_size_request(buttons[n],22,22);gtk_box_pack_end(GTK_BOX(header),buttons[n],FALSE,FALSE,0); }
 gtk_widget_set_name(host->minimize_button,"insight-panel-minimize");gtk_button_set_image(GTK_BUTTON(host->minimize_button),gtk_image_new_from_icon_name("window-minimize-symbolic",GTK_ICON_SIZE_MENU));gtk_widget_set_tooltip_text(host->minimize_button,"Minimize panel to Tab Tray");
 gtk_widget_set_name(host->dock_button,"insight-panel-toggle-dock");gtk_widget_set_name(host->maximize_button,"insight-panel-toggle-maximize");
 g_signal_connect(host->minimize_button,"clicked",G_CALLBACK(panel_minimize_command),GUINT_TO_POINTER(index));
 g_signal_connect(host->dock_button,"clicked",G_CALLBACK(panel_toggle_docking),GUINT_TO_POINTER(index));
 g_signal_connect(host->maximize_button,"clicked",G_CALLBACK(panel_toggle_maximized),GUINT_TO_POINTER(index));
 if(index<8) {
  host->group_button=gtk_menu_button_new();gtk_widget_set_name(host->group_button,"insight-panel-group-button");gtk_widget_set_tooltip_text(host->group_button,"Group this floating panel");gtk_button_set_image(GTK_BUTTON(host->group_button),gtk_image_new_from_icon_name("tab-new-symbolic",GTK_ICON_SIZE_MENU));gtk_button_set_relief(GTK_BUTTON(host->group_button),GTK_RELIEF_NONE);gtk_widget_set_no_show_all(host->group_button,TRUE);gtk_box_pack_end(GTK_BOX(header),host->group_button,FALSE,FALSE,0);
  GtkWidget *menu=gtk_menu_new();gtk_menu_button_set_popup(GTK_MENU_BUTTON(host->group_button),menu);
  for(guint target=0;target<8;target++) if(target!=index) { GtkWidget *item=gtk_menu_item_new_with_label(wb.hosts[target].label);gchar *name=g_strdup_printf("insight-panel-group-with-%u",target);gtk_widget_set_name(item,name);g_free(name);g_signal_connect(item,"activate",G_CALLBACK(panel_group_command),GUINT_TO_POINTER(index*16+target));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
  host->ungroup_item=gtk_menu_item_new_with_label("Float this panel separately");gtk_widget_set_name(host->ungroup_item,"insight-panel-ungroup");g_signal_connect(host->ungroup_item,"activate",G_CALLBACK(panel_ungroup_command),GUINT_TO_POINTER(index));gtk_menu_shell_append(GTK_MENU_SHELL(menu),host->ungroup_item);gtk_widget_show_all(menu);
 }
 panel_drag_attach(index,header);panel_controls_update(host);gtk_widget_show_all(header);
}
static void float_panel_command(GtkMenuItem *item,gpointer data) { (void)item; detach_panel(GPOINTER_TO_UINT(data)); }
static void dock_panels_command(GtkMenuItem *item,gpointer data) { (void)item; (void)data; dock_all_panels(); }
static GtkWidget *narrow_panel_host(GtkWidget *child);
static void constrain_panel_rows(GtkWidget *widget);
static void register_panels(void) {
 wb.host_count=10;panel_tray_create();
 wb.transparency_toggle=gtk_check_menu_item_new_with_label("Enable transparent panels");gtk_widget_set_name(wb.transparency_toggle,"insight-panel-transparency-toggle");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.transparency_toggle),wb.transparent_panels);g_signal_connect(wb.transparency_toggle,"toggled",G_CALLBACK(panel_transparency_toggled),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(wb.panel_commands),wb.transparency_toggle);gtk_widget_show(wb.transparency_toggle);
 for(guint i=0;i<8;i++) { PanelHost *host=&wb.hosts[i]; host->home=wb.project_notebook; host->page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(host->home),(gint)i); host->order=i; host->label=g_strdup(gtk_notebook_get_tab_label_text(GTK_NOTEBOOK(host->home),host->page)); }
 PanelHost *left=&wb.hosts[8]; left->home=main_widgets.sidebar_notebook; left->page=wb.local_entry; while(gtk_widget_get_parent(left->page)!=left->home) left->page=gtk_widget_get_parent(left->page); left->order=(guint)gtk_notebook_page_num(GTK_NOTEBOOK(left->home),left->page); left->label=g_strdup("File Symbols");
 PanelHost *context=&wb.hosts[9]; context->home=main_widgets.message_window_notebook; context->page=wb.box; context->order=(guint)gtk_notebook_page_num(GTK_NOTEBOOK(context->home),wb.box); context->pane=FALSE; context->label=g_strdup("Context");
 const gchar *topics[]={"panel-project-files.md","panel-project-symbols.md","panel-project-folders.md","panel-relations.md","panel-categories.md","panel-file-types.md","panel-bookmarks.md","panel-clips.md","panel-file-symbols.md","panel-context.md"};
 for(guint i=0;i<wb.host_count;i++) { PanelHost *host=&wb.hosts[i]; if(i<9) {
   /* Wide controls must not impose their minimum on either side splitter.
    * Keep panel help outside the viewport so it stays at the visible corner. */
   GtkWidget *old=host->page,*box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
   gint position=gtk_notebook_page_num(GTK_NOTEBOOK(host->home),old);
   g_object_ref(old); gtk_container_remove(GTK_CONTAINER(host->home),old);
   compact_panel_control(old);constrain_panel_rows(old);gtk_box_pack_start(GTK_BOX(box),narrow_panel_host(old),TRUE,TRUE,0);
   g_object_unref(old);gtk_notebook_insert_page(GTK_NOTEBOOK(host->home),box,gtk_label_new(host->label),position);
   GtkWidget *tab=gtk_notebook_get_tab_label(GTK_NOTEBOOK(host->home),box);if(GTK_IS_LABEL(tab)) { gtk_label_set_ellipsize(GTK_LABEL(tab),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(tab),1); }host->page=box; gtk_widget_show_all(box);
  } if(i!=9) insight_panel_help_attach(host->page,topics[i]);g_object_set_data(G_OBJECT(host->page),"insight-panel-id",GUINT_TO_POINTER(i+1)); panel_controls_attach(i); }
 insight_panel_help_attach(wb.search_page,"panel-search-results.md");
 insight_panel_help_notebook(GTK_NOTEBOOK(main_widgets.sidebar_notebook),"panel-native-symbols.md"); insight_panel_help_notebook(GTK_NOTEBOOK(main_widgets.message_window_notebook),"panel-messages.md"); insight_panel_help_notebook(GTK_NOTEBOOK(main_widgets.notebook),"panel-editor.md");
 GtkWidget *floating=gtk_menu_item_new_with_label("Float individual panel"),*menu=gtk_menu_new(); gtk_menu_item_set_submenu(GTK_MENU_ITEM(floating),menu); gtk_container_add(GTK_CONTAINER(wb.panel_commands),floating);
 for(guint i=0;i<wb.host_count;i++) { GtkWidget *item=gtk_menu_item_new_with_label(wb.hosts[i].label); gtk_container_add(GTK_CONTAINER(menu),item); g_signal_connect(item,"activate",G_CALLBACK(float_panel_command),GUINT_TO_POINTER(i)); }
 GtkWidget *dock=gtk_menu_item_new_with_label("Dock all individual panels"); gtk_container_add(GTK_CONTAINER(wb.panel_commands),dock); g_signal_connect(dock,"activate",G_CALLBACK(dock_panels_command),NULL); gtk_widget_show_all(floating); gtk_widget_show(dock);
}
static guint visible_host_mask(void) { guint mask=0; for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window && gtk_widget_get_visible(wb.hosts[i].window)) mask|=1u<<i; return mask; }
static guint detached_mask(void) { guint mask=0; for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window) mask|=1u<<i; return mask; }
static gint project_tab_identity(void) {
 gint page=gtk_notebook_get_current_page(GTK_NOTEBOOK(wb.project_notebook)); GtkWidget *widget=page>=0 ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(wb.project_notebook),page) : NULL;
 for(guint i=0;i<MIN(8u,wb.host_count);i++) if(wb.hosts[i].page==widget) return (gint)i;
 return MAX(0,page);
}
static void select_project_tab(gint identity) {
 if(wb.host_count && identity>=0 && identity<8) { GtkWidget *page=wb.hosts[identity].page; gint index=gtk_notebook_page_num(GTK_NOTEBOOK(wb.project_notebook),page); if(index>=0) gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),index); }
 else gtk_notebook_set_current_page(GTK_NOTEBOOK(wb.project_notebook),identity);
}
static void set_panel_visibility(gboolean left,gboolean project,gboolean context) {
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.local_toggle),left); local_visible(GTK_CHECK_MENU_ITEM(wb.local_toggle),NULL);
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.project_toggle),project); project_visible(GTK_CHECK_MENU_ITEM(wb.project_toggle),NULL);
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle),context); context_visible(GTK_CHECK_MENU_ITEM(wb.context_toggle),NULL);
}
static void main_geometry_capture(void) {
 if(wb.main_apply_source) return;
 GtkWindow *window=GTK_WINDOW(main_widgets.window);GdkWindow *native=gtk_widget_get_window(GTK_WIDGET(window));
 if(native && !(gdk_window_get_state(native)&(GDK_WINDOW_STATE_MAXIMIZED|GDK_WINDOW_STATE_FULLSCREEN|GDK_WINDOW_STATE_ICONIFIED))) {
  gint width,height;gtk_window_get_size(window,&width,&height);
  if(width>=400 && height>=220) { gtk_window_get_position(window,&wb.main_bounds.x,&wb.main_bounds.y);wb.main_bounds.width=width;wb.main_bounds.height=height;wb.main_bounds.set=TRUE; }
 }
 wb.main_bounds.maximized=gtk_window_is_maximized(window);
}
static gboolean main_geometry_changed(GtkWidget *widget,GdkEvent *event,gpointer data) { (void)widget;(void)event;(void)data;main_geometry_capture();return FALSE; }
/* Window managers apply maximize requests asynchronously. Resize the normal
 * window first so a later Restore uses the saved bounds, not the previous layout. */
static gboolean main_geometry_apply_step(gpointer data) {
 (void)data;GtkWindow *window=GTK_WINDOW(main_widgets.window);InsightPanelLayout *target=&wb.main_target;
 if(wb.main_apply_stage==1) {
  if(gtk_window_is_maximized(window) && wb.main_apply_ticks++<40) return G_SOURCE_CONTINUE;
  gtk_window_resize(window,target->width,target->height);gtk_window_move(window,target->x,target->y);wb.main_apply_stage=2;wb.main_apply_ticks=0;return G_SOURCE_CONTINUE;
 }
 gint width,height;gtk_window_get_size(window,&width,&height);GtkRequisition minimum;gtk_widget_get_preferred_size(GTK_WIDGET(window),&minimum,NULL);
 if((width!=MAX(target->width,minimum.width) || height!=MAX(target->height,minimum.height)) && wb.main_apply_ticks++<20) return G_SOURCE_CONTINUE;
 wb.main_apply_source=0;main_geometry_capture();wb.main_bounds.maximized=target->maximized;
 if(target->maximized) gtk_window_maximize(window);
 return G_SOURCE_REMOVE;
}
static void main_geometry_apply(const InsightPanelLayout *saved) {
 if(!saved->set) return;
 InsightPanelLayout next=*saved;GdkDisplay *display=gtk_widget_get_display(main_widgets.window);GdkMonitor *monitor=gdk_display_get_monitor_at_point(display,next.x,next.y);
 if(!monitor && gdk_display_get_n_monitors(display)) monitor=gdk_display_get_monitor(display,0);
 if(monitor) { GdkRectangle area;gdk_monitor_get_workarea(monitor,&area);next.width=MIN(next.width,MAX(1,area.width));next.height=MIN(next.height,MAX(1,area.height));next.x=CLAMP(next.x,area.x,area.x+MAX(0,area.width-next.width));next.y=CLAMP(next.y,area.y,area.y+MAX(0,area.height-next.height)); }
 if(wb.main_apply_source) g_source_remove(wb.main_apply_source);
 wb.main_bounds=next;wb.main_target=next;wb.main_apply_stage=1;wb.main_apply_ticks=0;
 gtk_window_unmaximize(GTK_WINDOW(main_widgets.window));wb.main_apply_source=g_timeout_add(25,main_geometry_apply_step,NULL);
}
static gboolean capture_layout(InsightLayout *out) {
 if(wb.float_window) { gtk_window_get_position(GTK_WINDOW(wb.float_window),&wb.float_x,&wb.float_y); gtk_window_get_size(GTK_WINDOW(wb.float_window),&wb.float_width,&wb.float_height); }
 main_geometry_capture();InsightLayout record={.valid=TRUE};gint *v=record.values;
 record.main_window=wb.main_bounds;
 if(!record.main_window.set) { record.main_window.set=TRUE;record.main_window.width=900;record.main_window.height=700; }
 record.main_window.x=CLAMP(record.main_window.x,-30000,30000);record.main_window.y=CLAMP(record.main_window.y,-30000,30000);
 v[IL_LEFT]=ui_prefs.sidebar_visible; v[IL_PROJECT]=gtk_widget_get_visible(wb.project_notebook); v[IL_CONTEXT]=wb.context_visible;
 v[IL_FOLLOW]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow)); v[IL_WRAP]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.wrap)); v[IL_FLOAT]=wb.float_window!=NULL;
 v[IL_LEFT_SPLIT]=gtk_paned_get_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1"))); v[IL_RIGHT_SPLIT]=gtk_paned_get_position(GTK_PANED(wb.right_pane)); v[IL_CONTEXT_SPLIT]=gtk_paned_get_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"vpaned1"))); v[IL_TAB]=project_tab_identity(); v[IL_DETACHED]=(gint)detached_mask(); v[IL_HOST_VISIBLE]=(gint)visible_host_mask();
 v[IL_TRANSPARENT_PANELS]=wb.transparent_panels;v[IL_TRAY_VISIBLE]=wb.tray_enabled;for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window && wb.hosts[i].minimized) v[IL_HOST_MINIMIZED]|=1<<i;
 v[IL_X]=CLAMP(wb.float_x,-30000,30000); v[IL_Y]=CLAMP(wb.float_y,-30000,30000); v[IL_WIDTH]=wb.float_width>=300 ? MIN(wb.float_width,4000) : 440; v[IL_HEIGHT]=wb.float_height>=200 ? MIN(wb.float_height,4000) : 620;
 for(guint i=0;i<wb.host_count;i++) {
  PanelHost *host=&wb.hosts[i];panel_geometry_capture(host);
  panel_group_capture(i,&record);record.opacity[i]=host->opacity;record.panels[i]=(InsightPanelLayout){TRUE,host->maximized,CLAMP(host->x,-30000,30000),CLAMP(host->y,-30000,30000),host->width>=300 ? MIN(host->width,4000) : 480,host->height>=200 ? MIN(host->height,4000) : 600};
 }
 insight_window_list_layout_capture(&record);
 if(!insight_layout_valid(&record)) return FALSE;
 *out=record;return TRUE;
}
