/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CONTEXT_SEARCH_H
#define INSIGHT_CONTEXT_SEARCH_H
#include <glib.h>
enum { INSIGHT_SEARCH_CASE=1,INSIGHT_SEARCH_WORD=2,INSIGHT_SEARCH_REGEX=4 };
gboolean insight_context_find(const char *,gsize,const char *,gsize,gboolean,guint,gsize *,gsize *,gboolean *,GError **);
#endif
