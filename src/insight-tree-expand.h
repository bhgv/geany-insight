/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_TREE_EXPAND_H
#define INSIGHT_TREE_EXPAND_H
#include <gtk/gtk.h>
/* Expand only this materialized subtree; never enumerate files or run a main loop.
 * Returns FALSE if the row/model disappeared or the work/depth limit was reached. */
gboolean insight_tree_expand_levels(GtkTreeView *view, GtkTreePath *path, guint levels);
#endif
