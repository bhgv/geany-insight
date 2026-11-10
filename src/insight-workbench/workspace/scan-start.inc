/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Scan startup and project membership checks.
 * RU: Запуск сканирования и проверка состава проекта. */


/* EN: Start scanning root and return whether it could be scheduled.
 * RU: Запускает сканирование root и сообщает, удалось ли запланировать работу. */
static gboolean start_scan(const char *root)
{
    gchar *requested_root = g_strdup(root);
    hdl_invalidate_colors();
    stop_scan();
    root = requested_root;
    if (wb.workspace_from_project && insight_git_active() && !wb.git_manual)
    {
        stop_scan();
        GError *error = NULL;
        InsightProjectFiles *policy = insight_git_membership(&error);
        if (!policy)
        {
            g_free(requested_root);
            ui_set_statusbar(TRUE, "Cannot read Git project files: %s", error ? error->message : "Git is busy");
            g_clear_error(&error);
            return FALSE;
        }
        insight_project_files_free(wb.membership);
        wb.membership = policy;
    }
    gchar *owned = insight_canonicalize_filename(root, NULL);
    g_free(requested_root);
    GDir *dir = g_dir_open(owned, 0, NULL);
    InsightIndex *next = NULL;
    gboolean context_changed = database_identity(owned) || g_strcmp0(wb.root, owned) != 0;
    if (context_changed)
    {
        g_hash_table_remove_all(wb.file_stamps);
        g_hash_table_remove_all(wb.directory_stamps);
        g_hash_table_remove_all(wb.declarations);
        retire_index(wb.base);
        wb.base = insight_index_new();
        retire_index(wb.disk_calls);
        wb.disk_calls = insight_index_new();
        retire_index(wb.disk_declarations);
        wb.disk_declarations = insight_index_new();
    }
    if (dir && !wb.force_parse && (context_changed || !g_hash_table_size(wb.file_stamps)) && g_strcmp0(wb.restore_attempt_identity, wb.db_identity))
    {
        if (dir)
            g_dir_close(dir);
        if (wb.watch_idle)
        {
            g_source_remove(wb.watch_idle);
            wb.watch_idle = 0;
        }
        wb.watch_changed = FALSE;
        wb.report_scan_completed = FALSE;
        if (context_changed)
            g_ptr_array_set_size(wb.files, 0);
        insight_workspace_watch_clear(wb.watch);
        g_free(wb.root);
        wb.root = g_strdup(owned);
        restore_project_database(owned);
        g_free(owned);
        return TRUE;
    }
    next = wb.base ? insight_index_ref(wb.base):insight_index_new();
    if (!dir || !next)
    {
        if (dir)
            g_dir_close(dir);
        insight_index_free(next);
        g_free(owned);
        return FALSE;
    }
    gboolean same_root = !g_strcmp0(wb.root, owned) && (!context_changed || g_hash_table_size(wb.file_stamps)>0) && (wb.files->len || wb.report_scan_completed);
    invalidate_search_context();
    stop_scan();
    wb.scan_keep_files = same_root;
    g_ptr_array_set_size(wb.scan_members, 0);
    if (wb.watch_idle)
    {
        g_source_remove(wb.watch_idle);
        wb.watch_idle = 0;
    }
    insight_workspace_watch_clear(wb.watch);
    wb.watch_changed = FALSE;
    wb.watch_incomplete = FALSE;
    if (!wb.cache_validation) watch_directory(owned);
    retire_index(wb.base);
    wb.base = next;
    g_free(wb.root);
    wb.root = g_strdup(owned);
    if (!same_root)
        g_ptr_array_set_size(wb.files, 0);
    ScanFrame *frame = g_new0(ScanFrame, 1);
    frame->path = owned;
    frame->dir = dir;
    gboolean known_only = wb.cache_validation;
    wb.cache_validation = FALSE;
    if (known_only)
    {
        GHashTableIter it;
        gpointer path, value;
        g_hash_table_iter_init(&it, wb.directory_stamps);
        while (g_hash_table_iter_next(&it, &path, &value))
        {
            gchar *now = insight_project_db_stamp(path, TRUE);
            if (g_strcmp0(now, value)) known_only = FALSE;
            g_free(now);
        }
    }
    if (!known_only)
    {
        g_hash_table_remove_all(wb.directory_stamps);
        watch_directory(owned);
    }
    if (known_only)
    {
        GHashTableIter it;
        gpointer path;
        g_hash_table_iter_init(&it, wb.file_stamps);
        while (g_hash_table_iter_next(&it, &path, NULL))
        {
            gchar *native = utils_get_locale_from_utf8(path);
            if (g_file_test(native, G_FILE_TEST_IS_REGULAR))
                g_ptr_array_add(wb.scan_files, native);
            else g_free(native);
        }
        g_hash_table_iter_init(&it, wb.directory_stamps);
        while (g_hash_table_iter_next(&it, &path, NULL))
        {
            GError *error = NULL;
            insight_workspace_watch_add(wb.watch, path, &error);
            g_clear_error(&error);
        }
        frame_free(frame);
    }
    else if (!wb.workspace_from_project || wb.membership->automatic)
    {
        g_ptr_array_add(wb.frames, frame);
        g_hash_table_add(wb.scan_seen, g_strdup(owned));
    }
    else frame_free(frame);
    if (wb.workspace_from_project && !known_only)
    {
        for (guint i = 0;i<wb.membership->files->len;i++)
        {
            const char *path = g_ptr_array_index(wb.membership->files, i);
            gchar *native = utils_get_locale_from_utf8(path), *parent = g_path_get_dirname(native);
            g_ptr_array_add(wb.scan_files, native);
            watch_directory(parent);
            g_free(parent);
        }
        for (guint i = 0;i<wb.membership->trees->len;i++)
            g_ptr_array_add(wb.scan_trees, utils_get_locale_from_utf8(g_ptr_array_index(wb.membership->trees, i)));
    }
    g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-saved", NULL);
    g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-scanning", GINT_TO_POINTER(1));
    wb.scan_indexing = FALSE;
    wb.scan_index_position = 0;
    g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-members-ready", NULL);
    wb.scan_file_position = wb.scan_tree_position = 0;
    wb.parsed = wb.skipped = wb.reused = 0;
    wb.report_scan_completed = FALSE;
    mark_index_dirty();
    wb.scan_failed = FALSE;
    wb.scan = g_idle_add(scan_step, NULL);
    ui_set_statusbar(TRUE, wb.force_parse ? "Rebuilding project symbols…":known_only ? "Project database restored; checking file stamps…":"Synchronizing project membership and changed files…");
    return TRUE;
}
