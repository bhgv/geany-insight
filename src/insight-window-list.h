/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_WINDOW_LIST_H
#define GEANY_INSIGHT_WINDOW_LIST_H
#include <gtk/gtk.h>
typedef void (*InsightWindowPreview)(guint);
void insight_window_list_init(InsightWindowPreview);
void insight_window_list_show(GtkWidget *,gpointer);
void insight_window_list_document_event(guint,const gchar *);
void insight_window_list_editor_updated(void);
void insight_window_list_locale_changed(void);
void insight_window_list_finalize(void);
#endif
