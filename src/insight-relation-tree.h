/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RELATION_TREE_H
#define INSIGHT_RELATION_TREE_H
#include <gtk/gtk.h>
#include "filetypes.h"
#include "../insight/core/index.h"
#include "../insight/desktop/relation_query.h"
/* First ten model columns match the workbench occurrence model. */
GtkWidget *insight_relation_tree_new(void);
GtkTreeView *insight_relation_tree_view(GtkWidget *);
void insight_relation_tree_query(GtkWidget *,InsightIndex *,const gchar *,InsightRelationMode,const gchar *,GeanyFiletype *);
/* Compiler call occurrences: exact USR branches, immutable project snapshot. */
void insight_relation_tree_calls(GtkWidget *,InsightIndex *,const gchar *,const gchar *,gboolean);
void insight_relation_tree_locale_changed(GtkWidget *);
void insight_relation_tree_cancel(GtkWidget *);
/* Removes one source branch from the display; never the query root/notices. */
gboolean insight_relation_tree_remove_selected(GtkWidget *);
void insight_relation_tree_filter(GtkWidget *,const gchar *);
#endif
