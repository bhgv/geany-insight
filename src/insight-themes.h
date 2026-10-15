/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_THEMES_H
#define INSIGHT_THEMES_H
#include <gtk/gtk.h>
#include "Scintilla.h"
#include "ScintillaWidget.h"
enum { IT_FONT=1,IT_SIZE=2,IT_WEIGHT=4,IT_ITALIC=8,IT_UNDERLINE=16,IT_FORE=32,IT_BACK=64,IT_SCALE=128 };
typedef struct { guint mask;gchar *font;gint size,weight,italic,underline,fore,back,scale; } InsightThemeStyle;
void insight_themes_init(void);
void insight_themes_finalize(void);
void insight_themes_menu(GtkWidget *);
/* Returned font is borrowed until the next theme change. */
InsightThemeStyle insight_themes_style(ScintillaObject *,gint);
#endif
