/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SYMBOL_EDIT_H
#define INSIGHT_SYMBOL_EDIT_H
#include <gio/gio.h>
#define INSIGHT_SYMBOL_SOURCE_LIMIT (2u*1024u*1024u)
#define INSIGHT_SYMBOL_TEXT_LIMIT (1024u*1024u)
typedef enum { INSIGHT_SYMBOL_COPY, INSIGHT_SYMBOL_CUT, INSIGHT_SYMBOL_DUPLICATE, INSIGHT_SYMBOL_PASTE } InsightSymbolEdit;
typedef struct { gchar *text,*payload;gsize begin,end,selection_begin,selection_end; } InsightSymbolPlan;
gboolean insight_symbol_plan(const gchar *,gsize,gsize,gsize,InsightSymbolEdit,const gchar *,const gchar *,InsightSymbolPlan *,GError **);
void insight_symbol_plan_clear(InsightSymbolPlan *);
#endif
