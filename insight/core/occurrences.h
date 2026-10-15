/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_OCCURRENCES_H
#define INSIGHT_OCCURRENCES_H
#include <stddef.h>
typedef struct {
 size_t start,end; /* UTF-8 byte offsets, half-open */
 unsigned long line,column; /* line one-based, byte column zero-based */
} InsightOccurrence;
/* Whole lexical identifiers; comments/strings included, no scope resolution.
 * Unicode 15.0 XID start/continue properties, without normalization. Strict UTF-8 without
 * NUL; text <= 2 MiB, name 1..1024 bytes, may not start with ASCII digit.
 * Copies up to capacity matches after offset; total includes all matches.
 * Returns 1 on success; 0 on invalid input, resets total to zero. */
int insight_occurrences_find(const char *,size_t,const char *,size_t,size_t,
 InsightOccurrence *,size_t,size_t *);
#endif
