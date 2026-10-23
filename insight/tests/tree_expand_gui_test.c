/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-tree-expand.h"
static void add(GtkTreeStore *store,GtkTreeIter *row,GtkTreeIter *parent)
{ gtk_tree_store_append(store,row,parent);gtk_tree_store_set(store,row,0,"node",-1); }
static void replaced(GtkTreeView *view,GtkTreeIter *iter,GtkTreePath *path,gpointer data)
{ (void)iter;(void)path;gtk_tree_view_set_model(view,GTK_TREE_MODEL(data)); }
int main(int argc,char **argv)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !gtk_init_check(&argc,&argv)) return 77;
 g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 GtkTreeStore *store=gtk_tree_store_new(1,G_TYPE_STRING);GtkTreeIter root,child,leaf,other,other_child;
 add(store,&root,NULL);add(store,&child,&root);add(store,&leaf,&child);add(store,&other,NULL);add(store,&other_child,&other);
 GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));g_object_ref_sink(view);
 GtkTreePath *r=gtk_tree_path_new_from_string("0"),*c=gtk_tree_path_new_from_string("0:0"),*o=gtk_tree_path_new_from_string("1");
 g_assert_true(insight_tree_expand_levels(GTK_TREE_VIEW(view),r,1));
 g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),r));g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),c));g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),o));
 g_assert_true(insight_tree_expand_levels(GTK_TREE_VIEW(view),r,2));g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),c));g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),o));
 g_assert_false(insight_tree_expand_levels(GTK_TREE_VIEW(view),r,0));g_assert_false(insight_tree_expand_levels(GTK_TREE_VIEW(view),r,33));
 /* Synchronous expansion callbacks may replace the entire project model. */
 gtk_tree_view_collapse_all(GTK_TREE_VIEW(view));GtkTreeStore *replacement=gtk_tree_store_new(1,G_TYPE_STRING);
 gulong signal=g_signal_connect(view,"row-expanded",G_CALLBACK(replaced),replacement);
 g_assert_false(insight_tree_expand_levels(GTK_TREE_VIEW(view),r,2));g_signal_handler_disconnect(view,signal);
 gtk_tree_view_set_model(GTK_TREE_VIEW(view),GTK_TREE_MODEL(store));
 /* A wide tree is bounded without expanding unrelated roots. */
 for(guint i=0;i<4200;i++) { GtkTreeIter row;add(store,&row,&root); }
 g_assert_false(insight_tree_expand_levels(GTK_TREE_VIEW(view),r,2));
 g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),o));
 gtk_tree_path_free(r);gtk_tree_path_free(c);gtk_tree_path_free(o);gtk_widget_destroy(view);g_object_unref(view);g_object_unref(store);g_object_unref(replacement);
 g_print("Selected subtree: exact levels, sibling isolation, invalid depths, model replacement and work limit passed\n");return 0;
}
