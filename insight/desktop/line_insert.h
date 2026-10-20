/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_LINE_INSERT_H
#define INSIGHT_LINE_INSERT_H
#include <glib.h>
typedef struct { gint start,end,caret;gchar *text; } InsightLineInsert;
/* mode: 0 above active caret line, 1 below, 2 split/replace selection and stay.
 * Owned replacement; positions and caret are UTF-8 bytes in source/result. */
gboolean insight_line_insert(const gchar *source,gint anchor,gint caret,gint mode,
                             gboolean indent,const gchar *eol,
                             InsightLineInsert *plan,GError **error);
void insight_line_insert_clear(InsightLineInsert *plan);
#endif
