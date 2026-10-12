/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CONTEXT_H
#define INSIGHT_CONTEXT_H
#include <stddef.h>
/* Lexical identifier only, not semantic resolution. UTF-8 input is assumed. */
int insight_word_at(const char *,size_t,size_t,size_t *,size_t *);
int insight_context_range(const char *,size_t,unsigned long,unsigned int,
 size_t *,size_t *,unsigned long *,size_t *,size_t *);
#endif
