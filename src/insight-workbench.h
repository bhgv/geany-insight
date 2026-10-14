/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_WORKBENCH_H
#define GEANY_INSIGHT_WORKBENCH_H
#include <glib.h>
void insight_workbench_init(void);
void insight_workbench_finalize(void);
void insight_workbench_refresh(void);
void insight_workbench_bookmarks_changed(void);
void insight_workbench_editor_updated(gboolean);
void insight_workbench_index_changed(void);
gboolean insight_workbench_goto_definition(void);
#endif
