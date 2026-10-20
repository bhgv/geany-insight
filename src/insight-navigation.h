/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_NAVIGATION_H
#define INSIGHT_NAVIGATION_H
#include "editor.h"
void insight_navigation_menu(GtkWidget *);
void insight_navigation_attach(ScintillaObject *);
void insight_navigation_delimiter(guint action);
void insight_navigation_notify(GeanyEditor *, SCNotification *);
gboolean insight_navigation_key(GeanyEditor *, GdkEventKey *);
/* Used only by navigation wrappers. FALSE preserves their native behavior. */
gboolean insight_navigation_extend_to(ScintillaObject *, gint position);
#endif
