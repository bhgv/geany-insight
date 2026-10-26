/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "ui_utils.h"
#include <string.h>
static GtkWidget *window,*menu,*unknown,*check,*radio,*known,*native,*reduced;
static guint phase,ticks;
static GtkWidget *named(GtkWidget *w,const gchar *name);
static GtkWidget *tip,*tip_label,*tip_icon,*tip_source;
static GtkAllocation before[3];
static guint tip_cycles,tip_wait;
static void geometry(gboolean capture) {
 GtkWidget *nodes[]={window,GTK_WIDGET(document_get_current()->editor->sci),named(window,"insight-context")};
 for(guint i=0;i<3;i++) { g_assert_nonnull(nodes[i]);GtkAllocation a;gtk_widget_get_allocation(nodes[i],&a);if(capture) before[i]=a;else { g_assert_cmpint(a.x,==,before[i].x);g_assert_cmpint(a.y,==,before[i].y);g_assert_cmpint(a.width,==,before[i].width);g_assert_cmpint(a.height,==,before[i].height); } }
}
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *c=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL;
}
static GtkWidget *find_toolbar(GtkWidget *w) {
 if(GTK_IS_TOOLBAR(w)) return w;
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=find_toolbar(p->data);g_list_free(children);return found; }return NULL;
}
static gint height(GtkWidget *w) { gint minimum,natural;gtk_widget_get_preferred_height(w,&minimum,&natural);return natural; }
static GtkWidget *native_tip(void) {
 GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(!strcmp(G_OBJECT_TYPE_NAME(p->data),"GtkTooltipWindow") && gtk_widget_get_mapped(p->data)) found=p->data;g_list_free(list);return found;
}
static void pointer_at(GtkWidget *widget) {
 GtkWidget *top=gtk_widget_get_toplevel(widget);g_assert_true(gtk_widget_get_mapped(widget));
 gint x,y,rx,ry;g_assert_true(gtk_widget_translate_coordinates(widget,top,gtk_widget_get_allocated_width(widget)/2,gtk_widget_get_allocated_height(widget)/2,&x,&y));gdk_window_get_origin(gtk_widget_get_window(top),&rx,&ry);
 GdkDisplay *display=gtk_widget_get_display(window);GdkDevice *device=gdk_seat_get_pointer(gdk_display_get_default_seat(display));g_assert_nonnull(device);gdk_device_warp(device,gtk_widget_get_screen(window),rx+x,ry+y);gdk_display_sync(display);gtk_tooltip_trigger_tooltip_query(display);
}
static void native_tooltip_style(GtkWidget *widget) {
 g_assert_false(gtk_style_context_has_class(gtk_widget_get_style_context(widget),"insight-macos"));g_assert_false(gtk_style_context_has_class(gtk_widget_get_style_context(widget),"insight-solid"));g_assert_null(g_object_get_data(G_OBJECT(widget),"insight-i18n-watched"));
}
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
  reduced=named(window,"insight-opacity-0-100");GtkWidget *translucent=named(window,"insight-opacity-0-90");g_assert_true(GTK_IS_RADIO_MENU_ITEM(reduced));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(translucent),TRUE);g_assert_nonnull(reduced);g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(reduced)));
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
  gint min,nat;GtkWidget *detach=named(menu,"insight-menu-detach");g_assert_nonnull(detach);gtk_widget_get_preferred_width(gtk_bin_get_child(GTK_BIN(detach)),&min,&nat);label_width=MAX(label_width,nat);gtk_widget_get_preferred_width(gtk_bin_get_child(GTK_BIN(native)),&min,&nat);label_width=MAX(label_width,nat);gtk_widget_get_preferred_width(menu,&min,&nat);g_assert_cmpint(nat,<=,label_width+60);
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
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-opacity-0-90")),TRUE);popup(menu);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==4) {
  g_assert_cmpfloat(background(menu).alpha,<,1.0);gtk_menu_popdown(GTK_MENU(menu));gtk_widget_destroy(menu);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);
  GtkWidget *bar=find_toolbar(window);g_assert_nonnull(bar);GtkToolItem *item=gtk_toolbar_get_nth_item(GTK_TOOLBAR(bar),4);g_assert_nonnull(item);tip_source=gtk_bin_get_child(GTK_BIN(item));g_assert_nonnull(tip_source);g_assert_true(gtk_widget_get_mapped(tip_source));gtk_widget_set_tooltip_text(tip_source,"Project files");gchar *translated=gtk_widget_get_tooltip_text(tip_source);g_assert_cmpstr(translated,==,"Файлы проекта");g_free(translated);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==5) {
  geometry(TRUE);tip=gtk_window_new(GTK_WINDOW_POPUP);gtk_window_set_type_hint(GTK_WINDOW(tip),GDK_WINDOW_TYPE_HINT_TOOLTIP);GtkWidget *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);tip_label=gtk_label_new("Project files");tip_icon=gtk_image_new_from_icon_name("edit-copy",GTK_ICON_SIZE_DIALOG);gtk_container_add(GTK_CONTAINER(tip),box);gtk_box_pack_start(GTK_BOX(box),tip_label,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),tip_icon,FALSE,FALSE,0);gtk_widget_show_all(tip);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==6) {
  native_tooltip_style(tip);native_tooltip_style(tip_label);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(tip_label)),==,"Project files");const gchar *name=NULL;GtkIconSize size;gtk_image_get_icon_name(GTK_IMAGE(tip_icon),&name,&size);g_assert_cmpstr(name,==,"edit-copy");g_assert_cmpint(size,==,GTK_ICON_SIZE_DIALOG);geometry(FALSE);
  if(++tip_cycles<4) { gtk_widget_hide(tip);gtk_widget_show_all(tip);return G_SOURCE_CONTINUE; }
  gtk_widget_destroy(tip);tip=NULL;pointer_at(tip_source);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==7) {
  GtkWidget *shown=native_tip();if(!shown) { if(++tip_wait>20) g_error("Native GTK tooltip did not appear");return G_SOURCE_CONTINUE; }
  native_tooltip_style(shown);geometry(FALSE);pointer_at(GTK_WIDGET(doc->editor->sci));phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==8) {
  geometry(FALSE);g_print("macOS menus and tooltips: native mixed-row geometry/materials, translated source tooltip, repeated popup maps without font/icon/translation mutation, real GTK tooltip and stable editor/Context allocations passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("menu-materials-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL),*source=g_build_filename(dir,"source.c",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(source,"int main(void) { return 0; }\n",-1,NULL));gchar *args[]={"menus","--new-instance","--no-session","--config",dir,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(250,step,NULL);return main_lib(6,args);
}
