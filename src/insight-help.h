/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_HELP_H
#define INSIGHT_HELP_H
#include <gtk/gtk.h>
/* Shared icon identity for menus and toolbar commands. */
GtkWidget *insight_command_icon(const gchar *command);
void insight_help_locale_changed(void);
void insight_help_show(GtkWidget *widget,gpointer topic);
void insight_markup_open_path(const gchar *path);
void insight_markup_preview(GtkWidget *widget,gpointer data);
void insight_markup_open(GtkWidget *widget,gpointer data);
void insight_menu_icons(GtkWidget *menu);
#endif
