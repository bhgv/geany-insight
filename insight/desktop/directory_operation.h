/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_DIRECTORY_OPERATION_H
#define INSIGHT_DIRECTORY_OPERATION_H
#include <gio/gio.h>
enum { INSIGHT_DIR_COPY_LR,INSIGHT_DIR_COPY_RL,INSIGHT_DIR_TRASH_L,INSIGHT_DIR_TRASH_R };
/* Worker-only, regular files only. Roots/relative/info belong to the completed
 * comparison. No recursive writes, parent creation or permanent-delete fallback.
 * Copy is bounded to 64 MiB and writes exact bytes, not normalized comparison text.
 * Existing targets require an unchanged identity and conditional etag replacement.
 * GIO checks are not a filesystem lock; callers must require a fresh scan after
 * every attempt, including cancellation or error, and protect open documents. */
gboolean insight_directory_operation(const gchar *const roots[2],const gchar *relative,
 GFileInfo *const info[2],guint action,GCancellable *,GError **);
#endif
