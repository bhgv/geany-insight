/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_WORKBENCH_H
#define GEANY_INSIGHT_WORKBENCH_H
#include <glib.h>
#include <gdk/gdk.h>
struct SCNotification;
struct GeanyEditor;
#define INSIGHT_TEMPLATE_COMPLETION_LIST 74
gboolean insight_workbench_complete_template(struct GeanyEditor *,gint);
void insight_workbench_template_selected(struct GeanyEditor *,const gchar *);
gboolean insight_workbench_show_file_search(void);
gboolean insight_workbench_focus_file_symbols(void);
gboolean insight_workbench_file_find_again(gboolean);
void insight_workbench_source_link_notify(struct GeanyEditor *,struct SCNotification *);
gboolean insight_workbench_source_link_key(GdkEventKey *);
void insight_workbench_symbol_edit(guint action);
/* Stable panel actions: project files/folders/symbols, relations, bookmarks, clips,
 * types, categories, file symbols, context, windows, file/project search, results, editor. */
gboolean insight_workbench_activate(guint action);
/* Symbol Info, prototype, implementation, nominal base type. */
void insight_workbench_symbol_navigation(guint action);
void insight_workbench_init(void);
void insight_workbench_finalize(void);
void insight_workbench_set_independent(gboolean active);
void insight_workbench_tools_changed(void);
void insight_workbench_refresh(void);
void insight_workbench_bookmarks_changed(void);
void insight_workbench_editor_updated(gboolean);
void insight_workbench_index_changed(void);
void insight_workbench_languages_changed(void);
void insight_workbench_locale_changed(void);
gboolean insight_workbench_goto_definition(void);
gboolean insight_workbench_browser_definition(gint position);
void insight_workbench_panel_font(void);
#endif
