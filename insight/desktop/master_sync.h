/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_MASTER_SYNC_H
#define INSIGHT_MASTER_SYNC_H
#include "master_list.h"
typedef struct _InsightMasterSync InsightMasterSync;
/* Called on the main context; policy is borrowed for the call. TRUE means
 * the project configuration accepted/persisted it. Reports are borrowed too. */
typedef gboolean (*InsightMasterApply)(const InsightProjectFiles *,gpointer);
typedef void (*InsightMasterReport)(const GError *,gpointer);
InsightMasterSync *insight_master_sync_new(guint,InsightMasterApply,InsightMasterReport,gpointer);
/* Copies the immutable known-type snapshot and resynchronizes membership. */
void insight_master_sync_set_types(InsightMasterSync *,const InsightMasterTypes *);
void insight_master_sync_set(InsightMasterSync *,const gchar *,const gchar *,const InsightProjectFiles *);
void insight_master_sync_poll(InsightMasterSync *);
/* Pause background reads and cancel pending work; explicit poll/set/apply resumes. */
void insight_master_sync_pause(InsightMasterSync *);
/* ATTACH reads an existing list, never overwrites it. WRITE requires a loaded
 * snapshot, flattens directory rules to eligible file entries, and rolls the
 * list back if the project refuses its new membership. Conflicting external
 * edits prevent both writing and rollback. Calls clone their input. */
gboolean insight_master_sync_attach(InsightMasterSync *,const InsightProjectFiles *,GError **);
gboolean insight_master_sync_write(InsightMasterSync *,const InsightProjectFiles *,GError **);
void insight_master_sync_free(InsightMasterSync *);
#endif
