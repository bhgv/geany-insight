/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_AI_H
#define INSIGHT_AI_H
#include <gtk/gtk.h>
typedef gboolean (*InsightAiInsert)(const gchar *,gpointer);
void insight_ai_init(void);
void insight_ai_window_modes(void);
GtkWidget *insight_ai_terminal_open(const gchar *,const gchar *,GtkWidget *,InsightAiInsert,gpointer);
GtkWidget *insight_ai_embed(GtkWidget *,const gchar *,const gchar *,GtkWidget *,InsightAiInsert,gpointer);
gboolean insight_ai_context_update(GtkWidget *,const gchar *);
gchar *insight_ai_suggested_command(GtkWidget *);
void insight_ai_lookup(const gchar *,const gchar *,const gchar *);
GtkWidget *insight_ai_open(const gchar *,const gchar *,GtkWidget *,InsightAiInsert,gpointer);
#endif
