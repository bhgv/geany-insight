/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_DOCUMENT_H
#define INSIGHT_DOCUMENT_H
#include <stddef.h>
#define INSIGHT_DOCUMENT_LIMIT (2u * 1024u * 1024u)
typedef struct {
 char *text, *selection;
 size_t length, start, end; /* UTF-8 byte offsets */
} InsightDocument;
/* Strict UTF-8, no embedded NUL; selection must be on code point boundaries.
 * doc must be zero-initialized, and disposed before reinitializing. */
int insight_document_init(InsightDocument *,const char *,size_t,size_t,size_t);
void insight_document_dispose(InsightDocument *);
const char *insight_document_text(void *);
const char *insight_document_selection(void *);
int insight_document_replace(void *,const char *);
#endif
