/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_TERMINAL_UI_H
#define INSIGHT_TERMINAL_UI_H
#include <gtk/gtk.h>
#include "insight-ai.h"
void insight_terminal_ui_status(GtkWidget *page,GtkWidget *label);
void insight_terminal_ui_add(GtkWidget *page,const gchar *label);
void insight_terminal_ui_reveal(GtkWidget *page);
GtkWidget *insight_terminal_ui_overlay(GtkWidget *content,const gchar *name,GCallback callback,gpointer data);
GtkWidget *insight_terminal_ui_assistant(const gchar *scope,const gchar *label,const gchar *context,GtkWidget *owner,InsightAiInsert insert,gpointer data);
#endif
