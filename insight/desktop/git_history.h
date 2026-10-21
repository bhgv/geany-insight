/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_GIT_HISTORY_H
#define INSIGHT_GIT_HISTORY_H
#include <gio/gio.h>
#define INSIGHT_GIT_HISTORY_PAGE 200u
#define INSIGHT_GIT_HISTORY_TEXT_LIMIT (1024u*1024u)
typedef struct { gchar *name,*oid; } InsightGitRef;
typedef struct { gchar *oid,*author,*date,*subject,*message;GPtrArray *parents; } InsightGitRevision;
typedef struct { gchar *path,*old_oid,*new_oid;guint old_mode,new_mode;gchar status; } InsightGitDelta;
/* Read-only Git object APIs, no shell, checkout, index changes or temp files.
 * Revisions must be full 40/64-digit object IDs. Native root/path encoding.
 * Output, object, ref, file and page limits reject oversized results. */
gchar *insight_git_history_root(const gchar *,GCancellable *,GError **);
GPtrArray *insight_git_history_refs(const gchar *,GCancellable *,GError **);
GPtrArray *insight_git_history_log(const gchar *,const gchar *,guint,gboolean *,GCancellable *,GError **);
InsightGitRevision *insight_git_history_revision(const gchar *,const gchar *,GCancellable *,GError **);
void insight_git_revision_free(InsightGitRevision *);
GPtrArray *insight_git_history_delta(const gchar *,const gchar *,const gchar *,GCancellable *,GError **);
gchar *insight_git_history_text(const gchar *,const gchar *,guint,gsize *,GCancellable *,GError **);
#endif
