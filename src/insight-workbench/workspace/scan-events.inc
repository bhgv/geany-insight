/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Scan cancellation and workspace change handling.
 * RU: Отмена сканирования и обработка изменений рабочей области. */


/* EN: Cancel active scans and clear their temporary workspace state.
 * RU: Отменяет сканирование и очищает временное состояние. */
static void stop_scan(void)
{
    if (wb.scan || wb.scan_worker || wb.restore_worker)
        wb.report_scan_completed = FALSE;
    stop_definition();
    stop_file_preview();
    wb.scan_generation++;
    if (wb.restore_worker)
    {
        g_cancellable_cancel(wb.restore_worker);
        g_clear_object(&wb.restore_worker);
        if (wb.alive)
        {
            g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-loading", NULL);
            g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-scanning", NULL);
        }
    }
    if (wb.cache_worker)
    {
        g_cancellable_cancel(wb.cache_worker);
        g_clear_object(&wb.cache_worker);
    }
    if (wb.scan_worker)
    {
        g_cancellable_cancel(wb.scan_worker);
        g_clear_object(&wb.scan_worker);
        if (wb.alive && !wb.base)
            wb.base = wb.scan_backup ? insight_index_ref(wb.scan_backup):insight_index_new();
    }
    retire_index(wb.scan_backup);
    wb.scan_backup = NULL;
    if (wb.scan)
    {
        g_source_remove(wb.scan);
        wb.scan = 0;
    }
    if (wb.frames)
        g_ptr_array_set_size(wb.frames, 0);
    if (wb.scan_files)
        g_ptr_array_set_size(wb.scan_files, 0);
    if (wb.scan_trees)
        g_ptr_array_set_size(wb.scan_trees, 0);
    if (wb.scan_seen)
        g_hash_table_remove_all(wb.scan_seen);
}
static gboolean source_dialect(GeanyFiletype *, InsightCodeDialect *);
static gchar *reference_snippet(const char *, gsize);

/* EN: Process a deferred workspace change. data is unused.
 * RU: Обрабатывает отложенное изменение проекта. data не используется. */
static gboolean rescan_changed(gpointer data)
{
    (void)data;
    wb.watch_idle = 0;
    workspace_changed(NULL);
    return G_SOURCE_REMOVE;
}

/* EN: Rescan after workspace changes or defer if busy. data is unused.
 * RU: Пересканирует проект или откладывает работу, если индекс занят. data не используется. */
static void workspace_changed(gpointer data)
{
    (void)data;
    if (!wb.root) return;
    if (wb.scan || wb.scan_worker || wb.restore_worker)
    {
        wb.watch_changed = TRUE;
        return;
    }
    GPtrArray *paths = insight_workspace_watch_take_changes(wb.watch);
    gboolean membership = FALSE;
    for (guint n = 0; n < paths->len; n++)
    {
        const gchar *native = g_ptr_array_index(paths, n);
        gchar *utf8 = utils_get_utf8_from_locale(native);
        if (!g_hash_table_contains(wb.file_stamps, utf8) || !g_file_test(native, G_FILE_TEST_IS_REGULAR)) membership = TRUE;
        g_free(utf8);
    }
    g_ptr_array_unref(paths);
    if (!membership && g_hash_table_size(wb.file_stamps))
        wb.cache_validation = TRUE;
    if (!start_scan(wb.root)) ui_set_statusbar(TRUE, "Workspace changed but source folder is unavailable; previous index retained");
}
