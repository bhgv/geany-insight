/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_LAYOUT_STATE_H
#define INSIGHT_LAYOUT_STATE_H
#include <glib.h>
enum { IL_LEFT,IL_PROJECT,IL_CONTEXT,IL_FOLLOW,IL_WRAP,IL_FLOAT,IL_LEFT_SPLIT,IL_RIGHT_SPLIT,IL_CONTEXT_SPLIT,IL_TAB,IL_X,IL_Y,IL_WIDTH,IL_HEIGHT,IL_FIELDS };
typedef struct { gboolean valid; gint values[IL_FIELDS]; } InsightLayout;
gboolean insight_layout_valid(const InsightLayout *);
gboolean insight_layout_read(GKeyFile *,const char *,InsightLayout *);
void insight_layout_write(GKeyFile *,const char *,const InsightLayout *);
#endif
