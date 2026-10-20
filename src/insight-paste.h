/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_PASTE_H
#define INSIGHT_PASTE_H
#include "editor.h"
void insight_paste_word_edit(guint action);
void insight_paste_tools_menu(GtkWidget *menu);
void insight_paste_menu(GtkWidget *menu);
void insight_paste_line_menu(GtkWidget *menu);
typedef enum {
    INSIGHT_COPY_LINE, INSIGHT_CUT_LINE, INSIGHT_COPY_LINE_RIGHT,
    INSIGHT_CUT_LINE_LEFT, INSIGHT_CUT_LINE_RIGHT, INSIGHT_CUT_SELECTION_OR_PASTE
} InsightLineClipboard;
/* FALSE leaves non-document widgets and unsupported selection modes to native commands. */
gboolean insight_line_clipboard(ScintillaObject *, InsightLineClipboard);
gboolean insight_paste_move_lines(ScintillaObject *, gint direction, guint count);
void insight_paste_notify(GeanyEditor *, SCNotification *);
/* TRUE means a guarded clipboard request or intelligent text insert was handled. */
gboolean insight_paste_clipboard(ScintillaObject *);
gboolean insight_paste_text(ScintillaObject *, const gchar *);
#endif
