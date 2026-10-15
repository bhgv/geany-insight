/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_TYPE_RESOLVE_H
#define INSIGHT_TYPE_RESOLVE_H
#include <glib.h>
#include "../core/index.h"
typedef enum { INSIGHT_TYPE_FOUND,INSIGHT_TYPE_UNKNOWN,INSIGHT_TYPE_AMBIGUOUS,INSIGHT_TYPE_CYCLE,INSIGHT_TYPE_LIMIT,INSIGHT_TYPE_INVALID } InsightTypeStatus;
typedef int (*InsightTypeEligible)(const InsightSymbol *,void *);
typedef struct { InsightTypeStatus status;gchar *owner;const InsightSymbol *symbol;guint aliases; } InsightTypeResult;
/* Normalize a nominal qualified type, removing cv/elaborated qualifiers,
 * pointer/reference markers, array bounds and balanced template arguments.
 * Does not instantiate templates or interpret function/decltype expressions. */
gchar *insight_type_name(const gchar *,gboolean *);
/* Resolve lexical scopes from inner to outer. Same-file declarations may
 * disambiguate a scope; otherwise duplicates are not guessed. Alias targets
 * are resolved in the alias declaration's scope/file. Qualified nested owners
 * may expand aliases and find explicit qualified inherited nested types.
 * Direct nested declarations hide bases; distinct declarations, unresolved
 * bases and non-type hiding decline. Inheritance work is bounded to 4096
 * visited owners and 256 bases per owner. Unqualified names search class
 * bases before enclosing scopes; lexical walking also shares the depth bound.
 * Qualified names select their nearest first component before its members;
 * a missing/hidden member does not reopen outer-owner lookup. Namespaces
 * retain use-file preference; class members use declaration context.
 * Template instantiation and full include/ODR/access semantics remain open.
 * All alias-expansion work, including unsuccessful probes, is bounded to 32.
 * Reported aliases exclude completed unsuccessful scope/base probes. Recursion
 * is bounded to 64. Only active expansions form a cycle (an owner alias may be reused).
 * owner is owned; symbol is borrowed from the caller-owned index snapshot. */
InsightTypeResult insight_type_resolve(const InsightIndex *,const gchar *,const gchar *,const gchar *,InsightTypeEligible,void *);
void insight_type_result_clear(InsightTypeResult *);
#endif
