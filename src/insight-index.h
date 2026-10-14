/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_INDEX_H
#define GEANY_INSIGHT_INDEX_H
#include <glib.h>
#include "tagmanager/tm_source_file.h"
#include "tagmanager/tm_tag.h"
#include "../insight/core/index.h"
/* Copy native tag data into the owning UTF-8 index. NULL/empty tags clear path.
 * Reject >250k total symbols without changing the previous file snapshot. */
gboolean insight_native_import(InsightIndex *,TMSourceFile *,const char *);
/* Parse validated UTF-8 source and import produced tags. The legacy parser
 * return value is not a reliable success indicator for nonempty buffers. */
gboolean insight_native_parse(InsightIndex *,TMSourceFile *,const char *,const char *,gsize);
/* Local/parameter tags are eligible only in the same file and function,
 * at or before the reference. Negative means outside the reference scope. */
gboolean insight_native_language_compatible(TMParserType,TMParserType);
gint insight_native_definition_rank_at(const InsightSymbol *,const char *,const char *,gulong);
gchar *insight_native_function_scope(const InsightIndex *,const char *,gulong);
gchar *insight_native_member_owner(const InsightIndex *,const char *,const char *,gulong,const char *,gsize,gboolean *);
gboolean insight_native_member_matches(const InsightSymbol *,const char *);
guint insight_native_definition_rank(const InsightSymbol *,const char *);
#endif
