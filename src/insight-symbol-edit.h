/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_NATIVE_SYMBOL_EDIT_H
#define INSIGHT_NATIVE_SYMBOL_EDIT_H
#include <glib.h>
/* Range comes from a fresh parser/mask snapshot; end is exclusive. */
void insight_symbol_edit_range(guint,gint,gint,guint);
#endif
