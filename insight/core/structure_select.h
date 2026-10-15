/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_STRUCTURE_SELECT_H
#define INSIGHT_STRUCTURE_SELECT_H
#include <stddef.h>
/* Input is a lexical code mask: comments and strings are replaced by spaces,
 * retaining original byte offsets. Output includes delimiters, end exclusive.
 * Returns 1 when found, 0 when no enclosing/matching pair exists, -1 on error.
 * Repeating block selection with its exact result expands to the next parent. */
int insight_select_block(const char *,size_t,size_t,size_t,size_t *,size_t *);
int insight_select_match(const char *,size_t,size_t,size_t *,size_t *);
/* C-like brace-bodied function extent; prototypes and expression bodies return 0.
 * declaration is the parser's definition-line offset, not a semantic AST range. */
int insight_function_extent(const char *,size_t,size_t,size_t *);
/* Function range from a parser definition line. Text and lexical mask must
 * retain identical byte offsets. Python uses indentation (tabs: 8 columns);
 * Lua balances block keywords. Unfinished bodies return 0. */
typedef enum { INSIGHT_FUNCTION_BRACES,INSIGHT_FUNCTION_PYTHON,INSIGHT_FUNCTION_LUA } InsightFunctionStyle;
int insight_function_range(const char *,const char *,size_t,size_t,InsightFunctionStyle,size_t *,size_t *);
int insight_suite_range(const char *,const char *,size_t,size_t,InsightFunctionStyle,size_t *,size_t *);
int insight_select_suite(const char *,const char *,size_t,size_t,size_t,InsightFunctionStyle,size_t *,size_t *);
/* Conservative named class/struct/union/enum/namespace/interface definition.
 * Starts at a parser declaration line, verifies keyword/name, and rejects
 * forwards, aliases, earlier same-line definitions and incomplete bodies.
 * Brace headers are bounded to 16 KiB / 128 groups / 64 template levels.
 * Python supports class suites; Lua has no nominal declaration here.
 * Returns 1/0/-1 as above; this is a lexical range, not a semantic AST. */
int insight_nominal_range(const char *,const char *,size_t,size_t,const char *,const char *,InsightFunctionStyle,size_t *,size_t *);
/* Find the closest strictly preceding/following position; input may be unsorted. */
int insight_neighbor_position(const size_t *,size_t,size_t,int,size_t *);
/* Lexical identifier followed by optional whitespace and an opening parenthesis.
 * Skips control keywords; preserves offsets in the supplied code mask. */
int insight_next_call(const char *,size_t,size_t *,size_t *,size_t *);
#endif
