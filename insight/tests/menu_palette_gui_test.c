/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#if defined(__linux__) && !defined(__ANDROID__)
#include <execinfo.h>
#endif
static guint phase,hits;static GtkWidget *menu,*toggle,*palette,*icon,*handle;static gint width,row_height,menu_height;static gboolean cleaned;
static void critical_trace(const gchar *domain,GLogLevelFlags level,const gchar *message,gpointer data) {
 g_printerr("Menu palette critical at phase %u\n",phase);
#if defined(__linux__) && !defined(__ANDROID__)
 void *frames[64];gint count=backtrace(frames,64);backtrace_symbols_fd(frames,count,2);
#endif
 g_log_default_handler(domain,level,message,data);
}
static void clicked(GtkMenuItem *item,gpointer data) { (void)item;(void)data;hits++; }
static GtkWidget *find(GtkWidget *root,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) {GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *out=NULL;for(GList *p=children;p && !out;p=p->next) out=find(p->data,name);g_list_free(children);return out;}return NULL;
}
static void capture_menu(const gchar *name) {
 const gchar *dir=g_getenv("INSIGHT_MENU_CAPTURE_DIR");GdkWindow *native=gtk_widget_get_window(menu);gint minimum,natural;gtk_widget_get_preferred_height(menu,&minimum,&natural);
 g_printerr("Palette geometry %s: widget=%dx%d gdk=%dx%d preferred-height=%d/%d\n",name,gtk_widget_get_allocated_width(menu),gtk_widget_get_allocated_height(menu),gdk_window_get_width(native),gdk_window_get_height(native),minimum,natural);
 if(dir) {gchar *path=g_build_filename(dir,name,NULL);GdkPixbuf *image=gdk_pixbuf_get_from_window(native,0,0,gdk_window_get_width(native),gdk_window_get_height(native));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,path,"jpeg",NULL,"quality","95",NULL));g_object_unref(image);g_free(path);}
}
static void destroyed(GtkWidget *widget,gpointer data) { (void)widget;*(gboolean *)data=TRUE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;GtkWidget *main=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));if(!g_object_get_data(G_OBJECT(main),"insight-editor-menu")) return G_SOURCE_CONTINUE;
 if(!phase) {
  menu=gtk_menu_new();g_object_ref_sink(menu);toggle=gtk_check_menu_item_new_with_label("Fixture toggle");gtk_menu_shell_append(GTK_MENU_SHELL(menu),toggle);g_signal_connect(toggle,"activate",G_CALLBACK(clicked),NULL);
  GtkWidget *label=gtk_bin_get_child(GTK_BIN(toggle)),*box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);g_object_ref(label);gtk_container_remove(GTK_CONTAINER(toggle),label);icon=gtk_image_new_from_icon_name("text-x-generic",GTK_ICON_SIZE_MENU);gtk_box_pack_start(GTK_BOX(box),icon,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),label,TRUE,TRUE,0);g_object_unref(label);gtk_container_add(GTK_CONTAINER(toggle),box);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_menu_item_new_with_label("Another command"));g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);g_signal_connect(menu,"destroy",G_CALLBACK(destroyed),&cleaned);gtk_widget_show_all(menu);
  GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);event->button.window=g_object_ref(gtk_widget_get_window(main));event->button.button=3;GdkSeat *seat=gdk_display_get_default_seat(gtk_widget_get_display(main));gdk_event_set_device(event,gdk_seat_get_pointer(seat));gtk_menu_popup_at_widget(GTK_MENU(menu),main,GDK_GRAVITY_CENTER,GDK_GRAVITY_CENTER,event);gdk_event_free(event);
 } else if(phase==1) {
  handle=find(menu,"insight-menu-detach");g_assert_nonnull(handle);g_assert_true(GTK_IS_DRAWING_AREA(gtk_bin_get_child(GTK_BIN(handle))));g_assert_null(gtk_menu_item_get_label(GTK_MENU_ITEM(handle)));g_assert_cmpint(gtk_widget_get_allocated_height(handle),==,7);
  capture_menu("menu-attached.jpg");width=gtk_widget_get_allocated_width(menu);gtk_widget_get_preferred_height(menu,NULL,&menu_height);row_height=gtk_widget_get_allocated_height(toggle);gtk_menu_shell_select_item(GTK_MENU_SHELL(menu),handle);
  GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.keyval=GDK_KEY_Return;gboolean handled=FALSE;g_signal_emit_by_name(menu,"key-press-event",event,&handled);gdk_event_free(event);g_assert_true(handled);g_assert_true(gtk_menu_get_tearoff_state(GTK_MENU(menu)));g_assert_false(cleaned);palette=gtk_widget_get_toplevel(menu);g_assert_true(GTK_IS_WINDOW(palette));g_assert_false(gtk_widget_get_app_paintable(palette));
 } else if(phase==2) {
  capture_menu("menu-detached.jpg");if(gdk_display_supports_shapes(gtk_widget_get_display(palette))) g_assert_true(gdk_window_is_shaped(gtk_widget_get_window(palette)));g_assert_true(gtk_widget_get_mapped(menu));g_assert_true(gtk_widget_get_visible(icon));g_assert_true(gtk_widget_is_ancestor(toggle,menu));g_assert_cmpint(gtk_widget_get_allocated_height(toggle),==,row_height);g_assert_cmpint(ABS(gtk_widget_get_allocated_width(menu)-width),<=,2);g_assert_cmpint(ABS(gtk_widget_get_allocated_height(menu)-menu_height),<=,2);
  gtk_menu_shell_activate_item(GTK_MENU_SHELL(menu),toggle,TRUE);g_assert_cmpuint(hits,==,1);g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(toggle)));g_assert_false(cleaned);g_assert_true(gtk_menu_get_tearoff_state(GTK_MENU(menu)));gtk_menu_shell_activate_item(GTK_MENU_SHELL(menu),toggle,TRUE);g_assert_cmpuint(hits,==,2);g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(toggle)));g_assert_false(cleaned);
  g_printerr("Menu palette: native row geometry and repeated activation passed; reattaching\n");
  gtk_menu_set_tearoff_state(GTK_MENU(menu),FALSE);g_assert_false(cleaned);
 } else if(phase==3) {
  g_assert_true(cleaned);g_object_unref(menu);
  g_print("Menu palette GUI: 7px strip, original widgets/icons, preserved row geometry, repeated native activation and deferred popup cleanup passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(main,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 phase++;return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Menu palette timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("menu-palette-%u",g_random_int()),*profile=g_build_filename(cwd,name,NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *argv[]={"menu-palette-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(300,step,NULL);g_timeout_add_seconds(20,timeout,NULL);g_log_set_handler("Gtk",G_LOG_LEVEL_CRITICAL|G_LOG_FLAG_FATAL|G_LOG_FLAG_RECURSION,critical_trace,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(cwd);g_free(name);g_free(profile);g_free(config);return result;
}
