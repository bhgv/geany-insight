/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_PROJECT_DB_H
#define INSIGHT_PROJECT_DB_H
#include <gio/gio.h>
#include "index.h"
/* One atomically replaced project database: symbols, declaration rows, lexical
 * calls, all member paths (including empty/non-source files) and directory stamps. */
typedef struct { gchar *policy;InsightIndex *symbols,*declarations,*calls;GHashTable *files,*directories; } InsightProjectDb;
void insight_project_db_clear(InsightProjectDb *);
GHashTable *insight_project_db_copy_table(GHashTable *);
gchar *insight_project_db_path(const gchar *,const gchar *);
gchar *insight_project_db_stamp(const gchar *,gboolean);
gboolean insight_project_db_save(const gchar *,const gchar *,const gchar *,const gchar *,const InsightProjectDb *,GCancellable *,GError **);
gboolean insight_project_db_load(const gchar *,const gchar *,const gchar *,const gchar *,InsightProjectDb *,GError **);
#endif
