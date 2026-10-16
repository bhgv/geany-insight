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
enum { INSIGHT_DIFF_IGNORE_SPACE=1u, INSIGHT_DIFF_IGNORE_EOL=2u };
/* Ignore ASCII horizontal whitespace and/or CR/LF/CRLF differences. A missing
 * final terminator still differs. Hunk offsets always refer to original bytes;
 * ignored differences are kept from Left in a merge result. */
int insight_diff_compute_options(const char *a,const char *b,unsigned options,InsightDiff *out);
/* Zero-based logical lines, including CR-only sources. Works with all options.
 * Byte mapping above is only valid for the exact (options=0) comparison. */
size_t insight_diff_map_line(const InsightDiff *diff,int side,const char *source,const char *target,size_t line);
void insight_diff_clear(InsightDiff *diff);
#endif
