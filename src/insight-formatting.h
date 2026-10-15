/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_FORMATTING_H
#define INSIGHT_FORMATTING_H
#include <gtk/gtk.h>
#include "Scintilla.h"
#include "ScintillaWidget.h"
void insight_formatting_init(void);
void insight_formatting_refresh(void);
void insight_formatting_finalize(void);
/* Call begin before changing the underlying theme/profile fonts. Apply captures
 * that raw result once, then renders reversible display overrides only. */
void insight_formatting_begin(ScintillaObject *);
void insight_formatting_apply(ScintillaObject *);
void insight_formatting_menu(GtkWidget *view,GtkWidget *options);
#endif
