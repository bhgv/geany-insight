/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
static GtkWidget *window,*page,*floating;static guint phase;static gboolean restore,restore_started;static gchar *layout_path;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static void click(const gchar *name) { GtkWidget *button=named(page,name);g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button)); }
static void command(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static gboolean accept_file(gpointer data) { GtkWidget *dialog=data;gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);g_object_unref(dialog);return G_SOURCE_REMOVE; }
static gboolean choose_layout_file(gpointer data) {
 gint action=GPOINTER_TO_INT(data);gboolean save=action>0;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-layout-file-dialog")) { dialog=p->data;break; }
 g_list_free(windows);if(!dialog) return G_SOURCE_CONTINUE;
 if(action<0) { gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
 if(save) { gchar *folder=g_path_get_dirname(layout_path),*name=g_path_get_basename(layout_path);gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),folder);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),name);g_free(folder);g_free(name); }
 else g_assert_true(gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog),layout_path));
 g_timeout_add(350,accept_file,g_object_ref(dialog));return G_SOURCE_REMOVE;
}
static void layout_file(gboolean save) { g_timeout_add(100,choose_layout_file,GINT_TO_POINTER(save));command(save ? "insight-layout-export" : "insight-layout-import"); }
static GtkWidget *help_window(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *result=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-help-viewer")) result=p->data;g_list_free(windows);return result; }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!window) {
  window=gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));GtkWidget *book=named(window,"insight-project-notebook");g_assert_nonnull(book);
  if(restore) {
   GList *windows=gtk_window_list_toplevels();for(GList *p=windows;p && !page;p=p->next) if(p->data!=window) { GtkWidget *button=named(p->data,"insight-panel-toggle-dock");if(button) { floating=p->data;page=gtk_bin_get_child(GTK_BIN(floating)); } }g_list_free(windows);g_assert_nonnull(page);
  } else { page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),0);click("insight-panel-toggle-dock");floating=gtk_widget_get_toplevel(page);gtk_window_resize(GTK_WINDOW(floating),520,430);gtk_window_resize(GTK_WINDOW(window),900,760); }
  return G_SOURCE_CONTINUE;
 }
 if(restore) {
  if(!restore_started) {
   GtkWidget *tray_button=named(window,"insight-panel-tray-0");g_assert_nonnull(tray_button);g_assert_false(gtk_widget_get_visible(floating));gtk_button_clicked(GTK_BUTTON(tray_button));restore_started=TRUE;return G_SOURCE_CONTINUE;
  }
  if(!gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;
  click("insight-panel-toggle-maximize");restore=FALSE;phase=8;return G_SOURCE_CONTINUE;
 }
 switch(phase) {
 case 0: {
  GtkWidget *ru=named(window,"insight-interface-ru"),*en=named(window,"insight-interface-en");g_assert_nonnull(ru);g_assert_nonnull(en);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ru),TRUE);gchar *tip=gtk_widget_get_tooltip_text(named(page,"insight-panel-toggle-dock"));g_assert_cmpstr(tip,==,"Закрепить эту панель");g_free(tip);
  tip=gtk_widget_get_tooltip_text(named(page,"insight-panel-minimize"));g_assert_cmpstr(tip,==,"Свернуть панель в нижнюю полосу");g_free(tip);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(en),TRUE);
  command("insight-layout-save-0");gtk_window_resize(GTK_WINDOW(floating),710,550);gtk_window_resize(GTK_WINDOW(window),1100,850);phase++;break;
 }
 case 1: command("insight-layout-load-0");floating=gtk_widget_get_toplevel(page);phase++;break;
 case 2: {
  gint width,height;gtk_window_get_size(GTK_WINDOW(floating),&width,&height);g_assert_cmpint(width,==,520);g_assert_cmpint(height,==,430);
  gtk_window_get_size(GTK_WINDOW(window),&width,&height);g_assert_cmpint(width,==,900);g_assert_cmpint(height,==,760);
  gtk_window_maximize(GTK_WINDOW(window));phase=30;break;
 }
 case 30: if(!gtk_window_is_maximized(GTK_WINDOW(window))) return G_SOURCE_CONTINUE;command("insight-layout-save-3");gtk_window_unmaximize(GTK_WINDOW(window));phase=31;break;
 case 31: if(gtk_window_is_maximized(GTK_WINDOW(window))) return G_SOURCE_CONTINUE;gtk_window_resize(GTK_WINDOW(window),1050,820);phase=32;break;
 case 32: command("insight-layout-load-3");floating=gtk_widget_get_toplevel(page);phase=33;break;
 case 33: if(!gtk_window_is_maximized(GTK_WINDOW(window))) return G_SOURCE_CONTINUE;gtk_window_unmaximize(GTK_WINDOW(window));phase=34;break;
 case 34: {
  if(gtk_window_is_maximized(GTK_WINDOW(window))) return G_SOURCE_CONTINUE;
  gint width,height;gtk_window_get_size(GTK_WINDOW(window),&width,&height);g_assert_cmpint(width,==,900);g_assert_cmpint(height,==,760);click("insight-panel-minimize");phase=20;break;
 }
 case 20: {
  GtkWidget *tray=named(window,"insight-panel-tab-tray"),*tab=named(window,"insight-panel-tray-0");g_assert_nonnull(tray);g_assert_nonnull(tab);g_assert_true(gtk_widget_get_visible(tray));g_assert_false(gtk_widget_get_visible(floating));
  command("insight-panel-tab-tray-toggle");g_assert_false(gtk_widget_get_visible(tray));command("insight-panel-restore-all");g_assert_true(gtk_widget_get_visible(floating));command("insight-panel-tab-tray-toggle");
  click("insight-panel-minimize");command("insight-layout-save-2");layout_file(TRUE);g_assert_true(g_file_test(layout_path,G_FILE_TEST_IS_REGULAR));click("insight-panel-toggle-dock");layout_file(FALSE);floating=gtk_widget_get_toplevel(page);phase=21;break;
 }
 case 21: {
  GtkWidget *tray=named(window,"insight-panel-tab-tray"),*tab=named(window,"insight-panel-tray-0");g_assert_true(gtk_widget_get_visible(tray));g_assert_false(gtk_widget_get_visible(floating));g_assert_true(g_file_set_contents(layout_path,"[layout]\nversion=5\n",-1,NULL));layout_file(FALSE);g_assert_true(gtk_widget_get_toplevel(page)==floating);g_assert_false(gtk_widget_get_visible(floating));g_timeout_add(100,choose_layout_file,GINT_TO_POINTER(-1));command("insight-layout-import");g_assert_true(gtk_widget_get_toplevel(page)==floating);gtk_button_clicked(GTK_BUTTON(tab));g_assert_true(gtk_widget_get_visible(floating));g_assert_false(gtk_widget_get_visible(tray));gtk_window_iconify(GTK_WINDOW(floating));phase=22;break;
 }
 case 22: {
  if(gtk_widget_get_visible(floating)) return G_SOURCE_CONTINUE;
  GtkWidget *tray=named(window,"insight-panel-tab-tray"),*tab=named(window,"insight-panel-tray-0");g_assert_true(gtk_widget_get_visible(tray));gtk_button_clicked(GTK_BUTTON(tab));click("insight-panel-toggle-maximize");phase=3;break;
 }
 case 3: if(!gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;command("insight-layout-save-1");click("insight-panel-toggle-maximize");phase++;break;
 case 4: if(gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;command("insight-layout-load-1");floating=gtk_widget_get_toplevel(page);phase++;break;
 case 5: if(!gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;click("insight-panel-toggle-dock");phase++;break;
 case 6: click("insight-panel-toggle-dock");floating=gtk_widget_get_toplevel(page);phase++;break;
 case 7: if(!gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;click("insight-panel-minimize");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 case 8: {
  if(gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;
  gint width,height;gtk_window_get_size(GTK_WINDOW(floating),&width,&height);g_assert_cmpint(width,==,520);g_assert_cmpint(height,==,430);
  command("insight-layout-help");phase=9;return G_SOURCE_CONTINUE;
 }
 case 9: {
  GtkWidget *help=help_window();g_assert_nonnull(help);gtk_widget_destroy(help);
  const gchar *capture=g_getenv("GEANY_PANEL_CAPTURE");if(capture) { GdkWindow *root=gtk_widget_get_window(floating);GdkPixbuf *image=gdk_pixbuf_get_from_window(root,0,0,gdk_window_get_width(root),MIN(160,gdk_window_get_height(root)));g_assert_nonnull(image);GError *error=NULL;g_assert_true(gdk_pixbuf_save(image,capture,"jpeg",&error,"quality","88",NULL));g_assert_no_error(error);g_object_unref(image); }
  gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 default:g_assert_not_reached();
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Panel state GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 if(!gtk_init_check(NULL,NULL)) return 77;
#ifdef GDK_WINDOWING_X11
 if(GDK_IS_X11_DISPLAY(gdk_display_get_default()) && !gdk_x11_screen_supports_net_wm_hint(gdk_screen_get_default(),gdk_atom_intern_static_string("_NET_WM_STATE_MAXIMIZED_VERT"))) { g_print("Panel state GUI requires a window manager supporting maximized windows\n");return 77; }
#endif
 if(argc==1) {
  gchar *directory=g_dir_make_tmp("panel-state-XXXXXX",NULL);g_assert_nonnull(directory);gchar *config=g_build_filename(directory,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
  for(guint i=0;i<2;i++) { gchar *child[]={argv[0],directory,i ? "restore" : "fresh",NULL};gint status;GError *error=NULL;g_assert_true(g_spawn_sync(NULL,child,NULL,0,NULL,NULL,NULL,NULL,&status,&error));g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error); }
  g_free(config);g_free(directory);g_print("Panel state GUI: main-window bounds/maximize/normal restore, layout geometry, maximize/restore, Tab Tray, native minimize, external layout files, Cancel, invalid import, reparenting and restart passed\n");return 0;
 }
 layout_path=g_build_filename(argv[1],"review.geany-layout",NULL);restore=!g_strcmp0(argv[2],"restore");gchar *args[]={"panel-state-gui","--new-instance","--no-session","--config",argv[1],NULL};g_timeout_add(400,step,NULL);g_timeout_add_seconds(40,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);return main_lib(5,args);
}
