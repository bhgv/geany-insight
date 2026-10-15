/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CONTEXT_H
#define INSIGHT_CONTEXT_H
#include <stddef.h>
/* Unicode 15.0 XID identifier, not semantic resolution or normalization.
 * Byte offsets; caret inside a UTF-8 scalar is supported. UTF-8 input assumed. */
int insight_word_at(const char *,size_t,size_t,size_t *,size_t *);
int insight_context_range(const char *,size_t,unsigned long,unsigned int,
 size_t *,size_t *,unsigned long *,size_t *,size_t *);
#endif
