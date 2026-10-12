/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_INDEX_H
#define INSIGHT_INDEX_H
#include <stddef.h>
typedef struct {
 const char *name, *path, *scope, *kind;
 unsigned long line; /* one-based */
} InsightSymbol;
typedef struct InsightIndex InsightIndex;
InsightIndex *insight_index_new(void);
void insight_index_free(InsightIndex *);
size_t insight_index_count(const InsightIndex *);
/* Atomically replace all symbols for path; zero entries deletes that file.
 * Strings are copied. Returns 0 for invalid input/allocation failure. */
int insight_index_update(InsightIndex *,const char *path,const InsightSymbol *,size_t);
/* Case-sensitive UTF-8 byte comparison; exact=0 selects prefix matching.
 * Returns total matches, writes at most capacity borrowed pointers to out.
 * Pointers remain valid until the next successful update/free. */
size_t insight_index_find(const InsightIndex *,const char *,int exact,
 const InsightSymbol **out,size_t capacity);
#endif
