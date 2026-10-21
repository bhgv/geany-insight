/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SNAPSHOT_COMPARE_H
#define INSIGHT_SNAPSHOT_COMPARE_H
#include <gtk/gtk.h>
#define INSIGHT_SNAPSHOT_COMPARE_LIMIT (1024u*1024u)
/* Copies decoded UTF-8/no-NUL snapshots. No source paths are reopened or written.
 * Returns FALSE before creating a window on invalid/oversized input. */
gboolean insight_snapshot_compare_show(GtkWidget *parent,const gchar *left_name,const gchar *left,gsize left_size,const gchar *right_name,const gchar *right,gsize right_size);
void insight_snapshot_compare_init(void);
void insight_snapshot_compare_command(guint action);
#endif
