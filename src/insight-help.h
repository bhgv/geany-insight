/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_HELP_H
#define INSIGHT_HELP_H
#include <gtk/gtk.h>
/* Shared icon identity for menus and toolbar commands. */
GtkWidget *insight_command_icon(const gchar *command);
GtkWidget *insight_panel_help_button(const gchar *);
static inline GtkWidget *insight_panel_help_overlay(GtkWidget *content,const gchar *topic) {
 GtkWidget *overlay=gtk_overlay_new(),*button=insight_panel_help_button(topic);gtk_container_add(GTK_CONTAINER(overlay),content);gtk_widget_set_opacity(button,0.8);gtk_widget_set_halign(button,GTK_ALIGN_END);gtk_widget_set_valign(button,GTK_ALIGN_START);gtk_overlay_add_overlay(GTK_OVERLAY(overlay),button);return overlay;
}
void insight_panel_help_attach(GtkWidget *,const gchar *);
void insight_panel_help_notebook(GtkNotebook *,const gchar *);
void insight_help_locale_changed(void);
void insight_help_show(GtkWidget *widget,gpointer topic);
void insight_markup_open_path(const gchar *path);
void insight_markup_preview(GtkWidget *widget,gpointer data);
void insight_markup_open(GtkWidget *widget,gpointer data);
void insight_menu_icons(GtkWidget *menu);
#endif
