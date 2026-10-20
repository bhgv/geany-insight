/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SELECTION_SEARCH_H
#define INSIGHT_SELECTION_SEARCH_H
#include "editor.h"
gboolean insight_selection_search_owns_pattern(void);
gboolean insight_selection_search_show_loaded(void);
gboolean insight_selection_search_find_again(gboolean change_direction);
void insight_selection_search_init(void);
void insight_selection_search_command(guint action);
void insight_selection_search_notify(GeanyEditor *,SCNotification *);
#endif
