/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_FOLDER_TREE_H
#define INSIGHT_FOLDER_TREE_H
#include <gtk/gtk.h>
/* Columns 0/1 retain the Folder Browser name/path contract. Placeholder rows
 * have no path and must never be used as navigation destinations. */
enum { INSIGHT_FOLDER_NAME,INSIGHT_FOLDER_PATH,INSIGHT_FOLDER_STATE,INSIGHT_FOLDER_TOOLTIP,INSIGHT_FOLDER_COLUMNS };
enum { INSIGHT_FOLDER_UNKNOWN,INSIGHT_FOLDER_LOADING,INSIGHT_FOLDER_READY,INSIGHT_FOLDER_ERROR,INSIGHT_FOLDER_LIMITED,INSIGHT_FOLDER_PLACEHOLDER };
typedef enum { INSIGHT_FOLDER_EXPAND,INSIGHT_FOLDER_COLLAPSE,INSIGHT_FOLDER_REFRESH,INSIGHT_FOLDER_MORE,INSIGHT_FOLDER_CANCEL,INSIGHT_FOLDER_RESET } InsightFolderAction;
void insight_folder_tree_install(GtkTreeView *,GtkTreeStore *);
gboolean insight_folder_tree_is_updating(GtkTreeView *);
void insight_folder_tree_current(GtkTreeView *,const gchar *,gboolean hidden,gboolean refresh);
void insight_folder_tree_action(GtkTreeView *,InsightFolderAction);
void insight_folder_tree_cancel(GtkTreeView *);
void insight_folder_tree_shutdown(GtkTreeView *);
#endif
