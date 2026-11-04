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
#endif
