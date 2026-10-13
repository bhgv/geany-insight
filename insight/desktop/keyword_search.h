/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_KEYWORD_SEARCH_H
#define INSIGHT_KEYWORD_SEARCH_H
#include "context_search.h"
/* Boolean keywords: implicit/explicit AND, OR, NOT/-/!, parentheses,
 * quoted phrases, =case-sensitive terms and ?"regular expressions".
 * Context is the maximum distance between matching start lines, 0..50. */
gboolean insight_keyword_collect(const char *,gsize,const char *,guint,guint,InsightSearchMatch *,gsize,gsize *,GError **);
#endif
