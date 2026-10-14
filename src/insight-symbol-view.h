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
void insight_symbol_view_filter_selected(GtkTreeView *,gboolean);
void insight_symbol_view_more_loader(GtkTreeView *,GCallback,guint);
void insight_symbol_view_page_state(GtkTreeView *,gboolean);
gboolean insight_symbol_view_has_more(GtkTreeView *);
void insight_symbol_view_load_more(GtkTreeView *);
void insight_symbol_view_columns_reset(GtkTreeView *);
void insight_symbol_view_columns_write(GtkTreeView *,GKeyFile *,const gchar *);
void insight_symbol_view_columns_read(GtkTreeView *,GKeyFile *,const gchar *);
#endif
