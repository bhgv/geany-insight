/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TM_COMPLETION_H
#define TM_COMPLETION_H
#include <glib.h>
typedef struct TMCompletionPrefix TMCompletionPrefix;
/* Bounded UTF-8 names: prefix <=512 bytes, identifier <=4096 bytes. */
TMCompletionPrefix *tm_completion_prefix_new(const gchar *,gboolean);
gboolean tm_completion_prefix_matches(const TMCompletionPrefix *,const gchar *);
void tm_completion_prefix_free(TMCompletionPrefix *);
#endif
