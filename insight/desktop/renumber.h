/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RENUMBER_H
#define INSIGHT_RENUMBER_H
#include <glib.h>
/* Modes: ascending from first token, ascending from value, add value.
 * Radix: preserve each token, decimal, hexadecimal, octal. */
gchar *insight_renumber(const gchar *text, gint mode, gint radix,
                        const gchar *value, guint *count, GError **error);
typedef struct { gsize begin,end,after_begin,after_end;guint count; } InsightRenumberRange;
typedef struct { gchar *text;GArray *ranges;guint count; } InsightRenumberPlan;
/* One shared sequence across ordered ranges; unselected bytes are preserved.
 * An empty range has no tokens and never expands to the whole document. */
InsightRenumberPlan *insight_renumber_plan(const gchar *,const InsightRenumberRange *,guint,gint,gint,const gchar *,GError **);
void insight_renumber_plan_free(InsightRenumberPlan *);
#endif
