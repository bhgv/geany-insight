/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RELATION_QUERY_H
#define INSIGHT_RELATION_QUERY_H
#include "../core/index.h"
typedef enum { INSIGHT_RELATION_DEFINITIONS,INSIGHT_RELATION_MEMBERS } InsightRelationMode;
/* Tag-based definitions/declarations and direct scoped members. Borrowed
 * results; no inheritance, overload or call graph inference. SIZE_MAX invalid. */
size_t insight_relation_query(const InsightIndex *,const char *,InsightRelationMode,size_t,const InsightSymbol **,size_t);
#endif
