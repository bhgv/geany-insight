/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Incremental workspace traversal and source scheduling.
 * RU: Поэтапный обход проекта и планирование индексации. */

/* EN: Return whether the scan can keep traversing directories.
 * RU: Сообщает, может ли сканирование продолжить обход каталогов. */
static gboolean scan_continue(void)
{
    if (wb.scan_worker)
    {
        wb.scan = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Check path and schedule indexing when needed.
 * RU: Проверяет path и при необходимости запускает индексацию. */
static void scan_source_file(const char *path)
{
    if (!wb.scan_indexing)
    {
        if (g_hash_table_contains(wb.scan_seen, path)) return;
        g_hash_table_add(wb.scan_seen, g_strdup(path));
    }
    gchar *utf8 = utils_get_utf8_from_locale(path), *root = utils_get_utf8_from_locale(wb.root);
    if (wb.workspace_from_project && !insight_project_files_contains(wb.membership, root, utf8))
    {
        g_free(root);
        g_free(utf8);
        return;
    }
    g_free(root);
    if (!wb.scan_indexing)
    {
        GStatBuf st;
        if (!g_lstat(path, &st) && S_ISREG(st.st_mode))
        {
            g_ptr_array_add(wb.scan_members, g_strdup(utf8));
            if (!wb.scan_keep_files)
            {
                g_ptr_array_add(wb.files, g_strdup(utf8));
                if (wb.files->len == 1 || wb.files->len%100 == 0) files_filter(NULL, NULL);
            }
        }
        g_free(utf8);
        return;
    }
    GeanyFiletype *ft = filetypes_detect_from_extension(utf8);
    /* A cached shebang language avoids reopening extensionless source on every
  * validation. Detect content only for a genuinely new/changed file. */
    const gchar *stored = g_hash_table_lookup(wb.file_stamps, utf8);
    if (ft && ft->id == GEANY_FILETYPES_NONE && stored)
    {
        const gchar *colon = strchr(stored, ':');
        if (colon)
        {
            gchar *language = g_strndup(stored, colon-stored);
            GeanyFiletype *cached = filetypes_lookup_by_name(language);
            if (cached) ft = cached;
            g_free(language);
        }
    }
    gchar *stamp = source_stamp(path, ft ? ft->name:NULL, file_prefs.default_open_encoding);
    if (!wb.force_parse && stamp && !g_strcmp0(stamp, g_hash_table_lookup(wb.file_stamps, utf8)) && (!insight_clang_available() || !insight_clang_source(utf8) || insight_clang_dependencies_fresh(wb.disk_calls, utf8)))
    {
        wb.reused++;
        g_free(stamp);
        g_free(utf8);
        return;
    }
    ft = filetypes_detect_from_file(utf8);
    g_free(stamp);
    stamp = source_stamp(path, ft ? ft->name:NULL, file_prefs.default_open_encoding);
    IndexJob *j = g_new0(IndexJob, 1);
    j->compile_root = wb.root ? utils_get_utf8_from_locale(wb.root):NULL;
    j->clang_resources = g_build_filename(app->datadir, "insight", "clang", "resources", NULL);
    wb.scan_backup = insight_index_ref(wb.base);
    j->index = wb.base;
    wb.base = NULL;
    j->calls = insight_index_ref(wb.disk_calls);
    j->outline = insight_index_ref(wb.disk_declarations);
    j->stamp = stamp;
    j->path = g_strdup(path);
    j->utf8 = utf8;
    j->language = g_strdup(ft ? ft->name : NULL);
    j->lang = ft ? ft->lang : -1;
    j->read = ft && ft->id != GEANY_FILETYPES_NONE;
    j->encoding = file_prefs.default_open_encoding;
    j->generation = wb.scan_generation;
    j->epoch = workbench_epoch;
    wb.scan_worker = g_cancellable_new();
    GTask *task = g_task_new(NULL, wb.scan_worker, index_job_done, NULL);
    g_task_set_task_data(task, j, index_job_free);
    g_task_run_in_thread(task, index_job_run);
    g_object_unref(task);
}

/* EN: Advance the directory scan by one idle slice. data is unused.
 * RU: Продвигает обход каталогов за один проход. data не используется. */
static gboolean scan_step(gpointer data)
{
    (void)data;
    if ((!wb.frames->len && wb.scan_file_position >= wb.scan_files->len && wb.scan_tree_position >= wb.scan_trees->len))
    {
        /* Enumeration must never wait for Clang. Publish membership before parsing. */
        if (!wb.scan_indexing)
        {
            wb.scan_indexing = TRUE;
            wb.scan_index_position = 0;
            g_ptr_array_set_size(wb.files, 0);
            for (guint n = 0;n<wb.scan_members->len;n++)
                g_ptr_array_add(wb.files, g_strdup(g_ptr_array_index(wb.scan_members, n)));
            files_filter(NULL, NULL);
            tree_refresh();
            hdl_invalidate_colors();
            g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-members-ready", GUINT_TO_POINTER(wb.files->len+1));
        }
        if (wb.scan_index_position<wb.scan_members->len)
        {
            gchar *native = utils_get_locale_from_utf8(g_ptr_array_index(wb.scan_members, wb.scan_index_position++));
            scan_source_file(native);
            g_free(native);
            return scan_continue();
        }
        gboolean limited = FALSE;
        wb.scan = 0;
        wb.relation_dirty = TRUE;
        wb.report_scan_completed = !wb.scan_failed;
        /* Project membership is separate from the symbol index. Publish a complete
   * replacement only after enumeration; do not narrow the visible list while
   * a same-root rescan is still in progress. */
        if (!wb.scan_failed)
        {
            gboolean detached = FALSE;
            GHashTable *live = g_hash_table_new(g_str_hash, g_str_equal);
            for (guint n = 0;n<wb.scan_members->len;n++)
                g_hash_table_add(live, g_ptr_array_index(wb.scan_members, n));
            GHashTableIter it;
            gpointer path;
            g_hash_table_iter_init(&it, wb.file_stamps);
            while (g_hash_table_iter_next(&it, &path, NULL)) if (!g_hash_table_contains(live, path))
            {
                if (!detached)
                {
                    InsightIndex *copy = insight_index_clone(wb.base);
                    retire_index(wb.base);
                    wb.base = copy;
                    copy = insight_index_clone(wb.disk_calls);
                    retire_index(wb.disk_calls);
                    wb.disk_calls = copy;
                    copy = insight_index_clone(wb.disk_declarations);
                    retire_index(wb.disk_declarations);
                    wb.disk_declarations = copy;
                    detached = TRUE;
                }
                insight_index_update(wb.base, path, NULL, 0);
                insight_index_update(wb.disk_calls, path, NULL, 0);
                insight_index_update(wb.disk_declarations, path, NULL, 0);
                g_hash_table_remove(wb.declarations, path);
                g_hash_table_iter_remove(&it);
            }
            g_hash_table_unref(live);
        }
        wb.force_parse = FALSE;
        if (!wb.scan_failed)
        {
            GPtrArray *old = wb.files;
            wb.files = wb.scan_members;
            wb.scan_members = old;
            g_ptr_array_set_size(wb.scan_members, 0);
        }
        g_ptr_array_set_size(wb.frames, 0);
        g_ptr_array_set_size(wb.scan_files, 0);
        g_ptr_array_set_size(wb.scan_trees, 0);
        g_hash_table_remove_all(wb.scan_seen);
        refresh_index();
        g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-parsed", GUINT_TO_POINTER(wb.parsed+1));
        g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-reused", GUINT_TO_POINTER(wb.reused+1));
        g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-members", GUINT_TO_POINTER(wb.files->len+1));
        g_object_set_data(G_OBJECT(main_widgets.window), "insight-db-scanning", NULL);
        if (!limited && !wb.scan_failed) save_index_cache();
        ui_set_statusbar(TRUE, "Workspace: %u parsed, %u reused, %zu symbols, %u skipped%s", wb.parsed, wb.reused, insight_index_count(wb.index), wb.skipped, limited ? "; file limit reached" : (wb.watch_incomplete ? "; directory monitoring incomplete" : ""));
        browser_filter(NULL, NULL);
        files_filter(NULL, NULL);
        tree_refresh();
        file_types_refresh();
        categories_filter(NULL, NULL);
        local_filter(NULL, NULL);
        insight_workbench_refresh();
        if (wb.watch_changed && !wb.watch_idle)
        {
            wb.watch_changed = FALSE;
            wb.watch_idle = g_timeout_add(2000, rescan_changed, NULL);
        }
        return G_SOURCE_REMOVE;
    }
    if (wb.scan_file_position<wb.scan_files->len)
    {
        scan_source_file(g_ptr_array_index(wb.scan_files, wb.scan_file_position++));
        return scan_continue();
    }
    if (!wb.frames->len)
    {
        const char *path = g_ptr_array_index(wb.scan_trees, wb.scan_tree_position++);
        if (!g_hash_table_contains(wb.scan_seen, path))
        {
            GDir *dir = g_dir_open(path, 0, NULL);
            if (dir)
            {
                ScanFrame *extra = g_new0(ScanFrame, 1);
                extra->path = g_strdup(path);
                extra->dir = dir;
                g_hash_table_add(wb.scan_seen, g_strdup(path));
                g_ptr_array_add(wb.frames, extra);
                watch_directory(path);
            }
            else
            {
                wb.scan_failed = TRUE;
                wb.skipped++;
            }
        }
        return G_SOURCE_CONTINUE;
    }
    ScanFrame *frame = g_ptr_array_index(wb.frames, wb.frames->len-1);
    const char *name = g_dir_read_name(frame->dir);
    if (!name)
    {
        g_ptr_array_remove_index(wb.frames, wb.frames->len-1);
        return G_SOURCE_CONTINUE;
    }
    if (name[0] == '.' || !strcmp(name, "node_modules") || !strcmp(name, "__pycache__"))
        return G_SOURCE_CONTINUE;
    gchar *path = g_build_filename(frame->path, name, NULL);
    GStatBuf st;
    if (g_lstat(path, &st))
    {
        wb.scan_failed = TRUE;
        wb.skipped++;
        g_free(path);
        return G_SOURCE_CONTINUE;
    }
    if (S_ISDIR(st.st_mode))
    {
        gchar *profile = insight_canonicalize_filename(app->configdir, NULL);
        gboolean own_profile = !g_strcmp0(path, profile);
        g_free(profile);
        if (own_profile || g_hash_table_contains(wb.scan_seen, path))
        {
            g_free(path);
            return G_SOURCE_CONTINUE;
        }
        GDir *dir = wb.frames->len<64 ? g_dir_open(path, 0, NULL) : NULL;
        if (dir)
        {
            g_hash_table_add(wb.scan_seen, g_strdup(path));
            watch_directory(path);
            ScanFrame *next = g_new0(ScanFrame, 1);
            next->path = path;
            next->dir = dir;
            g_ptr_array_add(wb.frames, next);
        }
        else
        {
            wb.scan_failed = TRUE;
            wb.skipped++;
            g_free(path);
        }
        return G_SOURCE_CONTINUE;
    }
    scan_source_file(path);
    g_free(path);
    return scan_continue();
}
