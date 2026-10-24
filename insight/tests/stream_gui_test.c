/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-stream-ui.c"
const gchar *insight_tr(const gchar *s) { return s; }
void insight_i18n_apply(GtkWidget *w) { (void)w; }
int main(int argc,char **argv) {
 if(!g_getenv("DISPLAY")) return 77;gtk_init(&argc,&argv);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 gchar *directory=g_dir_make_tmp("insight-stream-gui-XXXXXX",NULL),*a=g_build_filename(directory,"left",NULL),*b=g_build_filename(directory,"right",NULL);g_assert_true(g_file_set_contents(a,"one\ntwo\nthree\n",-1,NULL));g_assert_true(g_file_set_contents(b,"one\nTWO\nthree\n",-1,NULL));
 for(unsigned mode=0;mode<2;mode++) { insight_stream_ui_show(NULL,a,b,mode);GList *windows=gtk_window_list_toplevels();StreamUI *s=NULL;for(GList *p=windows;p;p=p->next) { s=g_object_get_data(G_OBJECT(p->data),"insight-stream-state");if(s) break; }g_list_free(windows);g_assert_nonnull(s);while(s->scan_idle) g_main_context_iteration(NULL,TRUE);
 GtkWidget *button=gtk_button_new();g_object_ref_sink(button);stream_difference(button,s);g_object_set_data(G_OBJECT(button),"side",GUINT_TO_POINTER(1));stream_take(button,s);g_assert_cmpuint(stream_history_count(s),==,2);gchar result[15]={0};g_assert_true(insight_stream_read(s->stream,2,0,result,14));g_assert_cmpstr(result,==,"one\nTWO\nthree\n");
 g_assert_true(gtk_widget_get_sensitive(s->history->undo));gtk_button_clicked(GTK_BUTTON(s->history->undo));g_assert_cmpuint(stream_history_position(s),==,0);gtk_button_clicked(GTK_BUTTON(s->history->redo));g_assert_cmpuint(stream_history_position(s),==,1);
 insight_history_show(s->history->menu,s->history);GList *items=gtk_container_get_children(GTK_CONTAINER(s->history->menu));g_assert_cmpuint(g_list_length(items),==,2);gtk_menu_item_activate(GTK_MENU_ITEM(items->data));g_assert_cmpuint(stream_history_position(s),==,0);g_list_free(items);
 gchar *before=NULL,*after=NULL;stream_preview(s,1,&before,&after);g_assert_nonnull(before);g_assert_nonnull(after);g_assert_cmpstr(before,!=,after);g_free(before);g_free(after);g_assert_cmpuint(stream_history_position(s),==,0);
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->edit)),mode ? "manual":"4d 41 4e",-1);stream_edit(button,s);g_assert_cmpuint(stream_history_count(s),==,2);g_assert_cmpuint(stream_history_position(s),==,1);g_assert_false(gtk_widget_get_sensitive(s->history->redo));s->dirty=FALSE;gtk_widget_destroy(button);g_object_unref(button);gtk_widget_destroy(s->window); }
 g_remove(a);g_remove(b);g_rmdir(directory);g_free(a);g_free(b);g_free(directory);g_print("Streaming GTK: text/hex views, selective merge, permanent arrow controls, direct history jumps, before/after previews and redo branching passed\n");return 0;
}
