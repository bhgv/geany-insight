/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SEARCH_LINKS_H
#define INSIGHT_SEARCH_LINKS_H
#include "editor.h"
#include "document.h"
typedef struct InsightSearchLinks InsightSearchLinks;
typedef struct { gint display,path,line,start,end,id,digest,column; } InsightSearchLinkColumns;
InsightSearchLinks *insight_search_links_new(GtkTreeView *,InsightSearchLinkColumns);
void insight_search_links_free(InsightSearchLinks *);
void insight_search_links_clear(InsightSearchLinks *);
void insight_search_links_capture(InsightSearchLinks *,GtkTreeIter *);
void insight_search_links_refresh(InsightSearchLinks *,GtkTreePath *);
gboolean insight_search_links_open(InsightSearchLinks *,GtkTreePath *);
gboolean insight_search_links_return(InsightSearchLinks *,GeanyDocument *);
void insight_search_links_notify(InsightSearchLinks *,GeanyEditor *,SCNotification *);
void insight_search_links_closed(InsightSearchLinks *,GeanyDocument *);
#endif
