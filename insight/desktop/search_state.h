/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SEARCH_STATE_H
#define INSIGHT_SEARCH_STATE_H
#include <gio/gio.h>
typedef struct {
 gchar *query,*mask;
 guint flags,scope,context;
 gboolean keywords;
 GPtrArray *files;
} InsightSearchState;
InsightSearchState *insight_search_state_new(void);
void insight_search_state_free(InsightSearchState *);
gboolean insight_search_state_save(const InsightSearchState *,const char *,GError **);
InsightSearchState *insight_search_state_load(const char *,GError **);
#endif
