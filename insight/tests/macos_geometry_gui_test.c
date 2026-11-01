/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*tools,*root_menu,*submenu,*parent_item;
static guint phase,ticks;static gboolean styled_before_map;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name))return w;
 if(GTK_IS_MENU_ITEM(w)) {GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(sub){GtkWidget *found=named(sub,name);if(found)return found;}}
 if(GTK_IS_CONTAINER(w)){GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p&&!found;p=p->next)found=named(p->data,name);g_list_free(children);return found;}return NULL;
}
static GtkWidget *any(const gchar *name) {GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p&&!found;p=p->next)found=named(p->data,name);g_list_free(windows);return found;}
static GtkWidget *maximize_button(GtkWidget *);
static void scan_button(GtkWidget *widget,gpointer data) {
 GtkWidget **found=data;if(!*found) *found=maximize_button(widget);
}
static GtkWidget *maximize_button(GtkWidget *w) {
 if(GTK_IS_BUTTON(w)&&gtk_style_context_has_class(gtk_widget_get_style_context(w),"maximize"))return w;
 GtkWidget *found=NULL;if(GTK_IS_CONTAINER(w)) gtk_container_forall(GTK_CONTAINER(w),scan_button,&found);return found;
}
static gboolean maximized(GtkWidget *w) {return gtk_window_is_maximized(GTK_WINDOW(w));}
static void fits(GtkWidget *w) {
 GdkMonitor *monitor=gdk_display_get_monitor_at_window(gdk_display_get_default(),gtk_widget_get_window(w));GdkRectangle area,frame;
 gdk_monitor_get_workarea(monitor,&area);gdk_window_get_frame_extents(gtk_widget_get_window(w),&frame);
 g_assert_cmpint(frame.x,>=,area.x-4);g_assert_cmpint(frame.y,>=,area.y-4);
 g_assert_cmpint(frame.x+frame.width,<=,area.x+area.width+4);g_assert_cmpint(frame.y+frame.height,<=,area.y+area.height+4);
}
static void shown(GtkWidget *menu,gpointer data) {
 (void)data;g_assert_false(gtk_widget_get_mapped(menu));
 styled_before_map=gtk_style_context_has_class(gtk_widget_get_style_context(menu),"insight-macos");
}
static void popup(GtkWidget *menu,GtkWidget *anchor,GdkGravity a,GdkGravity b) {
 GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);event->button.window=g_object_ref(gtk_widget_get_window(window));event->button.button=3;event->button.time=GDK_CURRENT_TIME;
 gdk_event_set_device(event,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));
 gtk_menu_popup_at_widget(GTK_MENU(menu),anchor,a,b,event);gdk_event_free(event);
}
static void capture(void) {
 const gchar *dir=g_getenv("INSIGHT_MACOS_GEOMETRY_CAPTURE_DIR");if(!dir)return;
 g_mkdir_with_parents(dir,0700);gchar *name=g_strdup_printf("phase-%u.jpg",phase),*path=g_build_filename(dir,name,NULL);g_free(name);
 GdkWindow *root=gdk_get_default_root_window();GdkPixbuf *pix=gdk_pixbuf_get_from_window(root,0,0,gdk_window_get_width(root),gdk_window_get_height(root));
 if(pix) {gdk_pixbuf_save(pix,path,"jpeg",NULL,"quality","85",NULL);g_object_unref(pix);}g_free(path);
}
static gboolean step(gpointer data) {
 (void)data;if(!window){GeanyDocument *doc=document_get_current();if(!doc)return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));}
 if(++ticks<4)return G_SOURCE_CONTINUE;ticks=0;capture();
 if(phase==0){g_object_set(gtk_settings_get_default(),"gtk-decoration-layout","minimize,maximize,close:",NULL);fits(window);GtkRequisition minimum;gtk_widget_get_preferred_size(window,&minimum,NULL);GdkMonitor *monitor=gdk_display_get_monitor_at_window(gdk_display_get_default(),gtk_widget_get_window(window));GdkRectangle area;gdk_monitor_get_workarea(monitor,&area);g_assert_cmpint(minimum.height,<=,area.height);gtk_window_maximize(GTK_WINDOW(window));phase++;return G_SOURCE_CONTINUE;}
 if(phase==1){if(!maximized(window))return G_SOURCE_CONTINUE;fits(window);gtk_window_unmaximize(GTK_WINDOW(window));phase++;return G_SOURCE_CONTINUE;}
 if(phase==2){if(maximized(window))return G_SOURCE_CONTINUE;GtkWidget *choice=any("insight-appearance-0-3");g_assert_true(GTK_IS_CHECK_MENU_ITEM(choice));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(choice),TRUE);phase++;return G_SOURCE_CONTINUE;}
 if(phase==3){if(g_object_get_data(G_OBJECT(window),"insight-independent-motion"))return G_SOURCE_CONTINUE;tools=any("insight-tools-palette");g_assert_nonnull(tools);GtkWidget *button=maximize_button(gtk_window_get_titlebar(GTK_WINDOW(tools)));g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button));phase++;return G_SOURCE_CONTINUE;}
 if(phase==4){if(!maximized(tools))return G_SOURCE_CONTINUE;fits(tools);GtkWidget *header=gtk_window_get_titlebar(GTK_WINDOW(tools));GdkEventButton event={0};event.type=GDK_2BUTTON_PRESS;event.button=1;event.window=gtk_widget_get_window(header);gboolean handled=FALSE;g_signal_emit_by_name(header,"button-press-event",&event,&handled);g_assert_true(handled);phase++;return G_SOURCE_CONTINUE;}
 if(phase==5){if(maximized(tools))return G_SOURCE_CONTINUE;root_menu=gtk_menu_new();parent_item=gtk_menu_item_new_with_label("Parent");submenu=gtk_menu_new();gtk_menu_shell_append(GTK_MENU_SHELL(submenu),gtk_menu_item_new_with_label("Child"));gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent_item),submenu);gtk_menu_shell_append(GTK_MENU_SHELL(root_menu),parent_item);g_signal_connect_after(submenu,"show",G_CALLBACK(shown),NULL);gtk_widget_show_all(root_menu);gtk_widget_show_all(submenu);g_assert_true(styled_before_map);GtkWidget *anchor=ui_lookup_widget(window,"notebook1");popup(root_menu,anchor,GDK_GRAVITY_NORTH_WEST,GDK_GRAVITY_NORTH_WEST);phase++;return G_SOURCE_CONTINUE;}
 if(phase==6){popup(submenu,parent_item,GDK_GRAVITY_NORTH_EAST,GDK_GRAVITY_NORTH_WEST);phase++;return G_SOURCE_CONTINUE;}
 if(!gtk_widget_get_mapped(submenu))return G_SOURCE_CONTINUE;
 gint px,py,sx,sy,x,y;g_assert_true(gtk_widget_translate_coordinates(parent_item,root_menu,0,0,&x,&y));gdk_window_get_origin(gtk_widget_get_window(root_menu),&px,&py);gdk_window_get_origin(gtk_widget_get_window(submenu),&sx,&sy);
 g_assert_cmpint(ABS(sy-(py+y)),<=,32);g_assert_cmpint(MIN(ABS(sx-(px+x+gtk_widget_get_allocated_width(parent_item))),ABS(sx+gtk_widget_get_allocated_width(submenu)-(px+x))),<=,32);
 gtk_menu_popdown(GTK_MENU(submenu));gtk_menu_popdown(GTK_MENU(root_menu));gtk_widget_destroy(root_menu);
 g_print("macOS geometry: oversized saved window fits workarea, maximize/restore button and title double-click, submenu styling before map and parent alignment passed\n");
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data){(void)data;g_error("macOS geometry timeout phase %u",phase);return G_SOURCE_REMOVE;}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS")||!g_getenv("GEANY_TEST_WINDOW_MANAGER")||!g_getenv("DISPLAY"))return 77;
 gchar *dir=g_dir_make_tmp("macos-geometry-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL),*settings=g_build_filename(profile,"insight",NULL);g_mkdir_with_parents(settings,0700);
 gchar *config=g_build_filename(profile,"geany.conf",NULL);g_file_set_contents(config,"[geany]\nconfirm_exit=false\ngeometry=0;0;1800;1800;0;\n",-1,NULL);g_free(config);
 config=g_build_filename(settings,"interface-style.conf",NULL);g_file_set_contents(config,"[interface]\nversion=1\nstyle=macos\nopacity=100\nmacos-theme=original\n",-1,NULL);g_free(config);g_free(settings);
 gchar *args[]={"macos-geometry","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(100,step,NULL);g_timeout_add_seconds(45,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);return main_lib(5,args);
}
