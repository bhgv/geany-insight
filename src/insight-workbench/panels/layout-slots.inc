/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Layout slots and layout state.
 * RU: Слоты раскладок и состояние панелей. */

static void save_layout(GtkMenuItem *item,gpointer data) {
 (void)item;guint slot=GPOINTER_TO_UINT(data);if(slot>=4) return;
 InsightLayout record;if(!capture_layout(&record)) { ui_set_statusbar(TRUE,"Layout cannot be saved: invalid panel geometry");return; }
 wb.layouts[slot]=record;ui_set_statusbar(TRUE,"Layout %c stored",'A'+(gint)slot);save_workbench_settings();
}
static void reset_layouts(GtkMenuItem *item,gpointer data) { (void)item; (void)data; memset(wb.layouts,0,sizeof wb.layouts); ui_set_statusbar(TRUE,"Saved layouts reset; A–D now use presets"); save_workbench_settings(); }
static void apply_layout_record(const InsightLayout *record) {
 wb.initial_right_split=-1;dock_all_panels();dock_project(NULL,NULL);main_geometry_apply(&record->main_window);
  const gint *v=record->values;
  wb.float_x=v[IL_X]; wb.float_y=v[IL_Y]; wb.float_width=v[IL_WIDTH]; wb.float_height=v[IL_HEIGHT];
  gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1")),v[IL_LEFT_SPLIT]); gtk_paned_set_position(GTK_PANED(wb.right_pane),v[IL_RIGHT_SPLIT]); gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"vpaned1")),v[IL_CONTEXT_SPLIT]);
  select_project_tab(v[IL_TAB]); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),v[IL_FOLLOW]); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.wrap),v[IL_WRAP]);
  if(v[IL_FLOAT]) float_project(NULL,NULL);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.transparency_toggle),v[IL_TRANSPARENT_PANELS]);
  for(guint i=0;i<wb.host_count;i++) {
   PanelHost *host=&wb.hosts[i];const InsightPanelLayout *p=&record->panels[i];host->opacity=record->opacity[i] ? record->opacity[i] : 100;gtk_scale_button_set_value(GTK_SCALE_BUTTON(host->opacity_button),host->opacity);
   if(p->set) { host->x=p->x;host->y=p->y;host->width=p->width;host->height=p->height;host->maximized=p->maximized; }
   if(v[IL_DETACHED]&(1<<i)) detach_panel(i);
  }
  panel_groups_apply(record);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.tray_toggle),v[IL_TRAY_VISIBLE]);
  for(guint i=0;i<wb.host_count;i++) if(v[IL_HOST_MINIMIZED]&(1<<i)) panel_minimize(i);
  insight_window_list_layout_apply(record);
  set_panel_visibility(v[IL_LEFT],v[IL_PROJECT],v[IL_CONTEXT]); for(guint i=0;i<wb.host_count;i++) if(wb.hosts[i].window) gtk_widget_set_visible(wb.hosts[i].window,!wb.hosts[i].minimized && (v[IL_HOST_VISIBLE]&(1<<i))!=0); return;
}
/* Preserve native source-window focus and document lifetimes. Only the tools
 * palette and already detachable panel hosts move to separate top-levels. */
typedef struct { GWeakRef widget;gint position;gboolean expand,fill;guint padding;GtkPackType pack; } PaletteRow;
typedef struct { GWeakRef sci;gulong allocated;gint first,x; } PaletteView;
typedef struct { GWeakRef window;InsightWindowMotion motion;gint chrome_width,chrome_height;InsightWindowRect last; } PaletteMotion;
static struct {
 gboolean active,joining,message_visible,animate,motions_ready;
 GtkWidget *window,*bottom,*bottom_parent,*bottom_content;gboolean bottom_resize,bottom_shrink;
 GPtrArray *rows,*views;guint view_timer,view_settle;InsightLayout before;
 InsightWindowRect area,origins[INSIGHT_FLOAT_COUNT],destinations[INSIGHT_FLOAT_COUNT];
 PaletteMotion motions[INSIGHT_FLOAT_COUNT];
 guint timer,settle_ticks,arrival_ticks;gint64 deadline,started;gdouble speed;
} palette;
/* Resizing Scintilla may scroll its caret into view. Preserve each document's
 * viewport through allocation, including inactive tabs, without moving carets. */
