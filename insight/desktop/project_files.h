/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_PROJECT_FILES_H
#define INSIGHT_PROJECT_FILES_H
#include <gio/gio.h>
typedef struct {
 gboolean automatic;
 gchar *masks;
 GPtrArray *files,*trees,*excluded_files,*excluded_trees;
} InsightProjectFiles;
InsightProjectFiles *insight_project_files_new(void);
InsightProjectFiles *insight_project_files_clone(const InsightProjectFiles *);
void insight_project_files_free(InsightProjectFiles *);
gboolean insight_project_files_set(InsightProjectFiles *,const char *,gboolean,gboolean,GError **);
gboolean insight_project_files_contains(const InsightProjectFiles *,const char *,const char *);
gboolean insight_project_files_write(const InsightProjectFiles *,GKeyFile *,const char *,GError **);
InsightProjectFiles *insight_project_files_read(GKeyFile *,const char *,GError **);
#endif
