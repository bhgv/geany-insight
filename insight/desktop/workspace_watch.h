/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_WORKSPACE_WATCH_H
#define INSIGHT_WORKSPACE_WATCH_H
#include <gio/gio.h>
typedef struct InsightWorkspaceWatch InsightWorkspaceWatch;
typedef void (*InsightWorkspaceChanged)(gpointer);
InsightWorkspaceWatch *insight_workspace_watch_new(guint, InsightWorkspaceChanged, gpointer);
gboolean insight_workspace_watch_add(InsightWorkspaceWatch *,const char *,GError **);
void insight_workspace_watch_clear(InsightWorkspaceWatch *);
void insight_workspace_watch_free(InsightWorkspaceWatch *);
/* Drains coalesced native paths; caller owns array and strings. */
GPtrArray *insight_workspace_watch_take_changes(InsightWorkspaceWatch *);
guint insight_workspace_watch_count(const InsightWorkspaceWatch *);
#endif
