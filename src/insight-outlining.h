/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_OUTLINING_H
#define INSIGHT_OUTLINING_H
#include "editor.h"
void insight_outlining_init(void);
void insight_outlining_finalize(void);
void insight_outlining_notify(GeanyEditor *, SCNotification *);
/* Expand/collapse current, expand/collapse branch, expand/collapse all,
 * fold to depth, block browser. */
void insight_outlining_command(guint action);
#endif
