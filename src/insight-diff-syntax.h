/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Native syntax coloring and private snapshot symbol collection.
 * RU: Встроенная раскраска синтаксиса и сбор символов отдельных снимков. */
#ifndef INSIGHT_DIFF_SYNTAX_H
#define INSIGHT_DIFF_SYNTAX_H
#include <gtk/gtk.h>
GPtrArray *insight_diff_native_symbols(const gchar *,const gchar *);
void insight_diff_native_colorize(GtkTextBuffer *,const gchar *);
#endif
