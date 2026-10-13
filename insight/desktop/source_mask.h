/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SOURCE_MASK_H
#define INSIGHT_SOURCE_MASK_H
#include <glib.h>
typedef enum { INSIGHT_CODE_C,INSIGHT_CODE_JS,INSIGHT_CODE_PYTHON,INSIGHT_CODE_LUA,INSIGHT_CODE_SHELL } InsightCodeDialect;
/* Replace comments and quoted strings with spaces, retaining byte offsets and
 * CR/LF. Lexical only: no overload resolution, preprocessing or interpolation. */
gchar *insight_source_code_mask(const char *,gsize,InsightCodeDialect,GError **);
#endif
