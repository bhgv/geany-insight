/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_TEXT_TRANSFER_H
#define INSIGHT_TEXT_TRANSFER_H
#include <gio/gio.h>
#define INSIGHT_TEXT_TRANSFER_LIMIT (8u*1024u*1024u)
/* Local regular files, bounded UTF-8 without NUL. Read strips one UTF-8 BOM.
 * Write preserves exact bytes, uses conditional replacement or an exclusive
 * sibling temporary and no-overwrite move. Caller must protect open documents.
 * These GIO identity checks do not constitute a filesystem lock. */
gchar *insight_text_file_read(const gchar *,gsize *,GError **);
gboolean insight_text_file_write(const gchar *,const gchar *,gsize,GError **);
#endif
