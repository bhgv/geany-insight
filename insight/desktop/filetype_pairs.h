/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_FILETYPE_PAIRS_H
#define INSIGHT_FILETYPE_PAIRS_H
#include <gtk/gtk.h>
#include "Scintilla.h"
#include "ScintillaWidget.h"
/* Per-widget transient markers; offsets are UTF-8 byte positions. */
void insight_filetype_pairs_attach(ScintillaObject *);
void insight_filetype_pairs_remember(ScintillaObject *,gint,gint);
void insight_filetype_pairs_clear(ScintillaObject *);
gboolean insight_filetype_pairs_key(ScintillaObject *,guint,GdkModifierType,gboolean);
#endif
