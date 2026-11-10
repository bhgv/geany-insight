/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Saving and publishing the project index cache.
 * RU: Сохранение и публикация кэша индекса проекта. */

typedef struct
{
    InsightIndex *index;
    InsightProjectDb db;
    gchar *path, *root, *identity, *policy;
    guint64 epoch, generation;
} CacheJob;
static GMutex cache_write_mutex;

/* EN: Release the cache job and its resources from data.
 * RU: Освобождает задачу кэша и ресурсы из data. */
static void cache_job_free(gpointer data)
{
    CacheJob *j = data;
    retire_index(j->index);
    insight_project_db_clear(&j->db);
    g_free(j->path);
    g_free(j->root);
    g_free(j->identity);
    g_free(j->policy);
    g_free(j);
}

/* EN: Save data under the cache lock and complete task. cancel interrupts the save. source is unused.
 * RU: Сохраняет data под блокировкой и завершает task. cancel прерывает запись. source не используется. */
static void cache_job_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    CacheJob *j = data;
    GError *error = NULL;
    /* EN: Serialize writes so cancelled jobs cannot replace a newer cache.
     * RU: Выполняем записи по очереди, чтобы отменённая задача не перезаписала новый кэш. */
    g_mutex_lock(&cache_write_mutex);
    gboolean ok = !g_cancellable_set_error_if_cancelled(cancel, &error);
    if (ok)
    {
        gchar *directory = g_path_get_dirname(j->path);
        if (g_mkdir_with_parents(directory, 0700))
        {
            ok = FALSE;
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to create index cache directory");
        }
        g_free(directory);
    }
    if (ok) ok = insight_project_db_save(j->path, j->identity, j->root, j->policy, &j->db, cancel, &error);
    g_mutex_unlock(&cache_write_mutex);
    if (error)
        g_task_return_error(task, error);
    else g_task_return_boolean(task, ok);
}

/* EN: Report result if it still belongs to the current scan. source and data are unused.
 * RU: Сообщает result, если он относится к текущему сканированию. source и data не используются. */
static void cache_job_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    CacheJob *j = g_task_get_task_data(task);
    GError *error = NULL;
    gboolean ok = g_task_propagate_boolean(task, &error);
    if (wb.alive && j->epoch == workbench_epoch && j->generation == wb.scan_generation)
    {
        if (wb.cache_worker == g_task_get_cancellable(task))
            g_clear_object(&wb.cache_worker);
        if (ok)
            g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-saved", GINT_TO_POINTER(1));
        if (!ok && error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) ui_set_statusbar(TRUE, "Workspace cache not saved: %s", error->message);
    }
    g_clear_error(&error);
}

/* EN: Queue a save of the current workspace index.
 * RU: Ставит сохранение текущего индекса в очередь. */
static void save_index_cache(void)
{
    if (!wb.alive || wb.restore_worker || !wb.base || !wb.root || !wb.cache_path) return;
    if (wb.cache_worker)
    {
        g_cancellable_cancel(wb.cache_worker);
        g_clear_object(&wb.cache_worker);
    }
    CacheJob *j = g_new0(CacheJob, 1);
    j->index = NULL;
    j->db.symbols = insight_index_ref(wb.base);
    j->db.calls = insight_index_ref(wb.disk_calls);
    j->db.declarations = insight_index_ref(wb.disk_declarations);
    j->db.files = insight_project_db_copy_table(wb.file_stamps);
    j->db.directories = insight_project_db_copy_table(wb.directory_stamps);
    j->identity = g_strdup(wb.db_identity);
    j->policy = g_strdup(wb.db_policy);
    j->path = g_strdup(wb.cache_path);
    j->root = g_strdup(wb.root);
    j->epoch = workbench_epoch;
    j->generation = wb.scan_generation;
    wb.cache_worker = g_cancellable_new();
    GTask *task = g_task_new(NULL, wb.cache_worker, cache_job_done, NULL);
    g_task_set_task_data(task, j, cache_job_free);
    g_task_run_in_thread(task, cache_job_run);
    g_object_unref(task);
}
