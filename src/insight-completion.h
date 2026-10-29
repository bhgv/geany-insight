/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_COMPLETION_H
#define INSIGHT_COMPLETION_H
#include "editor.h"
gboolean insight_completion_case_sensitive(void);
void insight_completion_search_limited(GeanyEditor *,gboolean);
gint insight_completion_min_chars(void);
gboolean insight_completion_inside_comments(void);
GtkWidget *insight_completion_view_new(GtkListStore *);
gboolean insight_completion_semantic(GeanyEditor *,gsize,gboolean);
gboolean insight_completion_show(GeanyEditor *,const GPtrArray *,gsize);
gboolean insight_completion_key(GeanyEditor *,GdkEventKey *);
void insight_completion_notify(GeanyEditor *,SCNotification *);
void insight_completion_cancel(GeanyEditor *);
void insight_completion_menu(GtkWidget *);
#endif
