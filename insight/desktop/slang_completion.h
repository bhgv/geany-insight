/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SLANG_COMPLETION_H
#define INSIGHT_SLANG_COMPLETION_H
#include "clang_backend.h"
/* Owned path to a Vivado export beside a project, or NULL. */
gchar *insight_slang_vivado_config(const gchar *project_file);
gboolean insight_slang_source(const gchar *);
/* Worker-only. Captured paths/buffers are immutable UTF-8 snapshots. The
 * executable path is selected on the main thread; no GTK access here.
 * NULL declines/fails and lets native completion take over. */
GPtrArray *insight_slang_complete(const gchar *service,const gchar *path,const gchar *text,
 guint offset,guint rootlen,GPtrArray *paths,GHashTable *buffers,GCancellable *);
gboolean insight_slang_wave_definition(const gchar *,const gchar *,GPtrArray *,GHashTable *,
 const gchar *,GCancellable *,gchar **,guint *,guint *);
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
GPtrArray *insight_slang_complete_configured(const gchar *service,const gchar *path,const gchar *text,
 guint offset,guint rootlen,GPtrArray *paths,GHashTable *buffers,const gchar *config,GCancellable *cancel);
gboolean insight_slang_definition_configured(const gchar *service,const gchar *path,const gchar *text,guint offset,
 GPtrArray *paths,GHashTable *buffers,const gchar *config,GCancellable *cancel,gchar **target,guint *line,guint *column);
InsightSlangProject *insight_slang_project_configured(const gchar *service,GPtrArray *paths,GHashTable *buffers,const gchar *root,const gchar *cache,const gchar *config,GCancellable *cancel);
#endif
