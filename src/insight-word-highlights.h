/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_WORD_HIGHLIGHTS_H
#define INSIGHT_WORD_HIGHLIGHTS_H
#include "editor.h"
void insight_word_highlights_init(void);
void insight_word_highlights_finalize(void);
void insight_word_highlights_notify(GeanyEditor *,SCNotification *);
/* Toggle word, manager, clear, next and previous. */
void insight_word_highlights_command(guint action);
#endif
