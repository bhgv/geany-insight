/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SYMBOL_VIEW_H
#define INSIGHT_SYMBOL_VIEW_H
#include <gtk/gtk.h>
#include "filetypes.h"
GtkWidget *insight_symbol_view_attach(GtkTreeView *,GtkListStore *,GeanyFiletype *);
void insight_symbol_view_filetype(GtkTreeView *,GeanyFiletype *);
gboolean insight_symbol_view_grouped(GtkTreeView *);
void insight_symbol_view_set_grouped(GtkTreeView *,gboolean);
void insight_symbol_views_locale_changed(void);
GtkWidget *insight_symbol_view_types_control(GtkTreeView *);
guint insight_symbol_view_types_mask(GtkTreeView *);
void insight_symbol_view_set_types(GtkTreeView *,guint);
void insight_symbol_view_loading(GtkTreeView *,gboolean);
#endif
