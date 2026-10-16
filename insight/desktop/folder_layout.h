/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_FOLDER_LAYOUT_H
#define INSIGHT_FOLDER_LAYOUT_H
#include <glib.h>
/* Shape switching has a 24px hysteresis and keeps narrow panes stacked. */
gboolean insight_folder_layout_vertical(gboolean adaptive,gboolean manual_vertical,
 gint width,gint height,gboolean current_vertical);
/* Bounded lexical ancestors, oldest first, including path. No disk I/O.
 * At most 64 paths; when truncated the first path is an intermediate ancestor. */
GPtrArray *insight_folder_ancestry(const gchar *path,guint limit);
#endif
