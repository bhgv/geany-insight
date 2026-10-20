/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_TEXT_BOUNDARY_H
#define INSIGHT_TEXT_BOUNDARY_H
#include <glib.h>
/* mode: smart beginning, smart end, next ASCII period (inclusive).
 * UTF-8 byte positions; space/tab are line-edge whitespace. */
gboolean insight_text_boundary(const gchar *text,gsize length,gint position,
                               gint mode,gint *target,GError **error);
#endif
