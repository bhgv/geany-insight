/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_TYPING_H
#define INSIGHT_TYPING_H
#include "editor.h"
void insight_typing_init(GtkWidget *edit_menu);
void insight_typing_notify(GeanyEditor *,SCNotification *);
void insight_typing_command(guint action);
#endif
