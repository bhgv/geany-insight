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
static void opened(const gchar *,gboolean,gpointer);
static GtkWidget *folder_split(void) { return named(browser,"insight-disk-split"); }
static void selected_folder_visible(void) {
 GtkWidget *folders=named(browser,"insight-disk-folders");gboolean visible=FALSE;gint64 end=g_get_monotonic_time()+2*G_TIME_SPAN_SECOND;
 do {
  while(g_main_context_iteration(NULL,FALSE));
  GtkTreeModel *model;GtkTreeIter iter;
  if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(folders)),&model,&iter)) { GtkTreePath *selected=gtk_tree_model_get_path(model,&iter),*first=NULL,*last=NULL;if(gtk_tree_view_get_visible_range(GTK_TREE_VIEW(folders),&first,&last)) { GdkRectangle area;gtk_tree_view_get_cell_area(GTK_TREE_VIEW(folders),selected,NULL,&area);visible=gtk_tree_path_compare(selected,first)>=0 && gtk_tree_path_compare(selected,last)<=0 && area.y>=0 && area.height>0 && area.y+area.height<=gdk_window_get_height(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(folders)));gtk_tree_path_free(first);gtk_tree_path_free(last); }gtk_tree_path_free(selected); }
  if(!visible) g_usleep(1000);
 } while(!visible && g_get_monotonic_time()<end);
 g_assert_true(visible);
}
static void resize_layout(gint width,gint height,GtkOrientation orientation) {
 g_printerr("Layout resize %dx%d expected %d\n",width,height,orientation);gtk_window_resize(GTK_WINDOW(window),width,height);gint64 end=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
 do { while(g_main_context_iteration(NULL,FALSE));g_usleep(1000); } while(g_get_monotonic_time()<end && gtk_orientable_get_orientation(GTK_ORIENTABLE(folder_split()))!=orientation);
 for(guint n=0;n<30;n++) { while(g_main_context_iteration(NULL,FALSE));g_usleep(1000); }
 g_printerr("Actual split %dx%d orientation %d\n",gtk_widget_get_allocated_width(folder_split()),gtk_widget_get_allocated_height(folder_split()),gtk_orientable_get_orientation(GTK_ORIENTABLE(folder_split())));g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(folder_split())),==,orientation);selected_folder_visible();
}
static GtkWidget *layout_dialog(void) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;
 for(GList *p=windows;p && !found;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-disk-options-dialog")) found=p->data;
 g_list_free(windows);g_assert_nonnull(found);return found;
}
static gboolean layout_adaptive,layout_vertical,layout_cancel;
static gboolean layout_response(gpointer data) {
 (void)data;GtkWidget *d=layout_dialog(),*adaptive=named(d,"insight-disk-options-adaptive"),*vertical=named(d,"insight-disk-options-vertical");
 g_assert_nonnull(adaptive);g_assert_nonnull(vertical);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(adaptive),layout_adaptive);g_assert_cmpint(gtk_widget_get_sensitive(vertical),==,!layout_adaptive);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vertical),layout_vertical);
 g_assert_nonnull(named(d,"insight-disk-options-folder-appearance"));gtk_dialog_response(GTK_DIALOG(d),layout_cancel ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE;
}
static void layout_options(gboolean adaptive,gboolean vertical,gboolean cancel) {
 layout_adaptive=adaptive;layout_vertical=vertical;layout_cancel=cancel;g_timeout_add(10,layout_response,NULL);gtk_button_clicked(GTK_BUTTON(named(browser,"insight-disk-options")));
}
static gchar *wanted_folder;static GtkTreePath *found_folder;
static gboolean find_folder(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 (void)data;gchar *value=NULL;gtk_tree_model_get(model,iter,1,&value,-1);gboolean match=!g_strcmp0(value,wanted_folder);g_free(value);if(match) found_folder=gtk_tree_path_copy(path);return match;
}
static void select_folder(const gchar *path) {
 GtkWidget *folders=named(browser,"insight-disk-folders");wanted_folder=(gchar *)path;found_folder=NULL;gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(folders)),find_folder,NULL);g_assert_nonnull(found_folder);
 gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(folders)),found_folder);gtk_tree_path_free(found_folder);found_folder=NULL;
}
static void folder_context_open(const gchar *path) {
 GtkWidget *folders=named(browser,"insight-disk-folders");wanted_folder=(gchar *)path;found_folder=NULL;gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(folders)),find_folder,NULL);g_assert_nonnull(found_folder);
 gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(folders),found_folder,NULL,FALSE,0,0);for(guint n=0;n<30;n++) { while(g_main_context_iteration(NULL,FALSE));g_usleep(1000); }
 GdkRectangle area;gtk_tree_view_get_cell_area(GTK_TREE_VIEW(folders),found_folder,gtk_tree_view_get_column(GTK_TREE_VIEW(folders),0),&area);gtk_tree_path_free(found_folder);found_folder=NULL;
 GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);event->button.window=g_object_ref(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(folders)));event->button.button=3;event->button.x=area.x+5;event->button.y=area.y+5;gdk_event_set_device(event,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));gboolean handled=FALSE;g_signal_emit_by_name(folders,"button-press-event",event,&handled);g_assert_true(handled);gdk_event_free(event);
 g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(browser,"insight-disk-root"))),==,root);GtkWidget *menu=g_object_get_data(G_OBJECT(folders),"insight-disk-folder-popup");g_assert_nonnull(menu);gtk_menu_item_activate(GTK_MENU_ITEM(named(menu,"insight-disk-folder-menu-copy-path")));gtk_menu_popdown(GTK_MENU(menu));gchar *copied=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(copied,==,path);g_free(copied);
 g_signal_emit_by_name(folders,"popup-menu",&handled);g_assert_true(handled);gtk_menu_item_activate(GTK_MENU_ITEM(named(menu,"insight-disk-folder-menu-open")));gtk_menu_popdown(GTK_MENU(menu));
}
static void check_folder_layout(const gchar *folder) {
 GtkWidget *folders=named(browser,"insight-disk-folders");g_assert_nonnull(folders);guint before=previews;
 resize_layout(1000,500,GTK_ORIENTATION_HORIZONTAL);resize_layout(450,1000,GTK_ORIENTATION_VERTICAL);
 layout_options(FALSE,FALSE,TRUE);g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(folder_split())),==,GTK_ORIENTATION_VERTICAL);
 layout_options(FALSE,FALSE,FALSE);g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(folder_split())),==,GTK_ORIENTATION_HORIZONTAL);gtk_paned_set_position(GTK_PANED(folder_split()),150);
 layout_options(FALSE,TRUE,FALSE);g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(folder_split())),==,GTK_ORIENTATION_VERTICAL);gtk_paned_set_position(GTK_PANED(folder_split()),110);
 layout_options(FALSE,FALSE,FALSE);resize_layout(1000,500,GTK_ORIENTATION_HORIZONTAL);g_assert_cmpint(gtk_paned_get_position(GTK_PANED(folder_split())),==,150);
 layout_options(FALSE,TRUE,FALSE);resize_layout(450,1000,GTK_ORIENTATION_VERTICAL);g_assert_cmpint(gtk_paned_get_position(GTK_PANED(folder_split())),==,110);
 GKeyFile *key=g_key_file_new();insight_disk_browser_write(browser,key);g_assert_false(g_key_file_get_boolean(key,"disk-browser","adaptive-layout",NULL));g_assert_true(g_key_file_get_boolean(key,"disk-browser","stack-vertically",NULL));g_assert_cmpint(g_key_file_get_integer(key,"disk-browser","horizontal-position",NULL),==,150);g_assert_cmpint(g_key_file_get_integer(key,"disk-browser","vertical-position",NULL),==,110);
 GtkWidget *restored=insight_disk_browser_new(opened,NULL);g_object_ref_sink(restored);g_assert_true(insight_disk_browser_read(restored,key));GKeyFile *round=g_key_file_new();insight_disk_browser_write(restored,round);g_assert_false(g_key_file_get_boolean(round,"disk-browser","adaptive-layout",NULL));g_assert_true(g_key_file_get_boolean(round,"disk-browser","stack-vertically",NULL));g_assert_cmpint(g_key_file_get_integer(round,"disk-browser","horizontal-position",NULL),==,150);g_assert_cmpint(g_key_file_get_integer(round,"disk-browser","vertical-position",NULL),==,110);g_key_file_unref(round);gtk_widget_destroy(restored);g_object_unref(restored);
 g_key_file_set_string(key,"disk-browser","adaptive-layout","bad");g_key_file_set_string(key,"disk-browser","stack-vertically","bad");g_key_file_set_integer(key,"disk-browser","horizontal-position",-1);g_key_file_set_integer(key,"disk-browser","vertical-position",99999);
 restored=insight_disk_browser_new(opened,NULL);g_object_ref_sink(restored);g_assert_true(insight_disk_browser_read(restored,key));round=g_key_file_new();insight_disk_browser_write(restored,round);g_assert_true(g_key_file_get_boolean(round,"disk-browser","adaptive-layout",NULL));g_assert_true(g_key_file_get_boolean(round,"disk-browser","stack-vertically",NULL));g_assert_cmpint(g_key_file_get_integer(round,"disk-browser","horizontal-position",NULL),==,180);g_assert_cmpint(g_key_file_get_integer(round,"disk-browser","vertical-position",NULL),==,130);g_key_file_unref(round);g_key_file_unref(key);gtk_widget_destroy(restored);g_object_unref(restored);
 layout_options(TRUE,TRUE,FALSE);resize_layout(1000,500,GTK_ORIENTATION_HORIZONTAL);resize_layout(450,1000,GTK_ORIENTATION_VERTICAL);
 gchar *child=g_build_filename(folder,"tree-child.txt",NULL);g_assert_true(g_file_set_contents(child,"folder navigation",-1,NULL));folder_context_open(folder);wait_for("tree-child.txt");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(browser,"insight-disk-root"))),==,folder);g_assert_cmpuint(previews,==,before);
 gchar *sibling=g_build_filename(root,"folder-sibling",NULL),*sibling_file=g_build_filename(sibling,"sibling-marker.txt",NULL);g_assert_true(g_file_set_contents(sibling_file,"sibling",-1,NULL));select_folder(sibling);wait_for("sibling-marker.txt");g_assert_cmpuint(previews,==,before);
 select_folder(root);wait_for("исходный.txt");g_assert_cmpuint(previews,==,before);g_unlink(sibling_file);g_rmdir(sibling);g_free(sibling_file);g_free(sibling);g_unlink(child);g_free(child);
 gchar *hidden=g_build_filename(root,".hidden-follow",NULL),*file=g_build_filename(hidden,"follow.txt",NULL);g_assert_cmpint(g_mkdir(hidden,0700),==,0);g_assert_true(g_file_set_contents(file,"follow",-1,NULL));
 gtk_entry_set_text(GTK_ENTRY(named(browser,"insight-disk-query")),"absent-filter");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(browser,"insight-disk-follow")),TRUE);insight_disk_browser_current_file(browser,file,TRUE);wait_for("follow.txt");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(browser,"insight-disk-root"))),==,hidden);
 GtkTreeModel *selected_model;GtkTreeIter selected_iter;g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(folders)),&selected_model,&selected_iter));gchar *selected_path=NULL;gtk_tree_model_get(selected_model,&selected_iter,1,&selected_path,-1);g_assert_cmpstr(selected_path,==,hidden);g_free(selected_path);g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(browser,"insight-disk-hidden"))));g_assert_cmpuint(previews,==,before);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(browser,"insight-disk-follow")),FALSE);insight_disk_browser_current_file(browser,NULL,TRUE);gtk_entry_set_text(GTK_ENTRY(named(browser,"insight-disk-query")),"");insight_disk_browser_set_root(browser,root);wait_for("исходный.txt");g_assert_cmpuint(previews,==,before);g_unlink(file);g_rmdir(hidden);g_free(file);g_free(hidden);resize_layout(650,600,GTK_ORIENTATION_HORIZONTAL);
 g_print("Folder layout GUI: shape/manual modes, Cancel/Apply, splitter persistence, invalid settings, ancestor/subfolder navigation and hidden-file Follow passed\n");
}

