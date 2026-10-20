/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_LAYOUT_STATE_H
#define INSIGHT_LAYOUT_STATE_H
#include <glib.h>
enum { IL_LEFT,IL_PROJECT,IL_CONTEXT,IL_FOLLOW,IL_WRAP,IL_FLOAT,IL_LEFT_SPLIT,IL_RIGHT_SPLIT,IL_CONTEXT_SPLIT,IL_TAB,IL_X,IL_Y,IL_WIDTH,IL_HEIGHT,IL_DETACHED,IL_HOST_VISIBLE,IL_WL_SET,IL_WL_VISIBLE,IL_WL_DOCKED,IL_WL_POSITIONED,IL_WL_MAXIMIZED,IL_WL_X,IL_WL_Y,IL_WL_WIDTH,IL_WL_HEIGHT,IL_TRAY_VISIBLE,IL_HOST_MINIMIZED,IL_FIELDS };
enum { IL_PANEL_COUNT=10, IL_FORMAT_VERSION=5 };
typedef struct { gboolean set,maximized; gint x,y,width,height; } InsightPanelLayout;
typedef struct { gboolean valid; gint values[IL_FIELDS]; InsightPanelLayout panels[IL_PANEL_COUNT]; } InsightLayout;
gboolean insight_layout_valid(const InsightLayout *);
gboolean insight_layout_read(GKeyFile *,const char *,InsightLayout *);
void insight_layout_write(GKeyFile *,const char *,const InsightLayout *);
gboolean insight_layout_load_file(const gchar *,InsightLayout *,GError **);
gboolean insight_layout_save_file(const gchar *,const InsightLayout *,GError **);
#endif
