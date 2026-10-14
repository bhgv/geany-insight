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
/* Find the closest strictly preceding/following position; input may be unsorted. */
int insight_neighbor_position(const size_t *,size_t,size_t,int,size_t *);
#endif
