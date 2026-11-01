/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_VIRTUAL_LIST_H
#define INSIGHT_VIRTUAL_LIST_H
#include "geany.h"
#include <gtk/gtk.h>
/* Worker-only provider: values are initialized; never access GTK here.
 * The data destructor also runs on a worker. Data is immutable; GCancellable must be respected by blocking reads. */
typedef gboolean (*InsightVirtualRead)(guint,GValue *,gpointer,GCancellable *);
typedef void (*InsightVirtualReload)(GtkTreeView *,gpointer);
/* Dataset ownership is independent of the bounded GTK row window. */
GEANY_API_SYMBOL GtkWidget *insight_virtual_list_wrap(GtkWidget *,GtkTreeView *,GtkListStore *,InsightVirtualReload,gpointer);
GEANY_API_SYMBOL gboolean insight_virtual_list_attached(GtkTreeView *);
GEANY_API_SYMBOL void insight_virtual_list_replace(GtkTreeView *,guint,InsightVirtualRead,gpointer,GDestroyNotify);
GEANY_API_SYMBOL void insight_virtual_list_clear(GtkTreeView *);
GEANY_API_SYMBOL void insight_virtual_list_reload(GtkTreeView *);
GEANY_API_SYMBOL void insight_virtual_list_position(GtkTreeView *,guint);
GEANY_API_SYMBOL guint insight_virtual_list_count(GtkTreeView *);
GEANY_API_SYMBOL guint insight_virtual_list_first(GtkTreeView *);
GEANY_API_SYMBOL gboolean insight_virtual_list_loading(GtkTreeView *);
#endif
