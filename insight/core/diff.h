/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_DIFF_H
#define INSIGHT_DIFF_H
#include <stddef.h>
typedef struct { size_t a_begin, a_end, b_begin, b_end; } InsightDiffHunk;
typedef struct { InsightDiffHunk *hunks; size_t count; } InsightDiff;
/* Byte offsets; CR, LF and CRLF delimit lines; exact endings and final-newline
 * differences are retained.
 * Returns 0 on success, -1 on allocation failure, -2 on work limit exceeded.
 * Pure insertion/deletion after common-edge trimming uses one hunk and no
 * LCS table. The work limit still applies to two-sided comparisons.
 * Inputs must be NUL-terminated text without embedded NULs. */
int insight_diff_compute(const char *a, const char *b, InsightDiff *out);
/* Map a valid source byte offset (side 0=left, 1=right) to the other source.
 * Unchanged spans map exactly; changed spans map to the opposite block start.
 * An insertion boundary maps after the inserted block. Sources must belong to diff. */
size_t insight_diff_map_offset(const InsightDiff *diff, int side, size_t offset);
void insight_diff_clear(InsightDiff *diff);
#endif
