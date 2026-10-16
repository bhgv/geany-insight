/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/completion_preferences.h"
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !gtk_init_check(&argc,&argv)) return 77;
 InsightCompletionSettings p,q;insight_completion_settings_default(&p);p.case_sensitive=FALSE;p.min_chars=3;p.max_rows=12;p.insert_parentheses=TRUE;p.space_after_name=TRUE;GtkWidget *window=gtk_window_new(GTK_WINDOW_TOPLEVEL),*page=insight_completion_preferences_new(&p);gtk_window_set_default_size(GTK_WINDOW(window),560,365);gtk_container_add(GTK_CONTAINER(window),page);gtk_widget_show_all(window);insight_completion_preferences_get(page,&q);g_assert_cmpint(q.min_chars,==,3);g_assert_false(q.case_sensitive);g_assert_cmpint(q.max_rows,==,12);g_assert_true(q.accept_tab);g_assert_true(q.insert_parentheses);g_assert_true(q.space_after_name);g_assert_false(q.inside_comments);
 const gchar *shot=g_getenv("GEANY_GUI_SCREENSHOT");if(shot) { gint64 end=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<end) { while(g_main_context_iteration(NULL,FALSE));g_usleep(1000); }GdkWindow *native=gtk_widget_get_window(window);GdkPixbuf *image=gdk_pixbuf_get_from_window(native,0,0,gdk_window_get_width(native),gdk_window_get_height(native));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,shot,"jpeg",NULL,"quality","82",NULL));g_object_unref(image); }
 gtk_widget_destroy(window);g_print("Production completion preferences page roundtrip passed\n");return 0;
}
