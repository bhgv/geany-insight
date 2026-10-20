/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*pages[8],*floating;static guint phase;static gboolean restore;static gchar *profile;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }
 if(GTK_IS_MENU_BUTTON(w)) { GtkWidget *menu=GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(w)));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *identified(GtkWidget *w,guint id) { if(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w),"insight-panel-id"))==id+1) return w;if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=identified(p->data,id);g_list_free(children);return found; }return NULL; }
static GtkWidget *find_page(guint id) { GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p && !found;p=p->next) found=identified(p->data,id);g_list_free(windows);return found; }
static void click(guint id,const gchar *name) { GtkWidget *button=named(pages[id],name);g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button)); }
static void command(GtkWidget *root,const gchar *name) { GtkWidget *item=named(root,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void join(guint source,guint target) { gchar *name=g_strdup_printf("insight-panel-group-with-%u",target);command(pages[source],name);g_free(name); }
static GtkWidget *check_group(void) {
 floating=gtk_widget_get_toplevel(pages[0]);g_assert_true(floating!=window);g_assert_true(floating==gtk_widget_get_toplevel(pages[1]));g_assert_true(floating==gtk_widget_get_toplevel(pages[2]));GtkWidget *book=gtk_widget_get_parent(pages[0]);g_assert_true(GTK_IS_NOTEBOOK(book));g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(book)),==,3);return book;
}
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!window) { window=gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));for(guint i=0;i<8;i++) { pages[i]=find_page(i);g_assert_nonnull(pages[i]); }if(restore) phase=10;return G_SOURCE_CONTINUE; }
 switch(phase) {
 case 0: {
  click(0,"insight-panel-toggle-dock");join(0,1);g_assert_true(gtk_widget_get_toplevel(pages[0])==gtk_widget_get_toplevel(pages[1]));click(2,"insight-panel-toggle-dock");join(2,0);GtkWidget *book=check_group();gtk_window_resize(GTK_WINDOW(floating),720,600);
  gtk_notebook_reorder_child(GTK_NOTEBOOK(book),pages[2],0);gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),pages[1]));
  command(window,"insight-panel-transparency-toggle");gtk_scale_button_set_value(GTK_SCALE_BUTTON(named(pages[1],"insight-panel-opacity")),75);for(guint i=0;i<3;i++) g_assert_cmpfloat(gtk_scale_button_get_value(GTK_SCALE_BUTTON(named(pages[i],"insight-panel-opacity"))),==,75);
  phase++;break;
 }
 case 1: {
  command(window,"insight-layout-save-0");command(pages[1],"insight-panel-ungroup");g_assert_true(gtk_widget_get_toplevel(pages[1])!=gtk_widget_get_toplevel(pages[0]));g_assert_true(gtk_widget_get_toplevel(pages[0])==gtk_widget_get_toplevel(pages[2]));
  click(0,"insight-panel-toggle-dock");g_assert_true(gtk_widget_get_toplevel(pages[0])==window);g_assert_true(GTK_IS_WINDOW(gtk_widget_get_toplevel(pages[2])));g_assert_true(gtk_widget_get_toplevel(pages[2])!=window);
  GtkWidget *remaining=gtk_widget_get_toplevel(pages[2]);GdkEvent *event=gdk_event_new(GDK_DELETE);gboolean handled=FALSE;g_signal_emit_by_name(remaining,"delete-event",event,&handled);gdk_event_free(event);g_assert_true(handled);g_assert_true(gtk_widget_get_toplevel(pages[2])==window);
  command(window,"insight-layout-load-0");phase++;break;
 }
 case 2: {
  GtkWidget *book=check_group();g_assert_true(gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),0)==pages[2]);g_assert_true(gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),gtk_notebook_get_current_page(GTK_NOTEBOOK(book)))==pages[1]);
  click(1,"insight-panel-minimize");g_assert_false(gtk_widget_get_visible(floating));GtkWidget *tab=named(window,"insight-panel-tray-0");g_assert_nonnull(tab);g_assert_true(gtk_widget_get_visible(tab));g_assert_nonnull(strstr(gtk_button_get_label(GTK_BUTTON(tab)),"+2"));
  command(window,"insight-layout-save-1");command(window,"insight-layout-load-0");g_assert_true(gtk_widget_get_visible(gtk_widget_get_toplevel(pages[0])));command(window,"insight-layout-load-1");phase++;break;
 }
 case 3: {
  check_group();g_assert_false(gtk_widget_get_visible(floating));gchar *path=g_build_filename(profile,"insight","workbench.conf",NULL);GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL));g_free(path);for(guint i=0;i<3;i++) { gchar *name=g_strdup_printf("panel-%u-group",i);g_assert_cmpint(g_key_file_get_integer(key,"layout-B",name,NULL),==,1);g_free(name); }g_key_file_unref(key);
  gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 case 10: check_group();g_assert_false(gtk_widget_get_visible(floating));gtk_button_clicked(GTK_BUTTON(named(window,"insight-panel-tray-0")));phase++;break;
 case 11: {
  GtkWidget *book=check_group();g_assert_true(gtk_widget_get_visible(floating));g_assert_true(gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),0)==pages[2]);g_assert_true(gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),gtk_notebook_get_current_page(GTK_NOTEBOOK(book)))==pages[1]);g_assert_cmpfloat(gtk_widget_get_opacity(floating),>,0.74);g_assert_cmpfloat(gtk_widget_get_opacity(floating),<,0.76);
  const gchar *capture=g_getenv("GEANY_GROUP_CAPTURE");if(capture) { GdkWindow *native=gtk_widget_get_window(floating);GdkPixbuf *image=gdk_pixbuf_get_from_window(native,0,0,gdk_window_get_width(native),MIN(250,gdk_window_get_height(native)));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  GdkEvent *event=gdk_event_new(GDK_DELETE);gboolean handled=FALSE;g_signal_emit_by_name(floating,"delete-event",event,&handled);gdk_event_free(event);g_assert_true(handled);for(guint i=0;i<3;i++) g_assert_true(gtk_widget_get_toplevel(pages[i])==window);
  gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 default:g_assert_not_reached();
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Panel group GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 if(argc==1) { gchar *dir=g_dir_make_tmp("panel-groups-XXXXXX",NULL);g_assert_nonnull(dir);gchar *config=g_build_filename(dir,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));for(guint i=0;i<2;i++) { gchar *child[]={argv[0],dir,i ? "restore" : "fresh",NULL};gint status;GError *error=NULL;g_assert_true(g_spawn_sync(NULL,child,NULL,0,NULL,NULL,NULL,NULL,&status,&error));g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error); }g_free(config);g_free(dir);g_print("Panel groups GUI: join, separate, partial dock, close, shared opacity/tray, layout order/active tab and restart passed\n");return 0; }
 profile=argv[1];restore=!g_strcmp0(argv[2],"restore");gchar *args[]={"panel-groups-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(400,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);return main_lib(5,args);
}
