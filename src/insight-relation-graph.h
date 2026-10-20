/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RELATION_GRAPH_H
#define INSIGHT_RELATION_GRAPH_H
#include <gtk/gtk.h>
/* Shares the tree's filtered/sorted model and selection. Double click activates
 * the tree row. All callbacks and model references are released on destruction. */
GtkWidget *insight_relation_graph_new(GtkTreeView *,GtkEntry *,gint,gint,gint);
/* Hierarchical model: row 0 is the virtual query root; visible source nodes
 * are collected below expanded ancestors. Shares lazy expansion with tree. */
GtkWidget *insight_relation_graph_new_tree(GtkTreeView *,GtkEntry *,gint,gint,gint);
void insight_relation_graph_get_options(GtkWidget *,gboolean *,gint *);
void insight_relation_graph_set_options(GtkWidget *,gboolean,gint);
/* Main-window shortcut dispatch: only handles a focused graph canvas. */
gboolean insight_relation_graph_handle_key(GtkWidget *,GdkEventKey *);
typedef void (*InsightRelationGraphQuery)(gint,gpointer);
void insight_relation_graph_set_query_handler(GtkWidget *,InsightRelationGraphQuery,gpointer);
#endif
