/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_INTELLIGENT_PASTE_H
#define INSIGHT_INTELLIGENT_PASTE_H
#include <glib.h>
/* NULL means ordinary paste: partial line, invalid input or bounded-work limit.
 * Whole lines end in CR/LF. Output uses destination indentation and EOL. */
gchar *insight_paste_reindent(const gchar *text, guint indent, guint tab_width,
                            gboolean use_tabs, const gchar *eol);
#endif