static void palette_view_restore(GtkWidget *widget,GtkAllocation *allocation,gpointer data) {
 (void)allocation;PaletteView *view=data;ScintillaObject *sci=SCINTILLA(widget);
 scintilla_send_message(sci,SCI_SETFIRSTVISIBLELINE,view->first,0);scintilla_send_message(sci,SCI_SETXOFFSET,view->x,0);
}
static void palette_view_free(gpointer data) {
 PaletteView *view=data;GtkWidget *sci=g_weak_ref_get(&view->sci);
 if(sci) {g_signal_handler_disconnect(sci,view->allocated);g_object_unref(sci);}g_weak_ref_clear(&view->sci);g_free(view);
}
static gboolean palette_views_settle(gpointer data) {
 (void)data;
 if(palette.timer || wb.main_apply_source) palette.view_settle=0;else palette.view_settle++;
 if(palette.view_settle<3) return G_SOURCE_CONTINUE;
 palette.view_timer=0;g_clear_pointer(&palette.views,g_ptr_array_unref);return G_SOURCE_REMOVE;
}
static void palette_views_capture(void) {
 if(palette.views) return;
 palette.views=g_ptr_array_new_with_free_func(palette_view_free);palette.view_settle=0;
 guint i;foreach_document(i) {
  ScintillaObject *sci=documents[i]->editor->sci;PaletteView *view=g_new0(PaletteView,1);g_weak_ref_init(&view->sci,sci);
  view->first=scintilla_send_message(sci,SCI_GETFIRSTVISIBLELINE,0,0);view->x=scintilla_send_message(sci,SCI_GETXOFFSET,0,0);
  view->allocated=g_signal_connect_after(sci,"size-allocate",G_CALLBACK(palette_view_restore),view);g_ptr_array_add(palette.views,view);
 }
 palette.view_timer=g_timeout_add(32,palette_views_settle,NULL);
}
static void palette_compact_title(GtkWidget *window,gboolean compact) {
 GtkStyleContext *style=gtk_widget_get_style_context(window);
 if(compact) gtk_style_context_add_class(style,"insight-independent-chrome");else gtk_style_context_remove_class(style,"insight-independent-chrome");
 GtkWidget *header=gtk_window_get_titlebar(GTK_WINDOW(window));if(!GTK_IS_HEADER_BAR(header)) return;
 if(compact) {
  if(!g_object_get_data(G_OBJECT(header),"insight-original-subtitle-space")) g_object_set_data(G_OBJECT(header),"insight-original-subtitle-space",GUINT_TO_POINTER(gtk_header_bar_get_has_subtitle(GTK_HEADER_BAR(header)) ? 2:1));
  gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(header),FALSE);
  if(!gtk_header_bar_get_custom_title(GTK_HEADER_BAR(header))) {GtkWidget *label=gtk_label_new(gtk_window_get_title(GTK_WINDOW(window)));gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(label),1);gtk_label_set_single_line_mode(GTK_LABEL(label),TRUE);g_object_set_data(G_OBJECT(label),"insight-compact-window-title",GINT_TO_POINTER(1));g_object_set_data(G_OBJECT(label),"insight-no-i18n",GINT_TO_POINTER(1));PangoAttrList *attrs=pango_attr_list_new();pango_attr_list_insert(attrs,pango_attr_size_new_absolute(7*PANGO_SCALE));gtk_label_set_attributes(GTK_LABEL(label),attrs);pango_attr_list_unref(attrs);gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header),label);gtk_widget_show(label);}

 } else {
  GtkWidget *custom=gtk_header_bar_get_custom_title(GTK_HEADER_BAR(header));if(custom && g_object_get_data(G_OBJECT(custom),"insight-compact-window-title")) gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header),NULL);
  guint previous=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(header),"insight-original-subtitle-space"));if(previous) gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(header),previous==2);g_object_set_data(G_OBJECT(header),"insight-original-subtitle-space",NULL);
 }
}
static void palette_row_free(gpointer data) { PaletteRow *row=data;g_weak_ref_clear(&row->widget);g_free(row); }
static GtkWidget *palette_part_window(guint part) {
 switch(part) {case INSIGHT_FLOAT_TOOLS:return palette.window;case INSIGHT_FLOAT_LEFT:return wb.hosts[8].window;case INSIGHT_FLOAT_RIGHT:return wb.float_window;case INSIGHT_FLOAT_BOTTOM:return palette.bottom;default:return main_widgets.window;}
}
static GtkWidget *palette_part_content(guint part) {
 switch(part) {case INSIGHT_FLOAT_TOOLS:return palette.window ? gtk_bin_get_child(GTK_BIN(palette.window)):NULL;case INSIGHT_FLOAT_LEFT:return wb.hosts[8].page;case INSIGHT_FLOAT_RIGHT:return wb.project_host;case INSIGHT_FLOAT_BOTTOM:return main_widgets.message_window_notebook;default:return main_widgets.window;}
}
static InsightWindowRect palette_frame(GtkWidget *widget) {
 InsightWindowRect rect={0,0,1,1};if(!GTK_IS_WINDOW(widget)) return rect;
 gtk_window_get_position(GTK_WINDOW(widget),&rect.x,&rect.y);gtk_window_get_size(GTK_WINDOW(widget),&rect.width,&rect.height);
 GdkWindow *native=gtk_widget_get_window(widget);
 /* Native CSD extents include the CSS shadow (84px per side with the material
  * theme). Only the visible titlebar/body frame belongs inside the workarea. */
 GtkWidget *body=gtk_bin_get_child(GTK_BIN(widget)),*title=gtk_window_get_titlebar(GTK_WINDOW(widget));
 if(native && title && body && gtk_widget_get_mapped(body)) {
  gint x=0,y=0,root_x=0,root_y=0;if(gtk_widget_translate_coordinates(body,widget,0,0,&x,&y)) {
   gdk_window_get_origin(native,&root_x,&root_y);
   return insight_window_visible_frame(rect.x,rect.y,(InsightWindowRect){root_x+x,root_y+y,gtk_widget_get_allocated_width(body),gtk_widget_get_allocated_height(body)});
  }
 }
 if(native) {GdkRectangle frame;gdk_window_get_frame_extents(native,&frame);if(frame.width>1 && frame.height>1) rect=(InsightWindowRect){frame.x,frame.y,frame.width,frame.height};}return rect;
}
static InsightWindowRect palette_widget_rect(GtkWidget *widget,InsightWindowRect fallback) {
 if(!widget || !gtk_widget_get_mapped(widget) || gtk_widget_get_allocated_width(widget)<2 || gtk_widget_get_allocated_height(widget)<2) return fallback;
 GtkWidget *top=gtk_widget_get_toplevel(widget);GdkWindow *native=gtk_widget_get_window(top);gint x=0,y=0,tx=0,ty=0;
 if(!native || !gtk_widget_translate_coordinates(widget,top,0,0,&x,&y)) return fallback;
 gdk_window_get_origin(native,&tx,&ty);return (InsightWindowRect){tx+x,ty+y,gtk_widget_get_allocated_width(widget),gtk_widget_get_allocated_height(widget)};
}
static void palette_cancel_motion(void) {
 if(palette.timer) {g_source_remove(palette.timer);palette.timer=0;}
 if(palette.motions_ready) {for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) g_weak_ref_clear(&palette.motions[n].window);palette.motions_ready=FALSE;}
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-independent-motion",NULL);
}
static gboolean palette_closed(GtkWidget *widget,GdkEvent *event,gpointer data) {
 (void)widget;(void)event;(void)data;insight_appearance_join_windows();return TRUE;
}
static void palette_restore_now(void) {
 palette_cancel_motion();if(!palette.active) return;palette.active=FALSE;palette.joining=FALSE;
 GtkWidget *root=ui_lookup_widget(main_widgets.window,"vbox1");
 for(guint i=0;palette.rows && i<palette.rows->len;i++) {
  PaletteRow *row=g_ptr_array_index(palette.rows,i);GtkWidget *widget=g_weak_ref_get(&row->widget);if(!widget) continue;
  GtkWidget *parent=gtk_widget_get_parent(widget);if(parent) gtk_container_remove(GTK_CONTAINER(parent),widget);
  if(row->pack==GTK_PACK_END) gtk_box_pack_end(GTK_BOX(root),widget,row->expand,row->fill,row->padding);else gtk_box_pack_start(GTK_BOX(root),widget,row->expand,row->fill,row->padding);
  gtk_box_reorder_child(GTK_BOX(root),widget,row->position);g_object_unref(widget);
 }
 if(palette.window) {gtk_widget_destroy(palette.window);palette.window=NULL;}g_clear_pointer(&palette.rows,g_ptr_array_unref);
 if(palette.bottom) {
  GtkWidget *content=palette.bottom_content;g_object_ref(content);GtkWidget *parent=gtk_widget_get_parent(content);if(parent) gtk_container_remove(GTK_CONTAINER(parent),content);
  gtk_paned_set_position(GTK_PANED(palette.bottom_parent),palette.before.values[IL_CONTEXT_SPLIT]);gtk_paned_pack2(GTK_PANED(palette.bottom_parent),content,palette.bottom_resize,palette.bottom_shrink);g_object_unref(content);gtk_widget_destroy(palette.bottom);palette.bottom=NULL;palette.bottom_content=NULL;g_clear_object(&palette.bottom_parent);
 }
 for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {GtkWidget *window=palette_part_window(n);if(window) palette_compact_title(window,FALSE);if(window && window!=main_widgets.window) gtk_window_set_transient_for(GTK_WINDOW(window),GTK_WINDOW(main_widgets.window));if(window) g_object_set_data(G_OBJECT(window),"insight-independent-opacity",NULL);}
 insight_appearance_opacity_tick(TRUE);apply_layout_record(&palette.before);ui_statusbar_mode(FALSE);gtk_widget_set_visible(main_widgets.message_window_notebook,palette.message_visible);
 /* Restore the global alpha after the main window loses its temporary tag. */
 insight_appearance_panel_opacity(main_widgets.window,100);
}
static void palette_move_frame(GtkWidget *window,PaletteMotion *motion,InsightWindowRect rect) {
 if(rect.width!=motion->last.width || rect.height!=motion->last.height) gtk_window_resize(GTK_WINDOW(window),MAX(1,rect.width-motion->chrome_width),MAX(1,rect.height-motion->chrome_height));
 if(rect.x!=motion->last.x || rect.y!=motion->last.y) gtk_window_move(GTK_WINDOW(window),rect.x,rect.y);motion->last=rect;
}
static void palette_prepare_motion(void) {
 InsightWindowRect from[INSIGHT_FLOAT_COUNT],to[INSIGHT_FLOAT_COUNT],sizes[INSIGHT_FLOAT_COUNT];
 for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {
  GtkWidget *window=palette_part_window(n);PaletteMotion *motion=&palette.motions[n];g_weak_ref_init(&motion->window,window);
  from[n]=palette_frame(window);sizes[n]=from[n];gint w=from[n].width,h=from[n].height;
  if(GTK_IS_WINDOW(window)) {gtk_window_get_size(GTK_WINDOW(window),&w,&h);}
  motion->chrome_width=from[n].width-w;motion->chrome_height=from[n].height-h;motion->last=from[n];
 }
 if(palette.joining) {
  for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {
   to[n]=palette.origins[n];GtkWidget *content=palette_part_content(n);
   if(n!=INSIGHT_FLOAT_EDITOR && content) {InsightWindowRect actual=palette_widget_rect(content,from[n]);to[n].x-=actual.x-from[n].x;to[n].y-=actual.y-from[n].y;to[n].width+=from[n].width-actual.width;to[n].height+=from[n].height-actual.height;}
   to[n]=insight_window_rect_clamp(to[n],palette.area);
  }
 } else {
  insight_window_edge_layout(palette.area,sizes,to);
  for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) palette.destinations[n]=to[n];
 }
 palette.motions_ready=TRUE;
}
static gboolean palette_motion_step(gpointer data) {
 (void)data;if(!wb.alive || !palette.active) {palette.timer=0;return G_SOURCE_REMOVE;}
 if(!palette.motions_ready) {
  insight_appearance_opacity_tick(FALSE);
  if(g_get_monotonic_time()<palette.deadline) return G_SOURCE_CONTINUE;
  if(!palette.joining && gtk_window_is_maximized(GTK_WINDOW(main_widgets.window)) && palette.settle_ticks++<40) return G_SOURCE_CONTINUE;
  palette_prepare_motion();
 }
 gboolean done=TRUE;
 for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {
  PaletteMotion *motion=&palette.motions[n];GtkWidget *window=g_weak_ref_get(&motion->window);if(!window) continue;
  palette_move_frame(window,motion,palette.destinations[n]);g_object_unref(window);
 }
 if(insight_appearance_opacity_tick(TRUE)) done=FALSE;
 if(!done) return G_SOURCE_CONTINUE;
 /* A move request is asynchronous. Re-read decorated frame bounds rather than
  * treating the last requested position as the window manager's final result.
  * Recompute anchors from actual sizes (CSD and theme minima can change them). */
 if(!palette.joining && palette.arrival_ticks++<12) {
  InsightWindowRect measured[INSIGHT_FLOAT_COUNT],targets[INSIGHT_FLOAT_COUNT];
  for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) measured[n]=palette_frame(palette_part_window(n));
  insight_window_edge_layout(palette.area,measured,targets);gboolean pending=FALSE;
  for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {
   GtkWidget *window=palette_part_window(n);if(!GTK_IS_WINDOW(window)) continue;
   gint dx=targets[n].x-measured[n].x,dy=targets[n].y-measured[n].y;
   if(ABS(dx)>1 || ABS(dy)>1) {gtk_window_move(GTK_WINDOW(window),targets[n].x,targets[n].y);pending=TRUE;}
  }
  if(pending || palette.arrival_ticks<3) return G_SOURCE_CONTINUE;
 }
 palette.timer=0;gboolean joining=palette.joining;palette_cancel_motion();if(joining) palette_restore_now();return G_SOURCE_REMOVE;
}
static void palette_queue_motion(gboolean joining) {
 palette_views_capture();palette_cancel_motion();palette.joining=joining;palette.animate=FALSE;insight_appearance_opacity_tick(TRUE);palette.settle_ticks=0;palette.arrival_ticks=0;palette.deadline=g_get_monotonic_time()+32000;
 g_object_set_data(G_OBJECT(main_widgets.window),"insight-independent-motion",GINT_TO_POINTER(1));palette.timer=g_timeout_add(16,palette_motion_step,NULL);
}
/* Fit the client content, not the outer CSD requisition: passing the latter
 * to gtk_window_resize counts decorations twice and leaves a blank shelf. */
