/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RELATION_QUERY_H
#define INSIGHT_RELATION_QUERY_H
#include "../core/index.h"
typedef enum { INSIGHT_RELATION_DEFINITIONS,INSIGHT_RELATION_MEMBERS,INSIGHT_RELATION_BASES,INSIGHT_RELATION_DERIVED } InsightRelationMode;
/* Tag-based definitions/declarations direct scoped members and direct inheritance recorded by tags. Borrowed
 * results; no alias/overload resolution or call graph inference. SIZE_MAX invalid. */
size_t insight_relation_query(const InsightIndex *,const char *,InsightRelationMode,size_t,const InsightSymbol **,size_t);
/* Walk unambiguous indexed inheritance edges, with cycle protection and a
 * caller-selected depth bound (0..32; zero selects direct members). Members include direct and inherited
 * scopes, retaining overloads/shadowed names. Eligibility applies BEFORE
 * resolving edges. Indexed nominal typedef/using chains resolve in declaration
 * context; unions/enums/namespaces also own direct Members. Missing bases,
 * overloads and template instantiation are not inferred. */
typedef int (*InsightRelationEligible)(const InsightSymbol *,void *);
size_t insight_relation_query_transitive(const InsightIndex *,const char *,InsightRelationMode,
 unsigned,InsightRelationEligible,void *,size_t,const InsightSymbol **,size_t,int *,int *);
/* Per-name nominal member lookup. Direct declarations hide inherited names on
 * each branch; distinct declaring scopes are ambiguous and yield no hits.
 * Results are borrowed, retaining overloads in one declaring scope. Unknown
 * explicit namespaces may match direct declarations. Cycles, unresolved bases
 * and bounds (32 levels, 4096 scopes/name candidates) decline safely. */
size_t insight_member_query(const InsightIndex *,const char *,const char *,const char *,
 InsightRelationEligible,void *,const InsightSymbol **,size_t,int *,int *);
#endif
