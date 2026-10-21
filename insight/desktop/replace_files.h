/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_REPLACE_FILES_H
#define INSIGHT_REPLACE_FILES_H
#include <gio/gio.h>
#include "source_mask.h"
#define INSIGHT_REPLACE_FILE_LIMIT (1024u*1024u)
typedef struct { gboolean case_sensitive,whole,regex,dotall,first_per_line,preserve_case;InsightSourceRegion region;InsightCodeDialect dialect; } InsightReplaceOptions;
typedef struct { gsize begin,end,after;guint line,column;gchar *replacement;gboolean selected; } InsightReplaceHit;
typedef struct { gchar *before;GPtrArray *hits; } InsightReplacePlan;
InsightReplacePlan *insight_replace_plan(const gchar *,const gchar *,const gchar *,const InsightReplaceOptions *,GCancellable *,GError **);
void insight_replace_plan_free(InsightReplacePlan *);
/* Rebuild after individual inclusion changes. Exact bytes outside enabled hits
 * are retained, including BOM, CRLF and terminal newlines. */
gchar *insight_replace_render(InsightReplacePlan *,guint *,GError **);
/* Bounded local UTF-8 snapshot, strips one BOM for editor-buffer comparison. */
gchar *insight_replace_read(const gchar *,GCancellable *,GError **);
gboolean insight_replace_mask(const gchar *,const gchar *,GError **);
#endif
