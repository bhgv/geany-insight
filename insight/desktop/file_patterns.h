/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_FILE_PATTERNS_H
#define INSIGHT_FILE_PATTERNS_H
#include <glib.h>
typedef struct InsightFilePatterns InsightFilePatterns;
enum { INSIGHT_FILE_PATTERN_ANY=-1,INSIGHT_FILE_PATTERN_EXACT,INSIGHT_FILE_PATTERN_GLOB,INSIGHT_FILE_PATTERN_DEFAULT };
/* Filename-only filters: space/comma/semicolon separated, * and ?, bounded UTF-8.
 * Windows folds case; other hosts preserve it. No path components or quoting. */
InsightFilePatterns *insight_file_patterns_new(const gchar *,GError **);
gboolean insight_file_patterns_match(const InsightFilePatterns *,const gchar *,gint);
gchar **insight_file_patterns_tokens(const InsightFilePatterns *);
void insight_file_patterns_free(InsightFilePatterns *);
#endif
