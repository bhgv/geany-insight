/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-virtual-list.h"
static guint filled,freed_count;
static void fill(GtkListStore *store,guint n,gpointer data) {
 (void)data;GtkTreeIter iter;gchar *name=g_strdup_printf("file-%07u.c",n);filled++;
 gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,name,1,n,-1);g_free(name);
}
static void release(gpointer data) { (void)data;freed_count++; }
static void drain(void) { while(gtk_events_pending()) gtk_main_iteration(); }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !gtk_init_check(&argc,&argv)) return 77;
 g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 GtkWidget *window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_default_size(GTK_WINDOW(window),640,480);
 GtkListStore *store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_UINT);
 GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
 gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Name",gtk_cell_renderer_text_new(),"text",0,NULL));
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view);
 GtkWidget *control=insight_virtual_list_wrap(scroll,GTK_TREE_VIEW(view),store,NULL,NULL);gtk_container_add(GTK_CONTAINER(window),control);
 gtk_widget_show_all(window);drain();
 insight_virtual_list_replace(GTK_TREE_VIEW(view),1000000,fill,NULL,release);drain();
 g_assert_cmpuint(insight_virtual_list_count(GTK_TREE_VIEW(view)),==,1000000);
 g_assert_cmpuint(filled,<=,256);
 g_assert_cmpint(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),NULL),<=,256);
 insight_virtual_list_position(GTK_TREE_VIEW(view),900000);drain();
 g_assert_cmpuint(insight_virtual_list_first(GTK_TREE_VIEW(view)),==,900000);
 GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&iter));guint row=0;gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,1,&row,-1);g_assert_cmpuint(row,==,900000);
 GdkEventScroll event={0};event.type=GDK_SCROLL;event.direction=GDK_SCROLL_UP;gboolean handled=FALSE;
 g_signal_emit_by_name(view,"scroll-event",&event,&handled);g_assert_true(handled);drain();
 g_assert_cmpuint(insight_virtual_list_first(GTK_TREE_VIEW(view)),==,899997);
 insight_virtual_list_position(GTK_TREE_VIEW(view),1000000);drain();
 g_assert_cmpuint(insight_virtual_list_first(GTK_TREE_VIEW(view)),>,999800);
 g_assert_cmpint(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),NULL),<=,256);
 insight_virtual_list_replace(GTK_TREE_VIEW(view),3,fill,NULL,release);drain();
 g_assert_cmpuint(freed_count,==,1);g_assert_cmpuint(insight_virtual_list_first(GTK_TREE_VIEW(view)),==,0);
 g_assert_cmpint(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),NULL),==,3);
 gtk_widget_destroy(window);g_object_unref(store);drain();g_assert_cmpuint(freed_count,==,2);
 g_print("Virtual list: million-row provider, bounded 256 GTK rows, global jump, reverse scroll, shrink and snapshot ownership passed\n");return 0;
}
