/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_DISK_BROWSER_H
#define INSIGHT_DISK_BROWSER_H
#include <gtk/gtk.h>
/* Paths are UTF-8. Preview/open callbacks never change project membership. */
typedef void (*InsightDiskOpen)(const gchar *,gboolean,gpointer);
GtkWidget *insight_disk_browser_new(InsightDiskOpen,gpointer);
void insight_disk_browser_set_root(GtkWidget *,const gchar *);
void insight_disk_browser_cancel(GtkWidget *);
void insight_disk_browser_locale_changed(GtkWidget *);
#endif
