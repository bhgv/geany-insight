/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_LOOKUP_FILTER_H
#define INSIGHT_LOOKUP_FILTER_H
#include <glib.h>
typedef struct InsightLookupFilter InsightLookupFilter;
/* Prefix a term (also after @ or scope=) with ~ for Unicode subsequence matching. */
InsightLookupFilter *insight_lookup_filter_new(const char *query,gboolean files,GError **error);
gboolean insight_lookup_filter_match(const InsightLookupFilter *,const char *name,const char *scope,const char *path,const char *kind);
/* True only for glob terms in file mode, excluding regex/exact/fuzzy terms. */
gboolean insight_lookup_filter_has_wildcards(const InsightLookupFilter *);
void insight_lookup_filter_free(InsightLookupFilter *);
/* Plain list filters retain literal, case-insensitive substring matching.
 * A leading ~ selects a Unicode subsequence of the entire query (spaces and
 * punctuation remain literal). Invalid UTF-8 declines; bare ~ matches none. */
gboolean insight_lookup_text_match(const char *text,const char *query);
#endif
