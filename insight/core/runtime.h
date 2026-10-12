/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RUNTIME_H
#define INSIGHT_RUNTIME_H
#include <stddef.h>
#include "index.h"
typedef struct {
 void *context;
 /* Borrowed UTF-8 strings, valid until the next host call. */
 const char *(*text)(void *);
 const char *(*selection)(void *);
 int (*replace_selection)(void *, const char *);
 /* Optional project index; borrowed, immutable during script execution. */
 const InsightIndex *index;
} InsightHost;
/* Fresh interpreter per invocation. Host edits are immediate, not transactional.
 * Returns 0 on success, 1 on script error, 2 on invalid host/allocation failure. */
int insight_run(const InsightHost *, const char *, size_t, char *, size_t);
#endif
