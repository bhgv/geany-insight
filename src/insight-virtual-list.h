/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_VIRTUAL_LIST_H
#define INSIGHT_VIRTUAL_LIST_H
#include "geany.h"
#include <gtk/gtk.h>
typedef void (*InsightVirtualFill)(GtkListStore *,guint,gpointer);
typedef void (*InsightVirtualReload)(GtkTreeView *,gpointer);
/* Dataset ownership is independent of the bounded GTK row window. */
GEANY_API_SYMBOL GtkWidget *insight_virtual_list_wrap(GtkWidget *,GtkTreeView *,GtkListStore *,InsightVirtualReload,gpointer);
GEANY_API_SYMBOL gboolean insight_virtual_list_attached(GtkTreeView *);
GEANY_API_SYMBOL void insight_virtual_list_replace(GtkTreeView *,guint,InsightVirtualFill,gpointer,GDestroyNotify);
GEANY_API_SYMBOL void insight_virtual_list_clear(GtkTreeView *);
GEANY_API_SYMBOL void insight_virtual_list_reload(GtkTreeView *);
GEANY_API_SYMBOL void insight_virtual_list_position(GtkTreeView *,guint);
GEANY_API_SYMBOL guint insight_virtual_list_count(GtkTreeView *);
GEANY_API_SYMBOL guint insight_virtual_list_first(GtkTreeView *);
#endif
