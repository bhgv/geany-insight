/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_BROWSER_MODE_H
#define INSIGHT_BROWSER_MODE_H
#include <gtk/gtk.h>
struct GeanyDocument;
struct GeanyEditor;
void insight_browser_mode_init(GtkWidget *edit_menu);
void insight_browser_mode_toggle(void);
gboolean insight_browser_mode_active(void);
gboolean insight_browser_mode_file_readonly(struct GeanyDocument *);
void insight_browser_mode_reloaded(struct GeanyDocument *);
gboolean insight_browser_mode_key(GtkWidget *,GdkEventKey *);
gboolean insight_browser_mode_mouse(struct GeanyEditor *,GdkEventButton *,gint position);
#endif
