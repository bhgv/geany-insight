/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RELATION_GRAPH_H
#define INSIGHT_RELATION_GRAPH_H
#include <gtk/gtk.h>
/* Shares the tree's filtered/sorted model and selection. Double click activates
 * the tree row. All callbacks and model references are released on destruction. */
GtkWidget *insight_relation_graph_new(GtkTreeView *,GtkEntry *,gint,gint,gint);
#endif
