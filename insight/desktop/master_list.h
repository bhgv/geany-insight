/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_MASTER_LIST_H
#define INSIGHT_MASTER_LIST_H
#include "project_files.h"
typedef struct { GPtrArray *files,*trees;GHashTable *dependencies;gchar *etag,*digest,*text,*membership_digest; } InsightMasterList;
/* Immutable borrowed filename globs, literal extensions, and bound full paths.
 * copy/free manage an owned snapshot; NULL uses the legacy recursive API. */
typedef struct InsightMasterTypes { const gchar *const *patterns,*const *extensions,*const *files; } InsightMasterTypes;
InsightMasterTypes *insight_master_types_copy(const InsightMasterTypes *);
void insight_master_types_free(InsightMasterTypes *);
/* Typed directory entries expand only regular direct children of known types.
 * Explicit file entries and wildcard rules retain their existing semantics. */
InsightMasterList *insight_master_list_parse_typed(const gchar *,gsize,const gchar *,const InsightMasterTypes *,GError **);
InsightMasterList *insight_master_list_read_typed(const gchar *,const gchar *,const InsightMasterTypes *,GCancellable *,GError **);

/* Plain UTF-8/BOM/CRLF file lists, one path per line, relative to root.
 * Filename * and ? masks enumerate regular direct children, without following
 * child symlinks. At most 256 masks / 100000 visited entries / 1024 bytes
 * per filename mask; wildcard directory components are unsupported.
 * Directory suffix / preserves nonexistent directory entries. Existing
 * directories are recognized too. Limits: 2 MiB, 10000 unique entries.
 * Lines beginning with ; are comments; use ./;name for literal names.
 * Paths outside root remain explicit; no path variables are expanded. */
InsightMasterList *insight_master_list_parse(const gchar *,gsize,const gchar *,GError **);
InsightMasterList *insight_master_list_read(const gchar *,const gchar *,GCancellable *,GError **);
void insight_master_list_free(InsightMasterList *);
/* Metadata only: no list contents or directory names are read. */
gboolean insight_master_list_unchanged(GHashTable *,GCancellable *);
/* Refresh after changing the owned file/tree arrays; order-independent. */
void insight_master_list_rehash(InsightMasterList *);
gchar *insight_master_list_render(const InsightMasterList *,const gchar *,GError **);
/* Compare content digest before replacement, then let GIO check its ETag.
 * Digest guards coarse timestamp filesystems. NULL guards explicitly allow
 * unconditional replacement. new_etag is published only on success. */
gboolean insight_master_list_save(const InsightMasterList *,const gchar *,const gchar *,const gchar *,const gchar *,GCancellable *,gchar **,GError **);
/* Roll back byte-for-byte to a read snapshot, with the same guards. */
gboolean insight_master_list_restore(const InsightMasterList *,const gchar *,const gchar *,const gchar *,const gchar *,GCancellable *,gchar **,GError **);
/* Replace only inclusion rules; retain project masks/exclusion overlays. */
InsightProjectFiles *insight_master_list_policy(const InsightMasterList *,const InsightProjectFiles *);
/* Expand a membership snapshot to explicit paths for a manual MFL write.
 * Missing explicit files remain entries. Directory walks do not follow
 * symlinks, skip the same generated/hidden trees as indexing, and stop at
 * depth 64, 4096 directories, 100000 entries or 10000 files, atomically. */
InsightMasterList *insight_master_list_collect(const InsightProjectFiles *,const gchar *,GCancellable *,GError **);
#endif
