/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SIDE_TABS_H
#define INSIGHT_SIDE_TABS_H
#include <gtk/gtk.h>
GtkWidget *insight_side_tabs_wrap(GtkNotebook *book,const gchar *(*translate)(const gchar *));
void insight_side_tabs_refresh(gpointer wrapper);
#endif
