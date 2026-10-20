/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RENUMBER_H
#define INSIGHT_RENUMBER_H
#include <glib.h>
/* Modes: ascending from first token, ascending from value, add value.
 * Radix: preserve each token, decimal, hexadecimal, octal. */
gchar *insight_renumber(const gchar *text, gint mode, gint radix,
                        const gchar *value, guint *count, GError **error);
#endif
