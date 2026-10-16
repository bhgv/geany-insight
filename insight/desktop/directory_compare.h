/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_DIRECTORY_COMPARE_H
#define INSIGHT_DIRECTORY_COMPARE_H
#include <gio/gio.h>
enum { INSIGHT_DIR_CONTENT,INSIGHT_DIR_DATE,INSIGHT_DIR_SIZE_DATE };
enum { INSIGHT_DIR_SAME,INSIGHT_DIR_DIFFERENT,INSIGHT_DIR_LEFT,INSIGHT_DIR_RIGHT,INSIGHT_DIR_TYPE,INSIGHT_DIR_SKIPPED,INSIGHT_DIR_ERROR,INSIGHT_DIR_FOLDER };
typedef struct {
 gint method;gboolean recursive,hidden;guint text_flags;
 guint max_entries,max_depth;guint64 max_bytes;
 /* Borrowed immutable UTF-8 filename globs; NULL disables filtering, empty
  * vector accepts no files. Directory rows and traversal are retained. */
 const gchar *const *known_patterns;
} InsightDirectoryOptions;
typedef struct {
 gchar *relative;GFileInfo *info[2];gint status;gchar *detail;
} InsightDirectoryEntry;
/* Immutable snapshot, sorted by exact relative filename (native filename
 * encoding). Includes folders and skipped links/special files; never follows
 * enumerated symlinks. NULL on cancellation/root/scan/limit errors. Per-file
 * content failures are explicit error rows. No writes or external programs. */
InsightDirectoryOptions insight_directory_options(void);
GPtrArray *insight_directory_compare(const gchar *left,const gchar *right,
 const InsightDirectoryOptions *,GCancellable *,GError **);
#endif
