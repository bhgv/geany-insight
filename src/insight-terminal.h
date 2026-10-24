/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_TERMINAL_H
#define INSIGHT_TERMINAL_H
#include <gtk/gtk.h>
typedef void (*InsightTerminalWrite)(const gchar *,gsize,gpointer);
GtkWidget *insight_terminal_new(InsightTerminalWrite write,gpointer data);
void insight_terminal_feed(GtkWidget *view,const gchar *bytes,gsize length);
void insight_terminal_reset(GtkWidget *view);
void insight_terminal_send(GtkWidget *view,const gchar *text);
gboolean insight_terminal_key(GtkWidget *focus,GdkEventKey *event);
GtkWidget *insight_terminal_tab(guint kind,guint number,const gchar *tooltip);
#endif
