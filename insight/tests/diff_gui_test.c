/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Isolated fixture using the production window and callbacks, not libgeany. */
#include <stdarg.h>
#include <glib/gstdio.h>
#include "../../src/insight-diff.c"
gchar *insight_i18n_printf(const gchar *format,...) {
 va_list args;va_start(args,format);gchar *s=g_strdup_vprintf(format,args);va_end(args);return s;
}
void insight_panel_help_attach(GtkWidget *widget,const gchar *topic) { (void)widget;(void)topic; }
static void flush(void) { while(gtk_events_pending()) gtk_main_iteration(); }
static void wait_comparison(Merge *m) {
 gint64 until=g_get_monotonic_time()+10*G_TIME_SPAN_SECOND;
 while(m->busy && g_get_monotonic_time()<until) { flush();g_usleep(1000); }
 g_assert_false(m->busy);flush();
}
static gboolean heartbeat(gpointer data) { (*(guint*)data)++;return G_SOURCE_CONTINUE; }
static void finalized(gpointer data,GObject *object) { (void)object;*(gboolean*)data=TRUE; }
static void assert_text(Merge *m,const char *expected) {
 gchar *s=contents(result(m));g_assert_cmpstr(s,==,expected);g_free(s);
}
int main(int argc,char **argv) {
 gtk_init(&argc,&argv);
 GError *error=NULL;gchar *dir=g_dir_make_tmp("insight-diff-XXXXXX",&error);g_assert_no_error(error);
 gchar *left=g_build_filename(dir,"left.txt",NULL),*right=g_build_filename(dir,"right.txt",NULL);
 const char *a="start\nleft\ncommon\nlast left\nend\n",*b="start\nright\ncommon\nlast right\nextra\nend\n";
 g_assert_true(g_file_set_contents(left,a,-1,&error));g_assert_no_error(error);
 g_assert_true(g_file_set_contents(right,b,-1,&error));g_assert_no_error(error);
 Merge *m=merge_new(NULL);flush();g_assert_false(gtk_widget_get_sensitive(m->next));
 gtk_entry_set_text(GTK_ENTRY(m->paths[0]),left);gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
 compare(NULL,m);wait_comparison(m);g_assert_cmpuint(m->diff.count,==,2);g_assert_cmpint(selected(m),==,0);
 g_assert_false(gtk_widget_get_sensitive(m->previous));g_assert_true(gtk_widget_get_sensitive(m->next));
 step(m->next,m);g_assert_cmpint(selected(m),==,1);step(m->next,m);g_assert_cmpint(selected(m),==,1);
 g_assert_false(gtk_widget_get_sensitive(m->next));step(m->previous,m);g_assert_cmpint(selected(m),==,0);
 GtkTextIter click;GtkTextBuffer *left_buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[0]));
 gtk_text_buffer_get_iter_at_line(left_buffer,&click,3);GdkRectangle where;gtk_text_view_get_iter_location(GTK_TEXT_VIEW(m->views[0]),&click,&where);
 gint x,y;gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(m->views[0]),GTK_TEXT_WINDOW_TEXT,where.x,where.y+where.height/2,&x,&y);
 GdkEventButton mouse={0};mouse.button=1;mouse.window=gtk_text_view_get_window(GTK_TEXT_VIEW(m->views[0]),GTK_TEXT_WINDOW_TEXT);mouse.x=x;mouse.y=y;
 g_assert_true(pick_block(m->views[0],&mouse,m));g_assert_cmpint(selected(m),==,1);navigate(m,-1);
 GdkEventKey event={0};event.state=GDK_CONTROL_MASK;event.keyval=GDK_KEY_Down;
 g_assert_true(key(m->views[0],&event,m));g_assert_cmpint(selected(m),==,1);
 take(m->take[1],m);g_assert_cmpuint(m->history->len,==,2);undo(NULL,m);assert_text(m,a);redo(NULL,m);
 navigate(m,-1);take(m->take[1],m);assert_text(m,b);undo(NULL,m);undo(NULL,m);assert_text(m,a);
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),1);gtk_entry_set_text(GTK_ENTRY(m->find_entry),"RIGHT");
 GtkTextBuffer *right_buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[1]));GtkTextIter start,end;
 gtk_text_buffer_get_start_iter(right_buffer,&start);gtk_text_buffer_place_cursor(right_buffer,&start);
 find_text(m,FALSE);g_assert_true(gtk_text_buffer_get_selection_bounds(right_buffer,&start,&end));
 gchar *match=gtk_text_buffer_get_text(right_buffer,&start,&end,TRUE);g_assert_cmpstr(match,==,"right");g_free(match);
 g_assert_cmpint(gtk_text_iter_get_line(&start),==,1);find_text(m,TRUE);
 g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->find_status)),"Wrapped"));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_case),TRUE);find_text(m,FALSE);
 g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->find_status)),==,"Text not found.");
 gtk_widget_grab_focus(m->views[2]);event.keyval=GDK_KEY_f;event.state=GDK_CONTROL_MASK;
 g_assert_true(key(m->views[2],&event,m));g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side)),==,2);
 g_assert_true(gtk_widget_has_focus(m->find_entry));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),FALSE);
 for(int i=0;i<3;i++) g_assert_cmpint(gtk_text_view_get_border_window_size(GTK_TEXT_VIEW(m->views[i]),GTK_TEXT_WINDOW_LEFT),==,0);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),TRUE);flush();
 for(int i=0;i<3;i++) g_assert_cmpint(gtk_text_view_get_border_window_size(GTK_TEXT_VIEW(m->views[i]),GTK_TEXT_WINDOW_LEFT),>,0);
 const gchar *capture=g_getenv("INSIGHT_DIFF_CAPTURE");
 if(capture) {
  gtk_entry_set_text(GTK_ENTRY(m->find_entry),"right");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_case),FALSE);
  gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),1);find_text(m,FALSE);flush();
  gint64 drawn=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<drawn) { flush();g_usleep(1000); }
  GdkWindow *window=gtk_widget_get_window(m->window);
  GdkPixbuf *pixels=gdk_pixbuf_get_from_window(window,0,0,gdk_window_get_width(window),gdk_window_get_height(window));g_assert_nonnull(pixels);
  g_assert_true(gdk_pixbuf_save(pixels,capture,"jpeg",&error,"quality","88",NULL));g_assert_no_error(error);g_object_unref(pixels);
 }
 GtkWidget *toggle=gtk_check_button_new();g_object_ref_sink(toggle);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle),TRUE);wrap(GTK_TOGGLE_BUTTON(toggle),m);
 for(int i=0;i<3;i++) g_assert_cmpint(gtk_text_view_get_wrap_mode(GTK_TEXT_VIEW(m->views[i])),==,GTK_WRAP_WORD_CHAR);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle),FALSE);show_list(GTK_TOGGLE_BUTTON(toggle),m);
 g_assert_false(gtk_widget_get_visible(m->list_scroll));navigate(m,1);g_assert_cmpint(selected(m),==,1);
 gtk_tree_selection_unselect_all(m->selection);navigate(m,-1);g_assert_cmpint(selected(m),==,1);
 g_object_unref(toggle);
 gtk_entry_set_text(GTK_ENTRY(m->paths[0]),left);gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
 g_assert_true(g_file_set_contents(left,"a b\r\nя\r\n",-1,&error));g_assert_no_error(error);
 g_assert_true(g_file_set_contents(right,"ab\nя\n",-1,&error));g_assert_no_error(error);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_space),TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->ignore_eol),TRUE);
 compare(NULL,m);wait_comparison(m);g_assert_cmpuint(m->diff.count,==,0);assert_text(m,"a b\r\nя\r\n");
 compare(NULL,m);g_assert_true(m->busy);cancel_compare(NULL,m);wait_comparison(m);
 assert_text(m,"a b\r\nя\r\n");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->status)),"cancel"));
 gtk_entry_set_text(GTK_ENTRY(m->paths[1]),dir);compare(NULL,m);wait_comparison(m);
 assert_text(m,"a b\r\nя\r\n");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->status)),"regular"));
 gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
 GString *large_left=g_string_new(NULL),*large_right=g_string_new(NULL);
 for(int i=0;i<1998;i++) { g_string_append(large_left,"left line\n");g_string_append(large_right,"right line\n"); }
 g_assert_true(g_file_set_contents(left,large_left->str,large_left->len,&error));g_assert_no_error(error);
 g_assert_true(g_file_set_contents(right,large_right->str,large_right->len,&error));g_assert_no_error(error);
 guint beats=0;guint timer=g_timeout_add(1,heartbeat,&beats);compare(NULL,m);wait_comparison(m);g_source_remove(timer);
 g_assert_cmpuint(beats,>,0);g_assert_cmpuint(m->diff.count,==,1);assert_text(m,large_left->str);
 g_string_free(large_left,TRUE);g_string_free(large_right,TRUE);
 gboolean gone=FALSE;g_object_weak_ref(G_OBJECT(m->window),finalized,&gone);
 compare(NULL,m);gtk_widget_destroy(m->window);
 gint64 until=g_get_monotonic_time()+10*G_TIME_SPAN_SECOND;
 while(!gone && g_get_monotonic_time()<until) { flush();g_usleep(1000); }
 g_assert_true(gone);flush();
 g_remove(left);g_remove(right);g_rmdir(dir);g_free(left);g_free(right);g_free(dir);
 g_print("Production Diff/Merge fixture: navigation, display, take and undo/redo passed.\n");return 0;
}