static void palette_tools_fit(GtkWidget *box,GtkAllocation *allocation,gpointer data) {
 GtkWindow *window=GTK_WINDOW(data);gint minimum,natural,w,h;
 gtk_widget_get_preferred_height(box,&minimum,&natural);
 gint excess=allocation->height-natural;
 if(excess>1) {gtk_window_get_size(window,&w,&h);gtk_window_resize(window,w,MAX(1,h-excess));}
}
void insight_workbench_set_independent(gboolean active) {
 if(!wb.alive || wb.host_count!=10) return;
 for(guint n=0;n<wb.host_count;n++) panel_controls_update(&wb.hosts[n]);
 if(palette.active) {
  if(active) return;
  palette_views_capture();palette_restore_now();return;
 }
 if(!active) return;
 if(wb.main_apply_source) {g_source_remove(wb.main_apply_source);wb.main_apply_source=0;}
 if(!capture_layout(&palette.before)) {ui_set_statusbar(TRUE,"Cannot capture layout for independent windows");return;}
 palette_views_capture();ui_statusbar_mode(TRUE);InsightWindowRect original=palette_frame(main_widgets.window);
 GdkDisplay *display=gtk_widget_get_display(main_widgets.window);GdkWindow *native=gtk_widget_get_window(main_widgets.window);GdkMonitor *monitor=native ? gdk_display_get_monitor_at_window(display,native):NULL;
 if(!monitor && gdk_display_get_n_monitors(display)) monitor=gdk_display_get_monitor(display,0);
 GdkRectangle work={0,0,1024,768};if(monitor) gdk_monitor_get_workarea(monitor,&work);palette.area=(InsightWindowRect){work.x,work.y,work.width,work.height};original=insight_window_rect_clamp(original,palette.area);
 GtkWidget *menu_row=ui_lookup_widget(main_widgets.window,"hbox_menubar"),*book=main_widgets.message_window_notebook;
 palette.origins[0]=palette_widget_rect(menu_row,original);
 if(main_widgets.toolbar && gtk_widget_get_mapped(main_widgets.toolbar)) {InsightWindowRect tool=palette_widget_rect(main_widgets.toolbar,palette.origins[0]);gint right=MAX(tool.x+tool.width,palette.origins[0].x+palette.origins[0].width),bottom=MAX(tool.y+tool.height,palette.origins[0].y+palette.origins[0].height);palette.origins[0].x=MIN(tool.x,palette.origins[0].x);palette.origins[0].y=MIN(tool.y,palette.origins[0].y);palette.origins[0].width=right-palette.origins[0].x;palette.origins[0].height=bottom-palette.origins[0].y;}
 palette.origins[1]=palette_widget_rect(wb.hosts[8].page,palette_widget_rect(main_widgets.sidebar_notebook,original));palette.origins[2]=palette_widget_rect(wb.project_host,original);palette.origins[3]=palette_widget_rect(book,original);palette.origins[4]=original;
 palette.active=TRUE;palette.message_visible=gtk_widget_get_visible(book);palette.speed=0;palette.rows=g_ptr_array_new_with_free_func(palette_row_free);
 GtkWidget *root=ui_lookup_widget(main_widgets.window,"vbox1");palette.window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(palette.window,"insight-tools-palette");gtk_window_set_title(GTK_WINDOW(palette.window),insight_tr("Geany Insight — Menu and Tools"));gtk_window_set_transient_for(GTK_WINDOW(palette.window),GTK_WINDOW(main_widgets.window));g_object_set_data(G_OBJECT(palette.window),"GladeParentKey",main_widgets.window);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_container_add(GTK_CONTAINER(palette.window),box);g_signal_connect(box,"size-allocate",G_CALLBACK(palette_tools_fit),palette.window);GList *children=gtk_container_get_children(GTK_CONTAINER(root));gint position=0;
 for(GList *p=children;p;p=p->next,position++) {
  GtkWidget *widget=p->data;if(widget!=menu_row && widget!=main_widgets.toolbar) continue;PaletteRow *row=g_new0(PaletteRow,1);row->position=position;g_weak_ref_init(&row->widget,widget);gtk_box_query_child_packing(GTK_BOX(root),widget,&row->expand,&row->fill,&row->padding,&row->pack);g_ptr_array_add(palette.rows,row);g_object_ref(widget);gtk_container_remove(GTK_CONTAINER(root),widget);gtk_box_pack_start(GTK_BOX(box),widget,FALSE,FALSE,0);g_object_unref(widget);
 }
 g_list_free(children);GSList *accels=gtk_accel_groups_from_object(G_OBJECT(main_widgets.window));for(GSList *p=accels;p;p=p->next) gtk_window_add_accel_group(GTK_WINDOW(palette.window),p->data);
 g_signal_connect(palette.window,"delete-event",G_CALLBACK(palette_closed),NULL);insight_appearance_decorate(palette.window);gtk_widget_show(box);
 dock_all_panels();dock_project(NULL,NULL);detach_panel(8);float_project(NULL,NULL);
 /* Move the complete bottom notebook, not just the Context page: Local/SSH/TTY,
  * messages and the selected tab keep their widget and process identities. */
 /* Move the clipping host too: leaving it behind creates a live empty
  * splitter. Its collapse callback would otherwise hide the detached notebook. */
 GtkWidget *split=ui_lookup_widget(main_widgets.window,"vpaned1");palette.bottom_content=gtk_paned_get_child2(GTK_PANED(split));palette.bottom_parent=g_object_ref(split);
 gtk_container_child_get(GTK_CONTAINER(split),palette.bottom_content,"resize",&palette.bottom_resize,"shrink",&palette.bottom_shrink,NULL);
 palette.bottom=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(palette.bottom,"insight-bottom-palette");gtk_window_set_title(GTK_WINDOW(palette.bottom),insight_tr("Geany Insight — Context and Terminals"));gtk_window_set_transient_for(GTK_WINDOW(palette.bottom),GTK_WINDOW(main_widgets.window));g_object_set_data(G_OBJECT(palette.bottom),"GladeParentKey",main_widgets.window);g_object_ref(palette.bottom_content);gtk_container_remove(GTK_CONTAINER(palette.bottom_parent),palette.bottom_content);gtk_container_add(GTK_CONTAINER(palette.bottom),palette.bottom_content);g_object_unref(palette.bottom_content);g_signal_connect(palette.bottom,"delete-event",G_CALLBACK(palette_closed),NULL);insight_appearance_decorate(palette.bottom);gtk_widget_show(book);
 ui_prefs.sidebar_visible=FALSE;ui_sidebar_show_hide();gtk_window_unmaximize(GTK_WINDOW(main_widgets.window));
 InsightWindowRect targets[INSIGHT_FLOAT_COUNT];insight_window_edge_layout(palette.area,NULL,targets);
 for(guint n=0;n<INSIGHT_FLOAT_COUNT;n++) {
  GtkWidget *window=palette_part_window(n);if(!window) continue;palette_compact_title(window,TRUE);if(window!=main_widgets.window) gtk_window_set_transient_for(GTK_WINDOW(window),NULL);gtk_window_set_gravity(GTK_WINDOW(window),GDK_GRAVITY_NORTH_WEST);gtk_window_set_position(GTK_WINDOW(window),GTK_WIN_POS_NONE);gtk_window_unmaximize(GTK_WINDOW(window));g_object_set_data(G_OBJECT(window),"insight-independent-opacity",GINT_TO_POINTER(1));gint wanted_h=targets[n].height;
  if(n==INSIGHT_FLOAT_TOOLS) {gint minimum,natural;gtk_widget_get_preferred_height(window,&minimum,&natural);wanted_h=MAX(1,natural);}
  gtk_window_set_default_size(GTK_WINDOW(window),targets[n].width,wanted_h);gtk_window_resize(GTK_WINDOW(window),targets[n].width,wanted_h);
  gtk_window_move(GTK_WINDOW(window),targets[n].x,targets[n].y);gtk_widget_show(window);insight_appearance_panel_opacity(window,100);
 }
 palette_queue_motion(FALSE);
}
void insight_workbench_tools_changed(void) {
 if(!palette.active || !palette.window || !main_widgets.toolbar) return;
 GtkWidget *root=ui_lookup_widget(main_widgets.window,"vbox1"),*widget=main_widgets.toolbar;
 if(gtk_widget_get_parent(widget)!=root) return;
 PaletteRow *row=g_new0(PaletteRow,1);g_weak_ref_init(&row->widget,widget);
 GList *children=gtk_container_get_children(GTK_CONTAINER(root));row->position=g_list_index(children,widget);g_list_free(children);
 gtk_box_query_child_packing(GTK_BOX(root),widget,&row->expand,&row->fill,&row->padding,&row->pack);g_ptr_array_add(palette.rows,row);
 g_object_ref(widget);gtk_container_remove(GTK_CONTAINER(root),widget);gtk_box_pack_start(GTK_BOX(gtk_bin_get_child(GTK_BIN(palette.window))),widget,FALSE,FALSE,0);g_object_unref(widget);
}
static void apply_layout(GtkMenuItem *item,gpointer data) {
 (void)item;gint layout=GPOINTER_TO_INT(data);if(layout<0 || layout>=4) return;
 if(insight_layout_valid(&wb.layouts[layout])) { apply_layout_record(&wb.layouts[layout]);return; }
 wb.initial_right_split=-1;dock_all_panels();dock_project(NULL,NULL);
 set_panel_visibility(layout!=1,layout!=1,layout!=1);
 gtk_paned_set_position(GTK_PANED(wb.pane),layout==2 ? 330 : 500); gtk_paned_set_position(GTK_PANED(wb.right_pane),layout==2 ? 650 : 850);
 gtk_paned_set_position(GTK_PANED(ui_lookup_widget(main_widgets.window,"hpaned1")),layout==3 ? 360 : 240);
 if(layout==3) float_project(NULL,NULL);
}
