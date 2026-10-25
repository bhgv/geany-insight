/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_APPEARANCE_H
#define INSIGHT_APPEARANCE_H
#include <gtk/gtk.h>
void insight_appearance_init(void);
void insight_appearance_menu(GtkWidget *menu);
void insight_appearance_finalize(void);
gboolean insight_appearance_independent(void);
void insight_appearance_join_windows(void);
void insight_appearance_panel_opacity(GtkWidget *window,gint percent);
gboolean insight_appearance_panel_opacity_available(void);
gboolean insight_appearance_opacity_tick(gboolean finish);
void insight_appearance_decorate(GtkWidget *window);
#endif
