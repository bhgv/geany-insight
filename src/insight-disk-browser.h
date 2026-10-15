/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_DISK_BROWSER_H
#define INSIGHT_DISK_BROWSER_H
#include <gtk/gtk.h>
/* Paths are UTF-8. Preview/open callbacks never change project membership. */
typedef void (*InsightDiskOpen)(const gchar *,gboolean,gpointer);
GtkWidget *insight_disk_browser_new(InsightDiskOpen,gpointer);
void insight_disk_browser_set_root(GtkWidget *,const gchar *);
void insight_disk_browser_resume(GtkWidget *,const gchar *);
/* Remember the editor file; follow it only while disk mode is active and its
 * persisted Follow option is enabled. Selection never invokes open callbacks. */
void insight_disk_browser_current_file(GtkWidget *,const gchar *,gboolean);
gboolean insight_disk_browser_read(GtkWidget *,GKeyFile *);
void insight_disk_browser_write(GtkWidget *,GKeyFile *);
void insight_disk_browser_cancel(GtkWidget *);
/* Stop callbacks and outstanding reads/writes before workbench teardown. */
void insight_disk_browser_shutdown(GtkWidget *);
void insight_disk_browser_locale_changed(GtkWidget *);
#endif
