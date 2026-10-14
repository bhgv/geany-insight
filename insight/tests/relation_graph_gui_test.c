/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-relation-graph.h"
#include <string.h>
static guint activations;
static gint query_mode=-1;
static void request_query(gint mode,gpointer data) { g_assert_true(data==&query_mode);query_mode=mode; }
static gboolean visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)data;gchar *name;gtk_tree_model_get(model,iter,0,&name,-1);gboolean result=name && strcmp(name,"hidden")!=0;g_free(name);return result;
}
static GtkWidget *find_canvas(GtkWidget *widget) {
 if(!strcmp(gtk_widget_get_name(widget),"insight-relation-graph")) return widget;
 if(!GTK_IS_CONTAINER(widget)) return NULL;
 GList *children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget *found=NULL;
 for(GList *p=children;p && !found;p=p->next) found=find_canvas(p->data);
 g_list_free(children);return found;
}
static void activated(GtkTreeView *tree,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 (void)tree;(void)path;(void)column;(void)data;activations++;
}
static void selected_is(GtkTreeView *tree,const gchar *expected) {
 GtkTreeModel *model;GtkTreeIter iter;g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(tree),&model,&iter));
 gchar *name;gtk_tree_model_get(model,&iter,0,&name,-1);g_assert_cmpstr(name,==,expected);g_free(name);
}
int main(int argc,char **argv) {
 if(!gtk_init_check(&argc,&argv)) return 77;
 GtkListStore *source=gtk_list_store_new(3,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG);GtkTreeIter iter;
 const gchar *names[]={"hidden","zeta","alpha"};
 for(guint i=0;i<3;i++) { gtk_list_store_append(source,&iter);gtk_list_store_set(source,&iter,0,names[i],1,"sample.c",2,(gulong)i+1,-1); }
 GtkTreeModel *filter=gtk_tree_model_filter_new(GTK_TREE_MODEL(source),NULL);gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter),visible,NULL,NULL);
 GtkTreeModel *sort=gtk_tree_model_sort_new_with_model(filter);gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(sort),0,GTK_SORT_ASCENDING);
 GtkWidget *window=gtk_offscreen_window_new(),*box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2),*query=gtk_entry_new(),*tree=gtk_tree_view_new_with_model(sort);
 gtk_container_add(GTK_CONTAINER(window),box);gtk_box_pack_start(GTK_BOX(box),query,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),tree,FALSE,FALSE,0);
 gtk_tree_view_append_column(GTK_TREE_VIEW(tree),gtk_tree_view_column_new_with_attributes("Symbol",gtk_cell_renderer_text_new(),"text",0,NULL));
 GtkWidget *graph=insight_relation_graph_new(GTK_TREE_VIEW(tree),GTK_ENTRY(query),0,1,2);gtk_box_pack_start(GTK_BOX(box),graph,TRUE,TRUE,0);
 gboolean vertical=FALSE;gint zoom=0;insight_relation_graph_set_options(graph,TRUE,500);insight_relation_graph_get_options(graph,&vertical,&zoom);g_assert_true(vertical);g_assert_cmpint(zoom,==,200);insight_relation_graph_set_options(graph,FALSE,100);
 g_signal_connect(tree,"row-activated",G_CALLBACK(activated),NULL);gtk_widget_show_all(window);
 for(guint i=0;i<100 && gtk_events_pending();i++) gtk_main_iteration();
 insight_relation_graph_set_query_handler(graph,request_query,&query_mode);
 GtkWidget *canvas=find_canvas(graph);g_assert_nonnull(canvas);gboolean handled=FALSE;
 GdkEventButton button={0};button.type=GDK_BUTTON_PRESS;button.button=1;button.x=268;button.y=28;
 g_signal_emit_by_name(canvas,"button-press-event",&button,&handled);g_assert_true(handled);selected_is(GTK_TREE_VIEW(tree),"alpha");
 button.type=GDK_2BUTTON_PRESS;button.y=92;g_signal_emit_by_name(canvas,"button-press-event",&button,&handled);selected_is(GTK_TREE_VIEW(tree),"zeta");g_assert_cmpuint(activations,==,1);
 GdkEventKey key={0};key.type=GDK_KEY_PRESS;key.keyval=GDK_KEY_Home;g_signal_emit_by_name(canvas,"key-press-event",&key,&handled);selected_is(GTK_TREE_VIEW(tree),"alpha");
 key.keyval=GDK_KEY_Return;g_signal_emit_by_name(canvas,"key-press-event",&key,&handled);g_assert_cmpuint(activations,==,2);
 key.keyval=GDK_KEY_d;g_signal_emit_by_name(canvas,"key-press-event",&key,&handled);g_assert_true(handled);g_assert_cmpint(query_mode,==,1);
 key.keyval=GDK_KEY_c;g_signal_emit_by_name(canvas,"key-press-event",&key,&handled);g_assert_cmpint(query_mode,==,7);
 key.keyval=GDK_KEY_r;g_signal_emit_by_name(canvas,"key-press-event",&key,&handled);g_assert_cmpint(query_mode,==,0);
 insight_relation_graph_set_query_handler(graph,NULL,NULL);key.keyval=GDK_KEY_d;g_signal_emit_by_name(canvas,"key-press-event",&key,&handled);g_assert_false(handled);
 gtk_list_store_clear(source);gtk_entry_set_text(GTK_ENTRY(query),"changed");
 gtk_widget_destroy(graph);gtk_entry_set_text(GTK_ENTRY(query),"after destruction");gtk_list_store_append(source,&iter);gtk_list_store_set(source,&iter,0,"late",1,"late.c",2,(gulong)1,-1);
 gtk_widget_destroy(window);g_object_unref(sort);g_object_unref(filter);g_object_unref(source);
 g_print("Graph GUI: filtered/sorted clicks, keyboard, activation and destruction passed\n");return 0;
}
