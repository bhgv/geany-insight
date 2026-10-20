/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_DELIMITER_NAVIGATION_H
#define INSIGHT_DELIMITER_NAVIGATION_H
#include <stddef.h>
#define INSIGHT_DELIMITER_LIMIT (8u*1024u*1024u)
#define INSIGHT_DELIMITER_DEPTH 4096u
typedef struct { size_t begin,end; } InsightDelimiter; /* end exclusive */
/* Lexical code mask, preserving source byte positions. Pairs containing the
 * caret (opening <= caret <= closing) are ordered innermost first. Mismatched
 * closers discard the pending stack; no fabricated enclosing pairs.
 * Caller frees *pairs. 0 success, -1 invalid/allocation, -2 size/depth limit. */
int insight_enclosing_delimiters(const char *,size_t,size_t,InsightDelimiter **,size_t *);
/* 0 Paren Left, 1 Paren Right: nearest enclosing matched '(' / ')' strictly
 * in the requested direction. 2 Block Up, 3 Block Down: previous literal '{'
 * / next literal '}' in the supplied lexical mask, including unmatched braces.
 * 1 found, 0 boundary/no pair, negative as above. Output unchanged on failure. */
int insight_delimiter_neighbor(const char *,size_t,size_t,unsigned,size_t *);
#endif
