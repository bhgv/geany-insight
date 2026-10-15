/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_DIFF_H
#define INSIGHT_DIFF_H
#include <stddef.h>
typedef struct { size_t a_begin, a_end, b_begin, b_end; } InsightDiffHunk;
typedef struct { InsightDiffHunk *hunks; size_t count; } InsightDiff;
/* Byte offsets; CR, LF and CRLF delimit lines; exact endings and final-newline
 * differences are retained.
 * Returns 0 on success, -1 on allocation failure, -2 on work limit exceeded.
 * Inputs must be NUL-terminated text without embedded NULs. */
int insight_diff_compute(const char *a, const char *b, InsightDiff *out);
void insight_diff_clear(InsightDiff *diff);
#endif
