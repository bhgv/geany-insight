/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-virtual-list.h"
static GThread *gui_thread;static gint reads,freed_count;static guint pulses;static GtkWidget *view;
typedef struct {const gchar *prefix;guint delay;} Fixture;
static gboolean read_row(guint n,GValue *values,gpointer data,GCancellable *cancel) {
 Fixture *fixture=data;g_assert_true(g_thread_self()!=gui_thread);
 if(g_cancellable_is_cancelled(cancel))return FALSE;
 g_atomic_int_inc(&reads);if(fixture->delay)g_usleep(fixture->delay);
 gchar *name=g_strdup_printf("%s-%07u.c",fixture->prefix,n);g_value_set_string(&values[0],name);g_value_set_uint(&values[1],n);g_free(name);return TRUE;
}
static void release(gpointer data) {g_assert_true(g_thread_self()!=gui_thread);g_free(data);g_atomic_int_inc(&freed_count);}
static Fixture *fixture(const gchar *prefix,guint delay) {Fixture *f=g_new(Fixture,1);*f=(Fixture){prefix,delay};return f;}
static void spin(guint milliseconds) {
 gint64 end=g_get_monotonic_time()+(gint64)milliseconds*1000;
 do {while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}while(g_get_monotonic_time()<end);
}
static void ready(void) {
 gint64 end=g_get_monotonic_time()+10000000;
 do {spin(2);g_assert_true(gtk_widget_get_sensitive(view));}while(insight_virtual_list_loading(GTK_TREE_VIEW(view))&&g_get_monotonic_time()<end);
 g_assert_false(insight_virtual_list_loading(GTK_TREE_VIEW(view)));
 g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),<=,256);
 g_assert_cmpuint(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(view),"insight-virtual-cache-rows")),<=,3072);
}
static gboolean heartbeat(gpointer data) {(void)data;pulses++;return G_SOURCE_CONTINUE;}
static guint first_row(GtkListStore *store) {GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&iter));guint n=0;gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,1,&n,-1);return n;}
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS")||!gtk_init_check(&argc,&argv))return 77;
 gui_thread=g_thread_self();g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 GtkWidget *window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_default_size(GTK_WINDOW(window),640,480);
 GtkListStore *store=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_UINT);view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
 gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Name",gtk_cell_renderer_text_new(),"text",0,NULL));
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_container_add(GTK_CONTAINER(window),insight_virtual_list_wrap(scroll,GTK_TREE_VIEW(view),store,NULL,NULL));gtk_widget_show_all(window);spin(50);
 guint timer=g_timeout_add(5,heartbeat,NULL),before=pulses;
 insight_virtual_list_replace(GTK_TREE_VIEW(view),1000000,read_row,fixture("file",500),release);
 spin(100);g_assert_true(insight_virtual_list_loading(GTK_TREE_VIEW(view)));g_assert_true(gtk_widget_get_sensitive(view));g_assert_cmpuint(pulses-before,>=,5);ready();
 g_assert_cmpuint(insight_virtual_list_count(GTK_TREE_VIEW(view)),==,1000000);g_assert_cmpuint(first_row(store),==,0);
 gint cached_reads=g_atomic_int_get(&reads);
 for(guint n=0;n<4;n++){insight_virtual_list_position(GTK_TREE_VIEW(view),100);ready();g_assert_cmpuint(first_row(store),==,100);insight_virtual_list_position(GTK_TREE_VIEW(view),0);ready();g_assert_cmpuint(first_row(store),==,0);}
 g_assert_cmpint(g_atomic_int_get(&reads),==,cached_reads);
 GtkTreePath *path=gtk_tree_path_new_from_indices(5,-1);GtkTreeRowReference *retained=gtk_tree_row_reference_new(GTK_TREE_MODEL(store),path);gtk_tree_path_free(path);
 insight_virtual_list_replace(GTK_TREE_VIEW(view),1000000,read_row,fixture("file",0),release);ready();g_assert_true(gtk_tree_row_reference_valid(retained));gtk_tree_row_reference_free(retained);
 insight_virtual_list_position(GTK_TREE_VIEW(view),900000);ready();g_assert_cmpuint(first_row(store),==,900000);cached_reads=g_atomic_int_get(&reads);
 insight_virtual_list_position(GTK_TREE_VIEW(view),899900);ready();g_assert_cmpuint(first_row(store),==,899900);
 insight_virtual_list_position(GTK_TREE_VIEW(view),900000);ready();g_assert_cmpint(g_atomic_int_get(&reads),==,cached_reads);
 /* Keep three recently used ranges; returning to an earlier range is a hit. */
 insight_virtual_list_position(GTK_TREE_VIEW(view),500000);ready();cached_reads=g_atomic_int_get(&reads);insight_virtual_list_position(GTK_TREE_VIEW(view),0);ready();g_assert_cmpuint(first_row(store),==,0);g_assert_cmpint(g_atomic_int_get(&reads),==,cached_reads);
 /* A slow old page cannot replace a newer dataset or position. */
 insight_virtual_list_replace(GTK_TREE_VIEW(view),1000000,read_row,fixture("stale",2000),release);spin(60);
 insight_virtual_list_position(GTK_TREE_VIEW(view),700000);spin(30);
 insight_virtual_list_replace(GTK_TREE_VIEW(view),1000000,read_row,fixture("current",0),release);insight_virtual_list_position(GTK_TREE_VIEW(view),42);ready();g_assert_cmpuint(first_row(store),==,42);
 GtkTreeIter iter;gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&iter);gchar *name=NULL;gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,0,&name,-1);g_assert_true(g_str_has_prefix(name,"current-"));g_free(name);
 insight_virtual_list_position(GTK_TREE_VIEW(view),1000000);ready();g_assert_cmpuint(insight_virtual_list_first(GTK_TREE_VIEW(view)),>,999800);
 insight_virtual_list_replace(GTK_TREE_VIEW(view),3,read_row,fixture("small",0),release);ready();g_assert_cmpuint(first_row(store),==,0);g_assert_cmpint(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),NULL),==,3);
 insight_virtual_list_replace(GTK_TREE_VIEW(view),1000000,read_row,fixture("closing",2000),release);spin(30);
 gtk_widget_destroy(window);g_object_unref(store);g_source_remove(timer);
 gint64 deadline=g_get_monotonic_time()+5000000;
 while(g_atomic_int_get(&freed_count)<6&&g_get_monotonic_time()<deadline)spin(5);
 g_assert_cmpint(g_atomic_int_get(&freed_count),==,6);
 g_print("Virtual list: worker-only reads and cleanup, responsive heartbeat, bounded million-row view, cached +/-100 reversal, LRU ranges, stale cancellation, row identity and close while loading passed\n");return 0;
}
