/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Index snapshot rebuild and event publication.
 * RU: Пересборка снимка индекса и публикация событий. */


/* EN: Queue changed paths and optional symbol names for index listeners.
 * RU: Ставит изменённые пути changed и имена names в очередь подписчиков индекса. */
static void index_event_publish(GHashTable *changed, GHashTable *names)
{
    if (!changed || !g_hash_table_size(changed))
        return;
    /* EN: Share the first event's tables. Merge keys only for subsequent queued events.
     * RU: Используем таблицы первого события и объединяем ключи последующих. */
    GHashTableIter it;
    gpointer key;
    if (!wb.index_event_paths)
        wb.index_event_paths = g_hash_table_ref(changed);
    else
    {
        g_hash_table_iter_init(&it, changed);
        while (g_hash_table_iter_next(&it, &key, NULL))
            g_hash_table_add(wb.index_event_paths, g_strdup(key));
    }
    if (names)
    {
        if (!wb.index_event_names)
            wb.index_event_names = g_hash_table_ref(names);
        else
        {
            g_hash_table_iter_init(&it, names);
            while (g_hash_table_iter_next(&it, &key, NULL))
                g_hash_table_add(wb.index_event_names, g_strdup(key));
        }
    }
    if (!wb.index_event_idle)
        wb.index_event_idle = g_idle_add(index_event_dispatch, NULL);
}

/* EN: Build a replacement index from data and complete task. cancel interrupts the work. source is unused.
 * RU: Собирает новый индекс из data и завершает task. cancel отменяет работу. source не используется. */
static void snapshot_job_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    SnapshotJob *j = data;
    if (g_cancellable_is_cancelled(cancel))
    {
        g_task_return_boolean(task, FALSE);
        return;
    }
    j->result = insight_index_clone(j->base);
    gboolean ok = j->valid && j->result != NULL;
    gsize count = j->paths->len;
    gchar **paths = (gchar **)j->paths->pdata;
    for (gsize n = 0; ok && n < count && !g_cancellable_is_cancelled(cancel); n++)
    {
        gsize size = insight_index_query(j->overlay, "", FALSE, NULL, NULL, paths[n], 0, NULL, 0);
        const InsightSymbol **tags = g_new(const InsightSymbol *, size);
        InsightSymbol *rows = g_new(InsightSymbol, size);
        insight_index_query(j->overlay, "", FALSE, NULL, NULL, paths[n], 0, tags, size);
        for (gsize k = 0; k < size; k++)
            rows[k] = *tags[k];
        gsize old = insight_index_query(j->result, "", FALSE, NULL, NULL, paths[n], 0, NULL, 0);
        ok = insight_index_count(j->result) - old + size <= 250000 &&
             insight_index_update(j->result, paths[n], rows, size);
        g_free(rows);
        g_free(tags);
    }
    if (ok && !g_cancellable_is_cancelled(cancel))
        index_collect_changes(j);
    g_task_return_boolean(task, ok && !g_cancellable_is_cancelled(cancel));
}

/* EN: Publish result if it matches the current workspace. source and data are unused.
 * RU: Публикует result, если он относится к текущему проекту. source и data не используются. */
static void snapshot_job_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    SnapshotJob *j = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.index_generation)
        return;
    g_clear_object(&wb.snapshot_worker);
    if (!ok)
    {
        ui_set_statusbar(TRUE, "Index snapshot could not be rebuilt; previous snapshot retained");
        return;
    }
    /* EN: Preserve borrowed GUI pointers when symbols are unchanged.
     * RU: При неизменных символах сохраняем ссылки интерфейса. */
    if (!g_hash_table_size(j->changed))
        return;
    /* EN: Stop highlighting before swapping its name snapshot.
     * RU: Останавливаем подсветку перед заменой снимка имён. */
    if (wb.preview_links_idle)
    {
        g_source_remove(wb.preview_links_idle);
        wb.preview_links_idle = 0;
    }
    /* EN: Drop borrowed candidates before retiring the index.
     * RU: Сбрасываем ссылки на символы до замены индекса. */
    if (wb.preview_candidates)
        g_hash_table_remove_all(wb.preview_candidates);
    retire_names(wb.preview_names);
    wb.preview_names = j->all_names;
    j->all_names = NULL;
    retire_index(wb.index);
    wb.index = j->result;
    j->result = NULL;
    wb.index_version++;
    wb.preview_names_position = insight_index_count(wb.index);
    index_event_publish(j->changed, j->names);
}

/* EN: Schedule a snapshot rebuild for the current workspace when the index is dirty.
 * RU: Планирует пересборку снимка текущего проекта при изменении индекса. */
static void refresh_index(void)
{
    if (!wb.alive || wb.restore_worker || wb.scan_worker || wb.snapshot_worker ||
        !wb.base || !wb.index_dirty)
        return;
    SnapshotJob *j = g_new0(SnapshotJob, 1);
    j->base = insight_index_ref(wb.base);
    j->previous = insight_index_ref(wb.index);
    j->overlay = insight_index_new();
    j->paths = g_ptr_array_new_with_free_func(g_free);
    j->valid = j->overlay != NULL;
    j->epoch = workbench_epoch;
    j->generation = wb.index_generation;
    gchar *root = wb.workspace_from_project && wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;
    for (guint n = 0;n < documents_array->len; n++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, n);
        if (doc->is_valid && doc->file_name &&
            (!root || insight_project_files_contains(wb.membership, root, doc->file_name)))
        {
            g_ptr_array_add(j->paths, g_strdup(doc->file_name));
            if (!insight_native_import(j->overlay, doc->tm_file, doc->file_name))
                j->valid = FALSE;
        }
    }
    g_free(root);
    wb.index_dirty = FALSE;
    wb.snapshot_worker = g_cancellable_new();
    GTask *task = g_task_new(NULL, wb.snapshot_worker, snapshot_job_done, NULL);
    g_task_set_task_data(task, j, snapshot_job_free);
    g_task_run_in_thread(task, snapshot_job_run);
    g_object_unref(task);
}
