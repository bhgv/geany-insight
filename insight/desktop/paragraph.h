/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_PARAGRAPH_H
#define INSIGHT_PARAGRAPH_H
#include <glib.h>
/* UTF-8 byte positions. join=FALSE expands to blank-delimited paragraphs;
 * join=TRUE expands to intersected lines (caret: current and next).
 * Returns replacement for [start,end), or NULL with no changes on failure. */
gchar *insight_paragraph(const gchar *text,gint anchor,gint caret,gboolean join,
                        gint margin,gint tab,const gchar *eol,
                        gint *start,gint *end,GError **error);
#endif
