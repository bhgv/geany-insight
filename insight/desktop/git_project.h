/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_GIT_PROJECT_H
#define INSIGHT_GIT_PROJECT_H
#include <gio/gio.h>
typedef struct { gchar *path,*snapshot; gchar x,y; } InsightGitChange;
/* Paths in this API use the native filename encoding, relative to root. */
GBytes *insight_git_run(const gchar *root,const gchar *index,const gchar *const *args,GCancellable *,GError **);
GPtrArray *insight_git_status(const gchar *root,GCancellable *,GError **);
GPtrArray *insight_git_files(const gchar *root,GCancellable *,GError **);
gboolean insight_git_paths(const gchar *root,GPtrArray *paths,gboolean add,GCancellable *,GError **);
gboolean insight_git_commit(const gchar *root,GPtrArray *changes,const gchar *message,GError **);
gchar *insight_git_message(GPtrArray *changes);
#endif
