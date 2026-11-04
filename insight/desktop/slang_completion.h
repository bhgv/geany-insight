/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SLANG_COMPLETION_H
#define INSIGHT_SLANG_COMPLETION_H
#include "clang_backend.h"
gboolean insight_slang_source(const gchar *);
/* Worker-only. Captured paths/buffers are immutable UTF-8 snapshots. The
 * executable path is selected on the main thread; no GTK access here.
 * NULL declines/fails and lets native completion take over. */
GPtrArray *insight_slang_complete(const gchar *service,const gchar *path,const gchar *text,
 guint offset,guint rootlen,GPtrArray *paths,GHashTable *buffers,GCancellable *);
gboolean insight_slang_definition(const gchar *,const gchar *,const gchar *,guint,
 GPtrArray *,GHashTable *,GCancellable *,gchar **,guint *,guint *);
typedef struct {guint64 hits,starts,bypasses;guint entries;gsize bytes;} InsightSlangCacheStats;
/* Process-local bounded response cache; no AST persistence. Safe across workers. */
void insight_slang_cache_clear(void);
void insight_slang_cache_stats(InsightSlangCacheStats *);
typedef struct {InsightIndex *index;guint diagnostics;gboolean restored,persisted;} InsightSlangProject;
/* Worker-only project sidecar. NULL cache means a memory-only workspace.
 * Caller owns the result. Preprocessor sets are saved but not reused yet. */
InsightSlangProject *insight_slang_project(const gchar *,GPtrArray *,GHashTable *,const gchar *,const gchar *,GCancellable *);
void insight_slang_project_free(InsightSlangProject *);
#endif
