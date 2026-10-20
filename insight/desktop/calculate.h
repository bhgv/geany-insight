/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CALCULATE_H
#define INSIGHT_CALCULATE_H
#include <glib.h>
/* Decimal/scientific IEEE double expressions, no evaluation of external code.
 * On failure byte_offset identifies the parser position; caller owns result. */
gchar *insight_calculate(const gchar *expression,guint *byte_offset,GError **error);
#endif
