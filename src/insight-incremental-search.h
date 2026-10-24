/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_INCREMENTAL_SEARCH_H
#define INSIGHT_INCREMENTAL_SEARCH_H
#include "editor.h"
void insight_incremental_search_init(void);
void insight_incremental_search_command(gboolean backwards);
gboolean insight_incremental_search_key(GdkEventKey *event);
void insight_incremental_search_notify(GeanyEditor *editor, SCNotification *notification);
#endif
