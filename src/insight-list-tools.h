/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_LIST_TOOLS_H
#define INSIGHT_LIST_TOOLS_H
#include <gtk/gtk.h>
void insight_list_tools_attach(GtkTreeView *);
void insight_list_tools_menu(GtkTreeView *, GtkWidget *);
/* For lists without an existing context menu. Right click preserves selection. */
void insight_list_tools_popup_attach(GtkTreeView *);
gboolean insight_list_tools_key(GtkWidget *, GdkEventKey *);
#endif
