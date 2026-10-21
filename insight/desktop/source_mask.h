/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SOURCE_MASK_H
#define INSIGHT_SOURCE_MASK_H
#include <glib.h>
typedef enum { INSIGHT_CODE_C,INSIGHT_CODE_JS,INSIGHT_CODE_PYTHON,INSIGHT_CODE_LUA,INSIGHT_CODE_SHELL } InsightCodeDialect;
typedef enum { INSIGHT_REGION_ALL,INSIGHT_REGION_CODE,INSIGHT_REGION_SKIP_COMMENTS,INSIGHT_REGION_COMMENTS } InsightSourceRegion;
/* Preserve byte offsets/CRLF and the selected lexical region. Comments-only
 * excludes quoted strings, including Python docstrings and C++ raw strings. */
gchar *insight_source_region_mask(const char *,gsize,InsightCodeDialect,InsightSourceRegion,GError **);
/* One byte per source byte: 1=inside the requested lexical region, 0=outside.
 * Unlike the display mask, this also distinguishes spaces and line endings. */
guchar *insight_source_region_membership(const char *,gsize,InsightCodeDialect,InsightSourceRegion,GError **);
/* Replace comments and quoted strings with spaces, retaining byte offsets and
 * CR/LF. Lexical only: no overload resolution, preprocessing or interpolation. */
gchar *insight_source_code_mask(const char *,gsize,InsightCodeDialect,GError **);
#endif
