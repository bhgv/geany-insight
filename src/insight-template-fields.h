/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_TEMPLATE_FIELDS_H
#define INSIGHT_TEMPLATE_FIELDS_H
#include <gtk/gtk.h>
#include "Scintilla.h"
#include "ScintillaWidget.h"
#include "../insight/desktop/snippet.h"
/* Ranges are copied; Scintilla owns the session until its destruction. */
void insight_template_fields_begin(ScintillaObject *,const GArray *,gsize,gsize);
gboolean insight_template_fields_key(GtkWidget *,GdkEventKey *);
#endif
