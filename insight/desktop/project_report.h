/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_PROJECT_REPORT_H
#define INSIGHT_PROJECT_REPORT_H
#include <gio/gio.h>
#include "../core/index.h"
#define INSIGHT_REPORT_LIMIT (4u*1024u*1024u)
#define INSIGHT_REPORT_FILE_LIMIT 10000u
#define INSIGHT_REPORT_SYMBOL_LIMIT 100000u
typedef struct {
    gint refs;
    gchar *project,*root,*project_file;
    GPtrArray *paths;
    InsightIndex *index;
    guint skipped;
    gboolean incomplete;
} InsightReportSnapshot;
typedef struct {
    gboolean file_list,include_symbols,line_numbers,sort_names,relative_paths;
    GHashTable *kinds; /* NULL=all, otherwise exact UTF-8 kind names; empty=none. */
} InsightReportOptions;
/* Copies project/path strings and retains an immutable index reference.
 * No file reads or writes occur. Snapshots must not be mutated after publication. */
InsightReportSnapshot *insight_report_snapshot_new(const gchar *,const gchar *,const gchar *,GPtrArray *,InsightIndex *,guint,gboolean,GError **);
InsightReportSnapshot *insight_report_snapshot_ref(InsightReportSnapshot *);
void insight_report_snapshot_unref(InsightReportSnapshot *);
gchar *insight_project_report_build(const InsightReportSnapshot *,const InsightReportOptions *,GCancellable *,gsize *,GError **);
/* Native absolute destination and native open paths. Protects captured source
 * names, the project file and open-file identities; rejects linked parents. */
gboolean insight_project_report_save(const InsightReportSnapshot *,const gchar *,const gchar *,gsize,GPtrArray *,GError **);
#endif
