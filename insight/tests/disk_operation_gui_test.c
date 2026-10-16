/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Isolated production Folder Browser. Editor documents and unrelated styling /
 * translation services are supplied below; no editor session is started. */
#include "geany.h"
#include "document.h"
#include "insight-disk-browser.h"
#include "insight-i18n.h"
#include "insight-list-style.h"
#include <glib/gstdio.h>
#include <stdarg.h>
#include <string.h>
GPtrArray *documents_array;
const gchar *insight_tr(const gchar *text) { return text; }
void insight_i18n_apply(GtkWidget *widget) { (void)widget; }
gchar *insight_i18n_printf(const gchar *format,...) { va_list args;va_start(args,format);gchar *text=g_strdup_vprintf(format,args);va_end(args);return text; }
GtkWidget *insight_list_style_button(GtkWidget *view) { (void)view;return gtk_button_new_with_label("Font & Color"); }
GtkWidget *insight_list_style_menu_item(GtkWidget *view) { (void)view;return gtk_menu_item_new_with_label("Font & Color"); }
void insight_list_style_read(GtkWidget *view,GKeyFile *key,const gchar *group) { (void)view;(void)key;(void)group; }
void insight_list_style_write(GtkWidget *view,GKeyFile *key,const gchar *group) { (void)view;(void)key;(void)group; }
static GtkWidget *window,*browser,*view;
static gchar *root,*destination;
static guint response_mode,previews;
static GtkWidget *named(GtkWidget *widget,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(widget),name)) return widget;
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static gboolean has(const gchar *name,gboolean select) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter iter;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) {
  gchar *value=NULL;gtk_tree_model_get(model,&iter,0,&value,-1);gboolean match=!g_strcmp0(value,name);g_free(value);
  if(match) { if(select) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&iter);return TRUE; }
 }return FALSE;
}
static void wait_for(const gchar *name) {
 gint64 end=g_get_monotonic_time()+5*G_TIME_SPAN_SECOND;
 while(!has(name,FALSE) && g_get_monotonic_time()<end) { while(g_main_context_iteration(NULL,FALSE));g_usleep(1000); }
 g_assert_true(has(name,FALSE));
}
static void wait_status(const gchar *fragment) {
 GtkWidget *label=named(browser,"insight-disk-status");gint64 end=g_get_monotonic_time()+5*G_TIME_SPAN_SECOND;
 while(!strstr(gtk_label_get_text(GTK_LABEL(label)),fragment) && g_get_monotonic_time()<end) { while(g_main_context_iteration(NULL,FALSE));g_usleep(1000); }
 g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(label)),fragment));
}
static GtkWidget *dialog(void) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;
 for(GList *p=windows;p && !found;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-disk-change-dialog")) found=p->data;
 g_list_free(windows);g_assert_nonnull(found);return found;
}
static gboolean reject_then_cancel(gpointer data) {
 (void)data;GtkWidget *d=dialog();g_assert_true(gtk_widget_get_visible(named(d,"insight-disk-change-error")));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog();
 g_assert_true(gtk_window_get_default_widget(GTK_WINDOW(d))==gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT));
 if(destination) gtk_entry_set_text(GTK_ENTRY(named(d,"insight-disk-change-destination")),destination);
 if(response_mode==2) { gchar *other=g_build_filename(root,"folder",NULL);insight_disk_browser_set_root(browser,other);g_free(other); }
 if(response_mode) g_timeout_add(10,reject_then_cancel,NULL);
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static gboolean cancel_dialog(gpointer data) { (void)data;GtkWidget *d=dialog();g_assert_true(gtk_window_get_default_widget(GTK_WINDOW(d))==gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
static void action(const gchar *name,const gchar *value,guint mode,gboolean cancel) {
 gboolean handled=FALSE;g_signal_emit_by_name(view,"popup-menu",&handled);g_assert_true(handled);GtkWidget *menu=g_object_get_data(G_OBJECT(view),"insight-disk-popup");g_assert_nonnull(menu);
 GtkWidget *item=named(menu,name);g_assert_nonnull(item);g_assert_true(gtk_widget_get_sensitive(item));destination=(gchar *)value;response_mode=mode;g_timeout_add(10,cancel ? cancel_dialog : respond,NULL);gtk_menu_item_activate(GTK_MENU_ITEM(item));gtk_menu_popdown(GTK_MENU(menu));
}
static void opened(const gchar *path,gboolean activate,gpointer data) { (void)path;(void)activate;(void)data;previews++; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !gtk_init_check(&argc,&argv)) return 77;
 GError *error=NULL;root=g_dir_make_tmp("insight-operation-gui-XXXXXX",&error);g_assert_no_error(error);
 gchar *from=g_build_filename(root,"исходный.txt",NULL),*renamed=g_build_filename(root,"новое.txt",NULL),*moved=g_build_filename(root,"folder","moved.txt",NULL),*folder=g_build_filename(root,"folder",NULL),*stale=g_build_filename(root,"stale.txt",NULL);
 g_assert_cmpint(g_mkdir(folder,0700),==,0);g_assert_true(g_file_set_contents(from,"keep",-1,&error));g_assert_true(g_file_set_contents(stale,"old",-1,&error));
 documents_array=g_ptr_array_new();window=gtk_window_new(GTK_WINDOW_TOPLEVEL);browser=insight_disk_browser_new(opened,NULL);gtk_container_add(GTK_CONTAINER(window),browser);gtk_window_set_default_size(GTK_WINDOW(window),650,500);gtk_widget_show_all(window);view=named(browser,"insight-disk-files");
 insight_disk_browser_current_file(browser,NULL,TRUE);insight_disk_browser_set_root(browser,root);wait_for("исходный.txt");g_assert_true(has("исходный.txt",TRUE));
 action("insight-disk-menu-rename","../escape",1,FALSE);g_assert_true(g_file_test(from,G_FILE_TEST_EXISTS));
 action("insight-disk-menu-trash",NULL,0,TRUE);g_assert_true(g_file_test(from,G_FILE_TEST_EXISTS));
 GeanyDocument doc={0};doc.is_valid=TRUE;doc.file_name=from;g_ptr_array_add(documents_array,&doc);
 action("insight-disk-menu-rename","blocked.txt",1,FALSE);g_assert_true(g_file_test(from,G_FILE_TEST_EXISTS));g_ptr_array_set_size(documents_array,0);
 guint before=previews;action("insight-disk-menu-rename","новое.txt",0,FALSE);wait_for("новое.txt");g_assert_false(has("исходный.txt",FALSE));g_assert_false(g_file_test(from,G_FILE_TEST_EXISTS));g_assert_cmpuint(previews,==,before);
 g_assert_true(has("новое.txt",TRUE));action("insight-disk-menu-move",stale,0,FALSE);wait_status("Cannot change disk entry:");insight_disk_browser_locale_changed(browser);wait_status("Cannot change disk entry:");g_assert_true(g_file_test(renamed,G_FILE_TEST_EXISTS));gchar *text=NULL;g_assert_true(g_file_get_contents(stale,&text,NULL,&error));g_assert_cmpstr(text,==,"old");g_free(text);
 action("insight-disk-menu-move",moved,0,FALSE);wait_status("Disk entries:");g_assert_false(g_file_test(renamed,G_FILE_TEST_EXISTS));g_assert_true(g_file_get_contents(moved,&text,NULL,&error));g_assert_cmpstr(text,==,"keep");g_free(text);
 g_assert_true(has("stale.txt",TRUE));g_assert_true(g_file_set_contents(stale,"changed outside browser",-1,&error));action("insight-disk-menu-rename","wrong.txt",0,FALSE);wait_status("Cannot change disk entry:");g_assert_true(g_file_test(stale,G_FILE_TEST_EXISTS));
 /* A directory containing an open document must also be protected. */
 g_assert_true(has("folder",TRUE));doc.file_name=moved;g_ptr_array_add(documents_array,&doc);action("insight-disk-menu-rename","blocked-folder",1,FALSE);g_assert_true(g_file_test(moved,G_FILE_TEST_EXISTS));g_ptr_array_set_size(documents_array,0);
 action("insight-disk-menu-rename","stale-root",2,FALSE);wait_for("moved.txt");g_assert_true(g_file_test(folder,G_FILE_TEST_IS_DIR));
 /* Close while a directory read is pending; completions must not touch UI. */
 insight_disk_browser_set_root(browser,root);gtk_widget_destroy(window);gint64 end=g_get_monotonic_time()+100000;while(g_get_monotonic_time()<end) { while(g_main_context_iteration(NULL,FALSE));g_usleep(1000); }
 g_ptr_array_unref(documents_array);documents_array=NULL;g_unlink(stale);g_unlink(moved);g_rmdir(folder);g_rmdir(root);g_free(root);g_free(from);g_free(renamed);g_free(moved);g_free(folder);g_free(stale);
 g_print("Disk operation GUI: dialogs, Unicode rename/move, collision, stale snapshot/root, open documents, trash cancellation and shutdown passed\n");return 0;
}
