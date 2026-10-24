/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_STREAM_UI_H
#define INSIGHT_STREAM_UI_H
#include <gtk/gtk.h>
void insight_stream_ui_set_colorizer(void (*callback)(GtkTextBuffer *,const gchar *));
gboolean insight_stream_ui_large(const gchar *left,const gchar *right,guint64 threshold);
void insight_stream_ui_show(GtkWidget *parent,const gchar *left,const gchar *right,gboolean text);
#endif
