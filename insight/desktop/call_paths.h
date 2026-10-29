/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CALL_PATHS_H
#define INSIGHT_CALL_PATHS_H
#include <gio/gio.h>
#include "../core/index.h"
#define INSIGHT_CALL_FUNCTION_LIMIT 20000
#define INSIGHT_CALL_EDGE_LIMIT 50000
#define INSIGHT_CALL_PATH_LIMIT 32
#define INSIGHT_CALL_DEPTH_LIMIT 64
typedef gchar *(*InsightCallLoad)(const gchar *,GCancellable *,gpointer);
typedef struct { gchar *name,*scope,*path;guint line;gsize begin,body,end;gboolean checked,valid,is_static; } InsightCallFunction;
typedef struct { gchar *text,*mask,*digest;GArray *lines; } InsightCallSource;
typedef struct { guint from,to,line,column;gboolean uncertain; } InsightCallEdge;
typedef struct {
 GPtrArray *functions,*paths;GArray *edges;GHashTable *sources;
 guint start,target,skipped,unresolved,ambiguous;gboolean limited;
} InsightCallPaths;
typedef struct { const gchar *name,*path;guint line; } InsightCallEndpoint;
/* Bounded shortest lexical call chains. Paths contain edge indices; an empty
 * path is valid when start==target. Sources are loaded lazily and owned by
 * the result. No execution/type/overload inference. */
InsightCallPaths *insight_call_paths_find(const InsightIndex *,GPtrArray *,const InsightCallEndpoint *,const InsightCallEndpoint *,gboolean,InsightCallLoad,gpointer,GCancellable *,GError **);
void insight_call_paths_free(InsightCallPaths *);
/* Normalizes BOM and CRLF only; line/within-line byte columns stay usable. */
gchar *insight_call_normalize(const gchar *);
gchar *insight_call_digest(const gchar *);
#endif
