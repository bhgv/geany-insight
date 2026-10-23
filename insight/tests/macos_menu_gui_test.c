/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "ui_utils.h"
#include <string.h>
static GtkWidget *window,*menu,*unknown,*check,*radio,*known,*native,*reduced;
static guint phase,ticks;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *c=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL;
}
static gint height(GtkWidget *w) { gint minimum,natural;gtk_widget_get_preferred_height(w,&minimum,&natural);return natural; }
static GdkRGBA background(GtkWidget *w) { GdkRGBA color;gtk_style_context_get_background_color(gtk_widget_get_style_context(w),GTK_STATE_FLAG_NORMAL,&color);return color; }
static void popup(GtkWidget *m) {
 GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);event->button.window=g_object_ref(gtk_widget_get_window(window));event->button.button=3;event->button.time=GDK_CURRENT_TIME;
 GdkSeat *seat=gdk_display_get_default_seat(gtk_widget_get_display(window));if(seat) { GdkDevice *device=gdk_seat_get_pointer(seat);if(device) gdk_event_set_device(event,device); }
 gtk_menu_popup_at_widget(GTK_MENU(m),ui_lookup_widget(window,"search1"),GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,event);gdk_event_free(event);
}
static void capture(const gchar *suffix) {
 const gchar *dir=g_getenv("INSIGHT_MENU_CAPTURE_DIR");if(!dir) return;gchar *path=g_build_filename(dir,suffix,NULL);
 GdkWindow *w=gtk_widget_get_window(menu);GdkPixbuf *pix=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(pix);g_assert_true(gdk_pixbuf_save(pix,path,"jpeg",NULL,"quality","90",NULL));g_object_unref(pix);g_free(path);
}
static gboolean step(gpointer data) {
 (void)data;if(++ticks>100) g_error("Menu materials timeout phase %u",phase);
 GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(phase==0) {
  if(ticks<8) return G_SOURCE_CONTINUE;g_assert_true(gtk_style_context_has_class(gtk_widget_get_style_context(window),"insight-macos"));
  reduced=named(window,"insight-appearance-0-2");g_assert_nonnull(reduced);g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(reduced)));
  menu=gtk_menu_new();gtk_menu_attach_to_widget(GTK_MENU(menu),window,NULL);
  unknown=gtk_menu_item_new_with_label("Unmapped action");known=gtk_menu_item_new_with_label("Next result");native=gtk_image_menu_item_new_with_label("Find in Files…");gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(native),gtk_image_new_from_icon_name("edit-find",GTK_ICON_SIZE_MENU));
  check=gtk_check_menu_item_new_with_label("Show panel");radio=gtk_radio_menu_item_new_with_label(NULL,"Selected mode");
  GtkWidget *all[]={unknown,known,native,check,radio};for(guint i=0;i<G_N_ELEMENTS(all);i++) gtk_menu_shell_append(GTK_MENU_SHELL(menu),all[i]);gtk_widget_show_all(menu);popup(menu);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==1) {
  g_assert_null(g_object_get_data(G_OBJECT(unknown),"insight-menu-icon"));g_assert_nonnull(g_object_get_data(G_OBJECT(known),"insight-menu-icon"));
  g_assert_null(g_object_get_data(G_OBJECT(check),"insight-menu-icon"));g_assert_null(g_object_get_data(G_OBJECT(radio),"insight-menu-icon"));
  gint base=height(native);g_assert_cmpint(base,<=,24);GtkWidget *all[]={unknown,known,check,radio};for(guint i=0;i<G_N_ELEMENTS(all);i++) { g_assert_cmpint(abs(height(all[i])-base),<=,2);g_assert_cmpint(abs(gtk_widget_get_allocated_height(all[i])-gtk_widget_get_allocated_height(native)),<=,2); }
  gint label_width=0;for(guint i=0;i<G_N_ELEMENTS(all);i++) { GtkWidget *label=gtk_bin_get_child(GTK_BIN(all[i]));gint min,nat;gtk_widget_get_preferred_width(label,&min,&nat);label_width=MAX(label_width,nat); }
  gint min,nat;gtk_widget_get_preferred_width(gtk_bin_get_child(GTK_BIN(native)),&min,&nat);label_width=MAX(label_width,nat);gtk_widget_get_preferred_width(menu,&min,&nat);g_assert_cmpint(nat,<=,label_width+60);
  g_assert_cmpfloat(background(menu).alpha,<,1.0);capture("macos-menu-material.jpg");gtk_menu_popdown(GTK_MENU(menu));
  GtkWidget *search=ui_lookup_widget(window,"search1_menu");if(!search) search=gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(window,"search1")));popup(search);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {
  GtkWidget *next=ui_lookup_widget(window,"find_next1"),*files=ui_lookup_widget(window,"find_in_files1"),*replace=ui_lookup_widget(window,"replace1");g_assert_cmpint(abs(height(next)-height(files)),<=,2);g_assert_cmpint(abs(height(next)-height(replace)),<=,2);g_assert_cmpint(height(next),<=,24);
  gtk_menu_popdown(GTK_MENU(gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(window,"search1")))));
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(reduced),TRUE);popup(menu);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==3) {
  g_assert_cmpfloat(background(menu).alpha,==,1.0);g_assert_true(gtk_style_context_has_class(gtk_widget_get_style_context(menu),"insight-solid"));capture("macos-menu-solid.jpg");gtk_menu_popdown(GTK_MENU(menu));
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(reduced),FALSE);popup(menu);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==4) {
  g_assert_cmpfloat(background(menu).alpha,<,1.0);gtk_menu_popdown(GTK_MENU(menu));gtk_widget_destroy(menu);
  g_print("macOS menus: fresh default, no fallback/check/radio duplicate icons, native mixed-row compact geometry, constrained width, live translucent/opaque materials passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("menu-materials-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL),*source=g_build_filename(dir,"source.c",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(source,"int main(void) { return 0; }\n",-1,NULL));gchar *args[]={"menus","--new-instance","--no-session","--config",dir,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(250,step,NULL);return main_lib(6,args);
}
