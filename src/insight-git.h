/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_GIT_H
#define INSIGHT_GIT_H
#include <gtk/gtk.h>
#include "../insight/desktop/project_files.h"
void insight_git_bind(GKeyFile *,const gchar *root);
void insight_git_close(void);
gboolean insight_git_active(void);
InsightProjectFiles *insight_git_membership(GError **);
void insight_git_manage(GtkWidget *,gpointer);
void insight_git_install(GCallback refresh);
void insight_git_toolbar(void);
#endif