static void opened(const gchar *path,gboolean activate,gpointer data) { (void)path;(void)activate;(void)data;previews++; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !gtk_init_check(&argc,&argv)) return 77;
 GError *error=NULL;root=g_dir_make_tmp("insight-operation-gui-XXXXXX",&error);g_assert_no_error(error);
 gchar *from=g_build_filename(root,"исходный.txt",NULL),*renamed=g_build_filename(root,"новое.txt",NULL),*moved=g_build_filename(root,"folder","moved.txt",NULL),*folder=g_build_filename(root,"folder",NULL),*stale=g_build_filename(root,"stale.txt",NULL);
 gchar *sibling=g_build_filename(root,"folder-sibling",NULL);g_assert_cmpint(g_mkdir(sibling,0700),==,0);g_free(sibling);g_assert_cmpint(g_mkdir(folder,0700),==,0);g_assert_true(g_file_set_contents(from,"keep",-1,&error));g_assert_true(g_file_set_contents(stale,"old",-1,&error));
 documents_array=g_ptr_array_new();window=gtk_window_new(GTK_WINDOW_TOPLEVEL);browser=insight_disk_browser_new(opened,NULL);gtk_container_add(GTK_CONTAINER(window),browser);gtk_window_set_default_size(GTK_WINDOW(window),650,500);gtk_widget_show_all(window);view=named(browser,"insight-disk-files");
 insight_disk_browser_current_file(browser,NULL,TRUE);insight_disk_browser_set_root(browser,root);wait_for("исходный.txt");check_folder_layout(folder);g_assert_true(has("исходный.txt",TRUE));
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
