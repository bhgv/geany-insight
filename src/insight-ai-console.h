/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_AI_CONSOLE_H
#define INSIGHT_AI_CONSOLE_H
#include <gtk/gtk.h>
GtkWidget *insight_ai_console_new(void);
gchar *insight_ai_console_prompt(GtkWidget *);
void insight_ai_console_set_prompt(GtkWidget *,const gchar *);
void insight_ai_console_transcript(GtkWidget *,const gchar *);
#endif
