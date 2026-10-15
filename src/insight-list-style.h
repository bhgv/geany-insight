/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_LIST_STYLE_H
#define INSIGHT_LIST_STYLE_H
#include <gtk/gtk.h>
/* Per-tree-view fonts/colors, theme defaults until explicitly enabled. The view owns state;
 * the returned button follows its lifetime. Dialog Cancel leaves it untouched. */
GtkWidget *insight_list_style_button(GtkWidget *);
void insight_list_style_read(GtkWidget *,GKeyFile *,const gchar *);
void insight_list_style_write(GtkWidget *,GKeyFile *,const gchar *);
#endif
