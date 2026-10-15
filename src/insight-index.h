/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_INDEX_H
#define GEANY_INSIGHT_INDEX_H
#include <glib.h>
#include "tagmanager/tm_source_file.h"
#include "tagmanager/tm_tag.h"
#include "../insight/core/index.h"
#include "../insight/desktop/type_resolve.h"
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
/* Nominal receiver navigation: indexed aliases/inheritance/fields, this and
 * qualified names. C-family call results require every visible candidate to
 * resolve to the same owner; argument types and overloads are not selected.
 * Macros, callable objects, indexing and exceeded bounds decline. Template
 * substitution and dependent-type semantics are not implemented.
 * member suppresses broad name lookup for explicit member syntax, masked
 * non-code and invalid lexical source. Call text must start at source byte zero. */
gchar *insight_native_member_owner(const InsightIndex *,const char *,const char *,gulong,const char *,gsize,gboolean *);
gchar *insight_native_member_owner_filtered(const InsightIndex *,const char *,const char *,gulong,const char *,gsize,gboolean *,InsightTypeEligible,void *);
/* Call sites with a captured non-C-family parser disable C-family calls while
 * retaining nominal field/qualified-name navigation. No extension guessing. */
/* Full lexical source from byte zero. mask, when supplied, has identical byte
 * offsets/length and replaces C-family comments/strings with spaces. Borrowed
 * for the duration of this call. A missing mask is computed lazily once. */
typedef struct { const gchar *text,*mask;gsize length; } InsightNativeSource;
gchar *insight_native_member_owner_source_filtered(const InsightIndex *,const char *,const char *,gulong,const InsightNativeSource *,gsize,gboolean *,gboolean,InsightTypeEligible,void *);
gchar *insight_native_member_owner_calls_filtered(const InsightIndex *,const char *,const char *,gulong,const char *,gsize,gboolean *,gboolean,InsightTypeEligible,void *);
gboolean insight_native_member_matches(const InsightSymbol *,const char *);
guint insight_native_definition_rank(const InsightSymbol *,const char *);
#endif
