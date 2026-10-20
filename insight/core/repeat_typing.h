/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_REPEAT_TYPING_H
#define INSIGHT_REPEAT_TYPING_H
#include <stddef.h>
#define INSIGHT_REPEAT_DOCUMENT (8u*1024u*1024u)
#define INSIGHT_REPEAT_TEXT 4096u
#define INSIGHT_REPEAT_COUNT 100u
typedef struct { char *replacement;size_t length,begin,end,caret;int changed; } InsightRepeatPlan;
/* Valid NUL-terminated UTF-8 inputs; single-line payload (tab permitted).
 * Exact forward/reverse replacement, no EOL conversion, caret after output.
 * 0 success, -1 allocation, -2 size/count, -4 invalid bounds/control text.
 * No-op plans retain changed=0. Caller releases with clear. */
int insight_repeat_plan(const char *,size_t,size_t,const char *,unsigned,InsightRepeatPlan *);
void insight_repeat_clear(InsightRepeatPlan *);
#endif
