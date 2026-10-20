/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RESTORE_LINES_H
#define INSIGHT_RESTORE_LINES_H
#include <stddef.h>
#define INSIGHT_RESTORE_LIMIT (8u * 1024u * 1024u)
typedef struct {
    char *replacement;
    size_t length, blocks;
    size_t current_begin, current_end, original_begin, original_end;
} InsightRestorePlan;
/* NUL-terminated UTF-8 inputs, byte selection bounds. Expand to complete
 * physical lines; restore whole exact-diff blocks touching that range.
 * Deletion anchors at either range edge are included, including EOF.
 * 0 success (blocks=0 means no-op), -1 allocation, -2 work/size limit,
 * -4 invalid selection or partial CRLF. No caller memory is modified. */
int insight_restore_lines_plan(const char *original, const char *current,
    size_t anchor, size_t caret, InsightRestorePlan *out);
void insight_restore_lines_clear(InsightRestorePlan *plan);
#endif
