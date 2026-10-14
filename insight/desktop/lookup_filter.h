/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_LOOKUP_FILTER_H
#define INSIGHT_LOOKUP_FILTER_H
#include <glib.h>
typedef struct InsightLookupFilter InsightLookupFilter;
/* Prefix a term (also after @ or scope=) with ~ for Unicode subsequence matching. */
InsightLookupFilter *insight_lookup_filter_new(const char *query,gboolean files,GError **error);
gboolean insight_lookup_filter_match(const InsightLookupFilter *,const char *name,const char *scope,const char *path,const char *kind);
void insight_lookup_filter_free(InsightLookupFilter *);
#endif
