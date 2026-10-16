/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real Directory Compare widgets; editor help/translation and pair opener stubbed. */
#include <stdarg.h>
#include <glib/gstdio.h>
#include "../../src/insight-directory.c"
const gchar *insight_tr(const gchar *text) { return text; }
gchar *insight_i18n_printf(const gchar *format,...) { va_list args;va_start(args,format);gchar *text=g_strdup_vprintf(format,args);va_end(args);return text; }
void insight_panel_help_attach(GtkWidget *widget,const gchar *topic) { (void)widget;(void)topic; }
static gchar *opened[2];static guint opened_flags;
void insight_diff_show_files(GtkWidget *parent,const gchar *left,const gchar *right,guint flags) { (void)parent;g_free(opened[0]);g_free(opened[1]);opened[0]=g_strdup(left);opened[1]=g_strdup(right);opened_flags=flags; }
static void flush(void) { while(gtk_events_pending()) gtk_main_iteration(); }
static void wait_scan(Directory *d) {
 gint64 until=g_get_monotonic_time()+10*G_TIME_SPAN_SECOND;
 while(d->busy && g_get_monotonic_time()<until) { flush();g_usleep(1000); }g_assert_false(d->busy);flush();
}
static void put(const gchar *root,const gchar *name,const gchar *text) {
 gchar *path=g_build_filename(root,name,NULL);GError *error=NULL;g_assert_true(g_file_set_contents(path,text,-1,&error));g_assert_no_error(error);g_free(path);
}
static gboolean select_path(Directory *d,const gchar *name) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(d->view));GtkTreeIter iter;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) {
  gchar *path;gtk_tree_model_get(model,&iter,DC_PATH,&path,-1);gboolean equal=!strcmp(path,name);g_free(path);
  if(equal) { GtkTreePath *p=gtk_tree_model_get_path(model,&iter);gtk_tree_view_set_cursor(GTK_TREE_VIEW(d->view),p,NULL,FALSE);gtk_tree_path_free(p);return TRUE; }
 }return FALSE;
}
static void finalized(gpointer data,GObject *object) { (void)object;*(gboolean*)data=TRUE; }
int main(int argc,char **argv) {
 gtk_init(&argc,&argv);GError *error=NULL;gchar *root=g_dir_make_tmp("insight-directory-gui-XXXXXX",&error);g_assert_no_error(error);
 gchar *left=g_build_filename(root,"left",NULL),*right=g_build_filename(root,"right",NULL),*empty=g_build_filename(root,"empty",NULL);
 g_assert_cmpint(g_mkdir(left,0700),==,0);g_assert_cmpint(g_mkdir(right,0700),==,0);g_assert_cmpint(g_mkdir(empty,0700),==,0);
 put(left,"config.ini","port = 3000\n");put(right,"config.ini","port = 8080\n");put(left,"same.py","print('same')\n");put(right,"same.py","print('same')\n");put(left,"old.lua","old\n");put(right,"new.cpp","new\n");put(left,"данные.txt","я b\r\n");put(right,"данные.txt","яb\n");
 Directory *d=directory_new(NULL);flush();start(NULL,d);g_assert_false(d->busy);g_assert_false(gtk_widget_get_sensitive(d->open));
 gtk_entry_set_text(GTK_ENTRY(d->paths[0]),left);gtk_entry_set_text(GTK_ENTRY(d->paths[1]),right);start(NULL,d);wait_scan(d);
 g_assert_cmpuint(d->rows->len,==,5);g_assert_true(select_path(d,"config.ini"));g_assert_true(gtk_widget_get_sensitive(d->open));open_pair(NULL,d);g_assert_true(g_str_has_suffix(opened[0],"config.ini"));g_assert_cmpuint(opened_flags,==,0);
 gtk_entry_set_text(GTK_ENTRY(d->paths[0]),empty);open_pair(NULL,d);gchar *expected=g_build_filename(left,"config.ini",NULL);g_assert_cmpstr(opened[0],==,expected);g_free(expected);gtk_entry_set_text(GTK_ENTRY(d->paths[0]),left);
 gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(d->view))),DC_PATH,GTK_SORT_DESCENDING);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->different),TRUE);g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,4);
 gtk_entry_set_text(GTK_ENTRY(d->filter_entry),"ДАННЫЕ");g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,1);g_assert_true(select_path(d,"данные.txt"));
 copy_list(NULL,d);gchar *clip=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(strstr(clip,"данные.txt"));g_assert_null(strstr(clip,"same.py"));g_free(clip);
 gtk_entry_set_text(GTK_ENTRY(d->filter_entry),"");g_assert_true(select_path(d,"old.lua"));g_assert_false(gtk_widget_get_sensitive(d->open));
 gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(d->view)));advance(d,1);g_assert_nonnull(selected_entry(d));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->different),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->space),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->eol),TRUE);start(NULL,d);wait_scan(d);
 g_assert_true(select_path(d,"данные.txt"));g_assert_cmpint(selected_entry(d)->status,==,INSIGHT_DIR_SAME);open_pair(NULL,d);g_assert_cmpuint(opened_flags,==,3);
 GtkWidget *menu=context_menu(d);g_assert_nonnull(menu);gtk_widget_destroy(menu);
 const gchar *capture=g_getenv("INSIGHT_DIRECTORY_CAPTURE");
 if(capture) {
  gint64 until=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<until) { flush();g_usleep(1000); }
  GdkWindow *window=gtk_widget_get_window(d->window);GdkPixbuf *pixels=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));g_assert_nonnull(pixels);
  g_assert_true(gdk_pixbuf_save(pixels,capture,"jpeg",&error,"quality","88",NULL));g_assert_no_error(error);g_object_unref(pixels);
 }
 guint previous=d->rows->len;start(NULL,d);cancel_scan(NULL,d);wait_scan(d);g_assert_cmpuint(d->rows->len,==,previous);
 gtk_entry_set_text(GTK_ENTRY(d->paths[1]),"/path/that/does/not/exist");start(NULL,d);wait_scan(d);g_assert_cmpuint(d->rows->len,==,previous);
 gtk_entry_set_text(GTK_ENTRY(d->paths[0]),empty);gtk_entry_set_text(GTK_ENTRY(d->paths[1]),empty);start(NULL,d);wait_scan(d);g_assert_cmpuint(d->rows->len,==,0);g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,0);
 gboolean gone=FALSE;g_object_weak_ref(G_OBJECT(d->window),finalized,&gone);start(NULL,d);gtk_widget_destroy(d->window);
 gint64 until=g_get_monotonic_time()+10*G_TIME_SPAN_SECOND;while(!gone && g_get_monotonic_time()<until) { flush();g_usleep(1000); }g_assert_true(gone);
 const gchar *names[]={"config.ini","same.py","old.lua","new.cpp","данные.txt",NULL};for(guint i=0;names[i];i++) for(guint side=0;side<2;side++) { gchar *path=g_build_filename(side ? right : left,names[i],NULL);if(g_file_test(path,G_FILE_TEST_EXISTS)) g_remove(path);g_free(path); }
 g_rmdir(left);g_rmdir(right);g_rmdir(empty);g_rmdir(root);g_free(left);g_free(right);g_free(empty);g_free(root);g_free(opened[0]);g_free(opened[1]);
 g_print("Directory production GUI: scan, filter, sort, pair routing, clipboard, cancellation, empty results and close passed.\n");return 0;
}
