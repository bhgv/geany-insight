/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_DISK_OPERATION_H
#define INSIGHT_DISK_OPERATION_H
#include <gio/gio.h>
#define INSIGHT_DISK_IDENTITY_ATTRIBUTES G_FILE_ATTRIBUTE_ID_FILE "," G_FILE_ATTRIBUTE_ETAG_VALUE
/* Worker-only operation. The identity is taken from the displayed directory
 * snapshot. Move never overwrites or falls back to a recursive copy/delete.
 * Trash never falls back to permanent deletion. No filesystem lock is held. */
gboolean insight_disk_operation(const gchar *source,const gchar *destination,
 const gchar *file_id,const gchar *etag,gboolean trash,GCancellable *cancel,GError **error);
#endif
