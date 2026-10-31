/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_PROJECT_FILES_H
#define INSIGHT_PROJECT_FILES_H
#include <gio/gio.h>
typedef struct {
 gboolean automatic;
 gchar *masks,*master_file,*master_expression;
 GPtrArray *files,*trees,*excluded_files,*excluded_trees;
} InsightProjectFiles;
InsightProjectFiles *insight_project_files_new(void);
InsightProjectFiles *insight_project_files_clone(const InsightProjectFiles *);
void insight_project_files_free(InsightProjectFiles *);
/* Atomically freeze a complete, caller-captured file snapshot. Preserve explicit
 * missing files, masks and exclusions; expand trees only through supplied paths.
 * No disk reads. Master-list projects and invalid/oversized snapshots decline. */
InsightProjectFiles *insight_project_files_freeze(const InsightProjectFiles *,const gchar *,const GPtrArray *,GError **);
gboolean insight_project_files_set(InsightProjectFiles *,const char *,gboolean,gboolean,GError **);
gboolean insight_project_files_contains(const InsightProjectFiles *,const char *,const char *);
/* Project/system directory variables and case-insensitive environment names
 * expand once against an environment snapshot; expressions stay portable.
 * Unknown, empty, conflicting-case or invalid UTF-8/line-break values decline. */
gchar *insight_project_files_escape_master(const gchar *);
/* One predefined path variable, without path normalization or expansion of
 * its value. Unavailable platform/project variables return NULL. */
gchar *insight_project_path_variable(const gchar *,const gchar *,const gchar *,GError **);
gboolean insight_project_files_set_master(InsightProjectFiles *,const gchar *,const gchar *,const gchar *,GError **);
InsightProjectFiles *insight_project_files_read_project(GKeyFile *,const char *,const char *,GError **);
gboolean insight_project_files_write(const InsightProjectFiles *,GKeyFile *,const char *,GError **);
InsightProjectFiles *insight_project_files_read(GKeyFile *,const char *,GError **);
#endif
