/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "master_sync.h"
#include <string.h>
struct _InsightMasterSync { GObject parent;gboolean alive,paused;guint timer,rollbacks;guint64 generation;GCancellable *worker;gchar *project,*root,*path,*digest,*etag,*last_error,*membership_digest;InsightProjectFiles *policy;InsightMasterTypes *types;InsightMasterApply apply;InsightMasterReport report;gpointer data; };
typedef struct { GObjectClass parent; } InsightMasterSyncClass;
G_DEFINE_TYPE(InsightMasterSync,insight_master_sync,G_TYPE_OBJECT)
typedef enum { POLL,ATTACH,WRITE,ROLLBACK } Operation;
typedef struct { Operation operation;guint64 generation;gchar *project,*root,*path,*digest,*etag,*after_digest,*after_etag,*membership_digest;InsightProjectFiles *policy;InsightMasterList *list,*before;InsightMasterTypes *types;GError *error;gboolean written; } SyncJob;
static void job_free(gpointer data) { SyncJob *job=data;g_free(job->project);g_free(job->root);g_free(job->path);g_free(job->digest);g_free(job->membership_digest);g_free(job->etag);g_free(job->after_digest);g_free(job->after_etag);insight_project_files_free(job->policy);insight_master_types_free(job->types);insight_master_list_free(job->list);insight_master_list_free(job->before);g_clear_error(&job->error);g_free(job); }
static void stop_worker(InsightMasterSync *sync) { sync->generation++;if(sync->worker) { g_cancellable_cancel(sync->worker);g_clear_object(&sync->worker); } }
static void insight_master_sync_finalize(GObject *object) { InsightMasterSync *sync=(InsightMasterSync *)object;g_free(sync->project);g_free(sync->root);g_free(sync->path);g_free(sync->digest);g_free(sync->membership_digest);g_free(sync->etag);g_free(sync->last_error);insight_project_files_free(sync->policy);insight_master_types_free(sync->types);G_OBJECT_CLASS(insight_master_sync_parent_class)->finalize(object); }
static void insight_master_sync_class_init(InsightMasterSyncClass *klass) { G_OBJECT_CLASS(klass)->finalize=insight_master_sync_finalize; }
static void insight_master_sync_init(InsightMasterSync *sync) { sync->alive=TRUE; }
static gboolean same_path(const gchar *one,const gchar *two) {
 if(!one || !two || !*one || !*two) return FALSE;
 gchar *a=g_canonicalize_filename(one,NULL),*b=g_canonicalize_filename(two,NULL);
#ifdef G_OS_WIN32
 gchar *fold_a=g_utf8_casefold(a,-1),*fold_b=g_utf8_casefold(b,-1);gboolean same=!g_strcmp0(fold_a,fold_b);g_free(fold_a);g_free(fold_b);
#else
 gboolean same=!strcmp(a,b);
#endif
 g_free(a);g_free(b);return same;
}
static void report(InsightMasterSync *sync,const GError *error) { if(!sync->alive || !error || g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) return;if(!g_strcmp0(sync->last_error,error->message)) return;g_free(sync->last_error);sync->last_error=g_strdup(error->message);if(sync->report) sync->report(error,sync->data); }
static void worker(GTask *task,gpointer object,gpointer data,GCancellable *cancel) {
 (void)object;SyncJob *job=data;
 if(job->operation==ROLLBACK) { insight_master_list_restore(job->before,job->path,job->root,job->after_etag,job->after_digest,NULL,NULL,&job->error);g_task_return_boolean(task,job->error==NULL);return; }
 if(job->operation==WRITE) {
  job->before=insight_master_list_read_typed(job->path,job->root,job->types,cancel,&job->error);if(!job->before) goto done;
  if(g_strcmp0(job->membership_digest,job->before->membership_digest) || g_strcmp0(job->digest,job->before->digest) || (job->etag && g_strcmp0(job->etag,job->before->etag))) { g_set_error_literal(&job->error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG,"Master file list or its expanded file set changed; reload before applying membership");goto done; }
  job->list=insight_master_list_collect(job->policy,job->root,cancel,&job->error);if(!job->list) goto done;
  /* A list never adds itself to the project through automatic enumeration. */
  for(guint i=job->list->files->len;i>0;i--) { if(same_path(g_ptr_array_index(job->list->files,i-1),job->path)) g_ptr_array_remove_index(job->list->files,i-1); }
  insight_master_list_rehash(job->list);
  gchar *text=insight_master_list_render(job->list,job->root,&job->error);if(!text) goto done;job->after_digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);g_free(text);
  if(!insight_master_list_save(job->list,job->path,job->root,job->before->etag,job->before->digest,cancel,&job->after_etag,&job->error)) goto done;
  job->written=TRUE;InsightProjectFiles *policy=insight_master_list_policy(job->list,job->policy);g_ptr_array_set_size(policy->excluded_files,0);g_ptr_array_set_size(policy->excluded_trees,0);insight_project_files_free(job->policy);job->policy=policy;
 }else { job->list=insight_master_list_read_typed(job->path,job->root,job->types,cancel,&job->error);if(job->list) { InsightProjectFiles *policy=insight_master_list_policy(job->list,job->policy);insight_project_files_free(job->policy);job->policy=policy; } }
 done:g_task_return_boolean(task,job->error==NULL);
}
static void done(GObject *,GAsyncResult *,gpointer);
static void request_read(InsightMasterSync *,gboolean);
static void launch(InsightMasterSync *sync,SyncJob *job,gboolean rollback) {
 GCancellable *cancel=rollback ? NULL : (sync->worker=g_cancellable_new());GTask *task=g_task_new(sync,cancel,done,NULL);g_task_set_task_data(task,job,job_free);g_task_run_in_thread(task,worker);g_object_unref(task);
}
static void rollback(InsightMasterSync *sync,SyncJob *job) {
 sync->rollbacks++;SyncJob *undo=g_new0(SyncJob,1);undo->operation=ROLLBACK;undo->generation=job->generation;undo->path=g_strdup(job->path);undo->root=g_strdup(job->root);undo->before=job->before;job->before=NULL;undo->after_etag=g_strdup(job->after_etag);undo->after_digest=g_strdup(job->after_digest);launch(sync,undo,TRUE);
}
static void done(GObject *object,GAsyncResult *result,gpointer data) {
 (void)data;InsightMasterSync *sync=(InsightMasterSync *)object;GTask *task=G_TASK(result);SyncJob *job=g_task_get_task_data(task);gboolean ok=g_task_propagate_boolean(task,NULL);
 if(job->operation==ROLLBACK) { sync->rollbacks--;if(job->error) report(sync,job->error);else { GError *notice=g_error_new_literal(G_IO_ERROR,G_IO_ERROR_FAILED,"Project membership was not committed; master file list restored");report(sync,notice);g_error_free(notice); }request_read(sync,FALSE);return; }
 if(!sync->alive || job->generation!=sync->generation) { if(job->written) rollback(sync,job);return; }
 g_clear_object(&sync->worker);if(!ok || job->error) { report(sync,job->error);if(job->written) rollback(sync,job);return; }
 gboolean changed=job->operation!=POLL || g_strcmp0(sync->digest,job->list->digest) || g_strcmp0(sync->membership_digest,job->list->membership_digest);
 if(changed && (!sync->apply || !sync->apply(job->policy,sync->data))) { if(job->written) rollback(sync,job);else { GError *error=g_error_new_literal(G_IO_ERROR,G_IO_ERROR_FAILED,"Project rejected master file list membership");report(sync,error);g_error_free(error); }return; }
 if(!sync->alive || job->generation!=sync->generation) { if(job->written) rollback(sync,job);return; }
 g_free(sync->membership_digest);sync->membership_digest=g_strdup(job->list->membership_digest);g_clear_pointer(&sync->last_error,g_free);g_free(sync->digest);sync->digest=g_strdup(job->written ? job->after_digest : job->list->digest);g_free(sync->etag);sync->etag=g_strdup(job->written ? job->after_etag : job->list->etag);
 if(changed) { insight_project_files_free(sync->policy);sync->policy=insight_project_files_clone(job->policy);g_free(sync->path);sync->path=g_strdup(job->policy->master_file); }
}
static SyncJob *prepare(InsightMasterSync *sync,Operation operation,const InsightProjectFiles *policy) { SyncJob *job=g_new0(SyncJob,1);job->generation=sync->generation;job->operation=operation;job->project=g_strdup(sync->project);job->root=g_strdup(sync->root);job->policy=insight_project_files_clone(policy);job->types=insight_master_types_copy(sync->types);job->path=g_strdup(policy->master_file);job->membership_digest=g_strdup(sync->membership_digest);job->digest=g_strdup(sync->digest);job->etag=g_strdup(sync->etag);return job; }
static void request_read(InsightMasterSync *sync,gboolean explicit) { if(explicit && sync) sync->paused=FALSE;if(!sync || !sync->alive || sync->paused || sync->worker || sync->rollbacks || !sync->project || !sync->root || !sync->policy || !sync->path || !*sync->path) return;if(same_path(sync->path,sync->project)) { GError *error=g_error_new_literal(G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Master file list must differ from project configuration");report(sync,error);g_error_free(error);return; }launch(sync,prepare(sync,POLL,sync->policy),FALSE); }
void insight_master_sync_set_types(InsightMasterSync *sync,const InsightMasterTypes *types) { if(!sync || !sync->alive) return;stop_worker(sync);insight_master_types_free(sync->types);sync->types=insight_master_types_copy(types);sync->paused=FALSE;request_read(sync,FALSE); }
void insight_master_sync_poll(InsightMasterSync *sync) { request_read(sync,TRUE); }
void insight_master_sync_pause(InsightMasterSync *sync) { if(sync && sync->alive) { sync->paused=TRUE;stop_worker(sync); } }
static gboolean tick(gpointer data) { request_read(data,FALSE);return G_SOURCE_CONTINUE; }
InsightMasterSync *insight_master_sync_new(guint interval,InsightMasterApply apply,InsightMasterReport notify,gpointer data) { InsightMasterSync *sync=g_object_new(insight_master_sync_get_type(),NULL);sync->apply=apply;sync->report=notify;sync->data=data;sync->timer=g_timeout_add(MAX(interval,50),tick,sync);return sync; }
void insight_master_sync_set(InsightMasterSync *sync,const gchar *project,const gchar *root,const InsightProjectFiles *policy) {
 if(!sync || !sync->alive) return;
 stop_worker(sync);gboolean changed=g_strcmp0(sync->project,project) || g_strcmp0(sync->root,root) || g_strcmp0(sync->path,policy ? policy->master_file : NULL);
 g_free(sync->project);sync->project=g_strdup(project);g_free(sync->root);sync->root=g_strdup(root);g_free(sync->path);sync->path=g_strdup(policy ? policy->master_file : NULL);insight_project_files_free(sync->policy);sync->policy=policy ? insight_project_files_clone(policy) : NULL;
 if(changed) { g_clear_pointer(&sync->membership_digest,g_free);g_clear_pointer(&sync->digest,g_free);g_clear_pointer(&sync->etag,g_free);g_clear_pointer(&sync->last_error,g_free); }
 insight_master_sync_poll(sync);
}
gboolean insight_master_sync_attach(InsightMasterSync *sync,const InsightProjectFiles *policy,GError **error) {
 if(!sync || !sync->alive || sync->rollbacks || !sync->project || !sync->root || !policy || !policy->master_file || !*policy->master_file) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Open a project and choose an existing master file list");return FALSE; }
 if(same_path(policy->master_file,sync->project)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Master file list must differ from project configuration");return FALSE; }
 stop_worker(sync);sync->paused=FALSE;launch(sync,prepare(sync,ATTACH,policy),FALSE);return TRUE;
}
gboolean insight_master_sync_write(InsightMasterSync *sync,const InsightProjectFiles *policy,GError **error) {
 if(!sync || !sync->alive || sync->worker || sync->rollbacks || !sync->digest || !policy || g_strcmp0(sync->path,policy->master_file)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_PENDING,"Wait for master file list synchronization before applying membership");return FALSE; }
 if(same_path(policy->master_file,sync->project)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Master file list must differ from project configuration");return FALSE; }
 stop_worker(sync);sync->paused=FALSE;launch(sync,prepare(sync,WRITE,policy),FALSE);return TRUE;
}
void insight_master_sync_free(InsightMasterSync *sync) { if(!sync) return;sync->alive=FALSE;if(sync->timer) { g_source_remove(sync->timer);sync->timer=0; }stop_worker(sync);sync->apply=NULL;sync->report=NULL;sync->data=NULL;g_object_unref(sync); }
