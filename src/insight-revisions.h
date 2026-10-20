/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_REVISIONS_H
#define INSIGHT_REVISIONS_H
#include "editor.h"
void insight_revisions_init(GtkWidget *edit_menu);
void insight_revisions_notify(GeanyEditor *, SCNotification *);
void insight_revisions_show(void);
void insight_revisions_original(void);
#endif
