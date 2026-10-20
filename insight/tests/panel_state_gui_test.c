/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
static GtkWidget *window,*page,*floating;static guint phase;static gboolean restore;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static void click(const gchar *name) { GtkWidget *button=named(page,name);g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button)); }
static void command(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!window) {
  window=main_widgets.window;GtkWidget *book=named(window,"insight-project-notebook");g_assert_nonnull(book);
  if(restore) {
   GList *windows=gtk_window_list_toplevels();for(GList *p=windows;p && !page;p=p->next) if(p->data!=window) { GtkWidget *button=named(p->data,"insight-panel-toggle-dock");if(button) { floating=p->data;page=gtk_bin_get_child(GTK_BIN(floating)); } }g_list_free(windows);g_assert_nonnull(page);
  } else { page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),0);click("insight-panel-toggle-dock");floating=gtk_widget_get_toplevel(page);gtk_window_resize(GTK_WINDOW(floating),520,430); }
  return G_SOURCE_CONTINUE;
 }
 if(restore) {
  if(!gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;
  click("insight-panel-toggle-maximize");restore=FALSE;phase=8;return G_SOURCE_CONTINUE;
 }
 switch(phase) {
 case 0: command("insight-layout-save-0");gtk_window_resize(GTK_WINDOW(floating),710,550);phase++;break;
 case 1: command("insight-layout-load-0");floating=gtk_widget_get_toplevel(page);phase++;break;
 case 2: {
  gint width,height;gtk_window_get_size(GTK_WINDOW(floating),&width,&height);g_assert_cmpint(width,==,520);g_assert_cmpint(height,==,430);
  click("insight-panel-toggle-maximize");phase++;break;
 }
 case 3: if(!gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;command("insight-layout-save-1");click("insight-panel-toggle-maximize");phase++;break;
 case 4: if(gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;command("insight-layout-load-1");floating=gtk_widget_get_toplevel(page);phase++;break;
 case 5: if(!gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;click("insight-panel-toggle-dock");phase++;break;
 case 6: click("insight-panel-toggle-dock");floating=gtk_widget_get_toplevel(page);phase++;break;
 case 7: if(!gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 case 8: {
  if(gtk_window_is_maximized(GTK_WINDOW(floating))) return G_SOURCE_CONTINUE;
  gint width,height;gtk_window_get_size(GTK_WINDOW(floating),&width,&height);g_assert_cmpint(width,==,520);g_assert_cmpint(height,==,430);
  const gchar *capture=g_getenv("GEANY_PANEL_CAPTURE");if(capture) { GdkWindow *root=gdk_get_default_root_window();GdkPixbuf *image=gdk_pixbuf_get_from_window(root,0,0,gdk_window_get_width(root),gdk_window_get_height(root));g_assert_nonnull(image);GError *error=NULL;g_assert_true(gdk_pixbuf_save(image,capture,"jpeg",&error,"quality","88",NULL));g_assert_no_error(error);g_object_unref(image); }
  gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 default:g_assert_not_reached();
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Panel state GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 if(argc==1) {
  gchar *directory=g_dir_make_tmp("panel-state-XXXXXX",NULL);g_assert_nonnull(directory);gchar *config=g_build_filename(directory,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
  for(guint i=0;i<2;i++) { gchar *child[]={argv[0],directory,i ? "restore" : "fresh",NULL};gint status;GError *error=NULL;g_assert_true(g_spawn_sync(NULL,child,NULL,0,NULL,NULL,NULL,NULL,&status,&error));g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error); }
  g_free(config);g_free(directory);g_print("Panel state GUI: layout geometry, maximize/restore, reparenting and restart passed\n");return 0;
 }
 restore=!g_strcmp0(argv[2],"restore");gchar *args[]={"panel-state-gui","--new-instance","--no-session","--config",argv[1],NULL};g_timeout_add(400,step,NULL);g_timeout_add_seconds(25,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);return main_lib(5,args);
}
