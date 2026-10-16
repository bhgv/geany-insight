/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real Directory Compare widgets; editor help/translation and pair opener stubbed. */
#include <stdarg.h>
#include <glib/gstdio.h>
#include "../../src/insight-directory.c"
void insight_i18n_apply(GtkWidget *widget) { (void)widget; }
const gchar *insight_tr(const gchar *text) { return text; }
gchar *insight_i18n_printf(const gchar *format,...) { va_list args;va_start(args,format);gchar *text=g_strdup_vprintf(format,args);va_end(args);return text; }
void insight_panel_help_attach(GtkWidget *widget,const gchar *topic) { (void)widget;(void)topic; }
static gchar **known_types(void) { const gchar *names[]={"*.ini","*.cpp",NULL};return g_strdupv((gchar **)names); }
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
static GtkWidget *named(GtkWidget *widget,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(widget),name)) return widget;
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static gboolean appearance_response(gpointer data) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p && !dialog;p=p->next) if(!strcmp(gtk_widget_get_name(p->data),"insight-list-style-dialog")) dialog=p->data;
 g_list_free(windows);g_assert_nonnull(dialog);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-list-style-enabled")),TRUE);gtk_font_chooser_set_font(GTK_FONT_CHOOSER(named(dialog,"insight-list-style-font")),"Monospace 12");
 gtk_dialog_response(GTK_DIALOG(dialog),GPOINTER_TO_INT(data));return G_SOURCE_REMOVE;
}
int main(int argc,char **argv) {
 gtk_init(&argc,&argv);GError *error=NULL;gchar *root=g_dir_make_tmp("insight-directory-gui-XXXXXX",&error);g_assert_no_error(error);
 gchar *left=g_build_filename(root,"left",NULL),*right=g_build_filename(root,"right",NULL),*empty=g_build_filename(root,"empty",NULL);
 g_assert_cmpint(g_mkdir(left,0700),==,0);g_assert_cmpint(g_mkdir(right,0700),==,0);g_assert_cmpint(g_mkdir(empty,0700),==,0);
 put(left,"config.ini","port = 3000\n");put(right,"config.ini","port = 8080\n");put(left,"same.py","print('same')\n");put(right,"same.py","print('same')\n");put(left,"old.lua","old\n");put(right,"new.cpp","new\n");put(left,"данные.txt","я b\r\n");put(right,"данные.txt","яb\n");
 insight_directory_set_types_provider(known_types);
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
 gint sort_column;GtkSortType sort_order;g_assert_true(gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(d->view))),&sort_column,&sort_order));g_assert_cmpint(sort_column,==,DC_PATH);g_assert_cmpint(sort_order,==,GTK_SORT_DESCENDING);
 g_assert_true(select_path(d,"данные.txt"));g_assert_cmpint(selected_entry(d)->status,==,INSIGHT_DIR_SAME);open_pair(NULL,d);g_assert_cmpuint(opened_flags,==,3);
 gtk_entry_set_text(GTK_ENTRY(d->include),"*.py; *.ini");g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,2);
 gtk_entry_set_text(GTK_ENTRY(d->exclude),"same.*");g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,1);
 gtk_combo_box_set_active(GTK_COMBO_BOX(d->status_filter),INSIGHT_DIR_SAME+1);g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,0);
 gtk_entry_set_text(GTK_ENTRY(d->exclude),"");g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,1);
 gtk_entry_set_text(GTK_ENTRY(d->include),"*.PY");g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,0);
 gtk_entry_set_text(GTK_ENTRY(d->include)," ; ; ");gtk_combo_box_set_active(GTK_COMBO_BOX(d->status_filter),0);g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,5);
 g_assert_true(select_path(d,"данные.txt"));
 GtkWidget *column_menu=columns_menu(d);GList *column_items=gtk_container_get_children(GTK_CONTAINER(column_menu));g_assert_cmpuint(g_list_length(column_items),==,4);
 g_assert_false(gtk_widget_get_sensitive(column_items->data));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_list_nth_data(column_items,2)),FALSE);
 g_assert_false(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(d->view),2)));
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_list_nth_data(column_items,2)),TRUE);g_list_free(column_items);gtk_widget_destroy(column_menu);
 GtkWidget *menu=context_menu(d);g_assert_nonnull(menu);gtk_widget_destroy(menu);
 gtk_entry_set_text(GTK_ENTRY(d->include),"");
 const gchar *capture=g_getenv("INSIGHT_DIRECTORY_CAPTURE");
 if(capture) {
  gint64 until=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<until) { flush();g_usleep(1000); }
  GdkWindow *window=gtk_widget_get_window(d->window);GdkPixbuf *pixels=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));g_assert_nonnull(pixels);
  g_assert_true(gdk_pixbuf_save(pixels,capture,"jpeg",&error,"quality","88",NULL));g_assert_no_error(error);g_object_unref(pixels);
 }
 g_assert_true(select_path(d,"config.ini"));GtkWidget *path_action=gtk_menu_item_new();g_object_ref_sink(path_action);g_object_set_data(G_OBJECT(path_action),"side",GINT_TO_POINTER(0));
 gtk_entry_set_text(GTK_ENTRY(d->paths[0]),empty);copy_absolute(path_action,d);clip=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));expected=g_build_filename(left,"config.ini",NULL);g_assert_cmpstr(clip,==,expected);g_free(clip);g_free(expected);gtk_entry_set_text(GTK_ENTRY(d->paths[0]),left);gtk_widget_destroy(path_action);g_object_unref(path_action);
 gtk_entry_set_text(GTK_ENTRY(d->include),"*.ini");gtk_entry_set_text(GTK_ENTRY(d->exclude),"*.ini");gtk_entry_set_text(GTK_ENTRY(d->filter_entry),"missing");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->different),TRUE);gtk_combo_box_set_active(GTK_COMBO_BOX(d->status_filter),INSIGHT_DIR_ERROR+1);reset_filters(NULL,d);g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,5);
 swap_and_scan(NULL,d);wait_scan(d);g_assert_cmpstr(d->roots[0],==,right);g_assert_cmpstr(d->roots[1],==,left);g_assert_true(select_path(d,"old.lua"));g_assert_cmpint(selected_entry(d)->status,==,INSIGHT_DIR_RIGHT);
 swap_and_scan(NULL,d);wait_scan(d);g_assert_cmpstr(d->roots[0],==,left);g_assert_cmpstr(d->roots[1],==,right);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->known),TRUE);start(NULL,d);wait_scan(d);g_assert_cmpuint(d->rows->len,==,2);g_assert_true(select_path(d,"config.ini"));g_assert_false(select_path(d,"same.py"));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->known),FALSE);start(NULL,d);wait_scan(d);g_assert_cmpuint(d->rows->len,==,5);
 guint previous=d->rows->len;start(NULL,d);
 gint64 publication_deadline=g_get_monotonic_time()+10*G_TIME_SPAN_SECOND;
 while(d->busy && !d->publication && g_get_monotonic_time()<publication_deadline) { g_main_context_iteration(NULL,FALSE);g_usleep(1000); }
 g_assert_cmpuint(d->publication,>,0);cancel_scan(NULL,d);g_assert_false(d->busy);g_assert_cmpuint(d->rows->len,==,previous);
 start(NULL,d);cancel_scan(NULL,d);wait_scan(d);g_assert_cmpuint(d->rows->len,==,previous);
 gtk_entry_set_text(GTK_ENTRY(d->paths[1]),"/path/that/does/not/exist");start(NULL,d);wait_scan(d);g_assert_cmpuint(d->rows->len,==,previous);
 gtk_entry_set_text(GTK_ENTRY(d->paths[0]),empty);gtk_entry_set_text(GTK_ENTRY(d->paths[1]),empty);start(NULL,d);wait_scan(d);g_assert_cmpuint(d->rows->len,==,0);g_assert_cmpint(gtk_tree_model_iter_n_children(d->filtered,NULL),==,0);
 GtkWidget *style_button=named(d->window,"insight-directory-appearance");g_assert_nonnull(style_button);
 g_idle_add(appearance_response,GINT_TO_POINTER(GTK_RESPONSE_CANCEL));gtk_button_clicked(GTK_BUTTON(style_button));
 GKeyFile *settings=g_key_file_new();insight_directory_write(settings);g_assert_false(g_key_file_get_boolean(settings,style_group,"enabled",NULL));
 g_idle_add(appearance_response,GINT_TO_POINTER(GTK_RESPONSE_APPLY));gtk_button_clicked(GTK_BUTTON(style_button));insight_directory_write(settings);g_assert_true(g_key_file_get_boolean(settings,style_group,"enabled",NULL));
 gtk_entry_set_text(GTK_ENTRY(d->include),"*.ini;*.cpp");gtk_entry_set_text(GTK_ENTRY(d->exclude),"backup*");gtk_combo_box_set_active(GTK_COMBO_BOX(d->method),INSIGHT_DIR_SIZE_DATE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->known),TRUE);
 gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(d->view),3),FALSE);insight_directory_write(settings);
 gboolean gone=FALSE;g_object_weak_ref(G_OBJECT(d->window),finalized,&gone);start(NULL,d);gtk_widget_destroy(d->window);
 gint64 until=g_get_monotonic_time()+10*G_TIME_SPAN_SECOND;while(!gone && g_get_monotonic_time()<until) { flush();g_usleep(1000); }g_assert_true(gone);
 insight_directory_read(settings);Directory *restored=directory_new(NULL);flush();g_assert_false(restored->busy);g_assert_null(restored->rows);
 g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(restored->include)),==,"*.ini;*.cpp");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(restored->paths[0])),==,empty);g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(restored->method)),==,INSIGHT_DIR_SIZE_DATE);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(restored->known)));g_assert_false(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(restored->view),3)));
 GKeyFile *again=g_key_file_new();insight_directory_write(again);g_assert_true(g_key_file_get_boolean(again,style_group,"enabled",NULL));gchar *font=g_key_file_get_string(again,style_group,"font",NULL);g_assert_cmpstr(font,==,"Monospace 12");g_free(font);
 Directory *second=directory_new(NULL);gtk_entry_set_text(GTK_ENTRY(second->include),"*.py");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(restored->include)),==,"*.ini;*.cpp");insight_directory_write(again);gchar *mask=g_key_file_get_string(again,options_group,"include",NULL);g_assert_cmpstr(mask,==,"*.py");g_free(mask);gtk_widget_destroy(second->window);gtk_widget_destroy(restored->window);
 g_key_file_set_integer(settings,options_group,"method",999);g_key_file_set_integer(settings,options_group,"status",-8);g_key_file_set_value(settings,options_group,"recursive","broken");g_key_file_set_boolean(settings,options_group,"column-0",FALSE);g_key_file_set_integer(settings,options_group,"width",99999);g_key_file_set_string(settings,style_group,"font","Sans 999");
 insight_directory_read(settings);insight_directory_set_types_provider(NULL);restored=directory_new(NULL);g_assert_false(restored->busy);g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(restored->method)),==,0);g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(restored->status_filter)),==,0);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(restored->recursive)));g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(restored->known)));g_assert_true(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(restored->view),0)));gtk_widget_destroy(restored->window);
 g_key_file_unref(settings);g_key_file_unref(again);g_clear_pointer(&saved_options,g_key_file_unref);g_assert_null(open_windows);
 const gchar *names[]={"config.ini","same.py","old.lua","new.cpp","данные.txt",NULL};for(guint i=0;names[i];i++) for(guint side=0;side<2;side++) { gchar *path=g_build_filename(side ? right : left,names[i],NULL);if(g_file_test(path,G_FILE_TEST_EXISTS)) g_remove(path);g_free(path); }
 g_rmdir(left);g_rmdir(right);g_rmdir(empty);g_rmdir(root);g_free(left);g_free(right);g_free(empty);g_free(root);g_free(opened[0]);g_free(opened[1]);
 g_print("Directory production GUI: scan, filter, sort, pair routing, clipboard, cancellation, empty results and close passed.\n");return 0;
}
