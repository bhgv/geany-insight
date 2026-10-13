/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RELATION_QUERY_H
#define INSIGHT_RELATION_QUERY_H
#include "../core/index.h"
typedef enum { INSIGHT_RELATION_DEFINITIONS,INSIGHT_RELATION_MEMBERS,INSIGHT_RELATION_BASES,INSIGHT_RELATION_DERIVED } InsightRelationMode;
/* Tag-based definitions/declarations direct scoped members and direct inheritance recorded by tags. Borrowed
 * results; no alias/overload resolution or call graph inference. SIZE_MAX invalid. */
size_t insight_relation_query(const InsightIndex *,const char *,InsightRelationMode,size_t,const InsightSymbol **,size_t);
#endif
