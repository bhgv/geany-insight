/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SCROLL_LINKS_H
#define INSIGHT_SCROLL_LINKS_H
#include <gtk/gtk.h>
#include "../insight/desktop/project_files.h"
struct GeanyEditor;
struct SCNotification;
void insight_scroll_links_add_popup(GtkWidget *menu,guint document_id);
void insight_scroll_links_membership(const InsightProjectFiles *policy,const gchar *root);
void insight_scroll_links_init(GtkWidget *window_menu);
void insight_scroll_links_command(guint action);
void insight_scroll_links_notify(struct GeanyEditor *,struct SCNotification *);
#endif
