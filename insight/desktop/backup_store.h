/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_BACKUP_STORE_H
#define INSIGHT_BACKUP_STORE_H
#include <gio/gio.h>
#define INSIGHT_BACKUP_TEXT_LIMIT (8u*1024u*1024u)
#define INSIGHT_BACKUP_TOTAL_LIMIT (64u*1024u*1024u)
#define INSIGHT_BACKUP_COUNT_LIMIT 200u
typedef struct { gchar *name,*etag,*file_id;gsize size; } InsightBackupEntry;
/* Native absolute paths, lexical source identity. No filesystem lock is held.
 * List is newest first; missing directories produce an empty list.
 * Each write is exclusive and publishes only a completed UTF-8 snapshot.
 * Read validates the listed identity and preserves every byte, including U+FEFF. */
gchar *insight_backup_directory(const gchar *config,const gchar *source,GError **error);
GPtrArray *insight_backup_list(const gchar *directory,GError **error);
gboolean insight_backup_save(const gchar *directory,const gchar *text,gsize size,gchar **saved,GError **error);
gchar *insight_backup_read(const gchar *directory,const InsightBackupEntry *entry,gsize *size,GError **error);
#endif
