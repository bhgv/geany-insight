/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_TEXT_PRINT_H
#define INSIGHT_TEXT_PRINT_H
#include <gtk/gtk.h>
/* Immutable UTF-8 snapshot; NULL on invalid input. The caller owns the result.
 * "insight-text-print-error" holds a static diagnostic after failed pagination. */
GtkPrintOperation *insight_text_print_operation(const gchar *,const gchar *,gsize);
/* Shows the native print dialog and consumes the operation (including NULL). */
void insight_text_print_run(GtkPrintOperation *,GtkWindow *);
#endif
