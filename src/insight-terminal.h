/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_TERMINAL_H
#define INSIGHT_TERMINAL_H
#include <gtk/gtk.h>
typedef void (*InsightTerminalWrite)(const gchar *,gsize,gpointer);
typedef void (*InsightTerminalResize)(gint rows,gint columns,gpointer);
void insight_terminal_settings_path(const gchar *path);
void insight_terminal_set_resize(GtkWidget *view,InsightTerminalResize resize,gpointer data);
void insight_terminal_size(GtkWidget *view,gint *rows,gint *columns);
GtkWidget *insight_terminal_new(InsightTerminalWrite write,gpointer data);
void insight_terminal_feed(GtkWidget *view,const gchar *bytes,gsize length);
void insight_terminal_reset(GtkWidget *view);
void insight_terminal_send(GtkWidget *view,const gchar *text);
gboolean insight_terminal_key(GtkWidget *focus,GdkEventKey *event);
GtkWidget *insight_terminal_tab(guint kind,guint number,const gchar *tooltip);
#endif
