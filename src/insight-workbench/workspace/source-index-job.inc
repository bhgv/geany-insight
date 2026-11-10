/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Asynchronous indexing jobs for individual source files.
 * RU: Асинхронная индексация отдельных исходников. */

static char *read_disk_source_full(const char *, gboolean, gint, GCancellable *);
static gboolean scan_step(gpointer);
typedef struct
{
    InsightIndex *index, *calls, *outline;
    OutlineFile *declarations;
    gchar *stamp, *path, *utf8, *language, *compile_root, *clang_resources;
    gint lang, encoding;
    guint64 generation, epoch;
    gboolean read, parsed, failed, skipped, regular;
} IndexJob;

/* EN: Release the index job in data and all owned resources.
 * RU: Освобождает задачу индексации из data и её ресурсы. */
static void index_job_free(gpointer data)
{
    IndexJob *j = data;
    outline_free(j->declarations);
    retire_index(j->index);
    retire_index(j->calls);
    retire_index(j->outline);
    g_free(j->stamp);
    g_free(j->path);
    g_free(j->utf8);
    g_free(j->language);
    g_free(j->compile_root);
    g_free(j->clang_resources);
    g_free(j);
}

/* EN: Index the source in data and complete task. cancel stops the work. source is unused.
 * RU: Индексирует исходник из data и завершает task. cancel отменяет работу. source не используется. */
static void index_job_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    IndexJob *j = data;
    GStatBuf st;
    if (g_cancellable_is_cancelled(cancel))
    {
        g_task_return_boolean(task, FALSE);
        return;
    }
    if (!insight_index_exclusive(j->index))
    {
        InsightIndex *copy = insight_index_clone(j->index);
        if (!copy)
        {
            j->failed = TRUE;
            g_task_return_boolean(task, TRUE);
            return;
        }
        insight_index_free(j->index);
        j->index = copy;
    }
    if (!insight_index_exclusive(j->calls))
    {
        InsightIndex *copy = insight_index_clone(j->calls);
        insight_index_free(j->calls);
        j->calls = copy;
    }
    if (!insight_index_exclusive(j->outline))
    {
        InsightIndex *copy = insight_index_clone(j->outline);
        insight_index_free(j->outline);
        j->outline = copy;
    }
    if (!j->calls || !j->outline)
    {
        j->failed = TRUE;
        g_task_return_boolean(task, TRUE);
        return;
    }
    insight_index_update(j->index, j->utf8, NULL, 0);
    insight_index_update(j->calls, j->utf8, NULL, 0);
    insight_index_update(j->outline, j->utf8, NULL, 0);
    j->regular = !g_lstat(j->path, &st) && S_ISREG(st.st_mode);
    if (!j->regular || st.st_size>2*1024*1024) j->skipped = TRUE;
    else if (j->read)
    {
        gchar *text = read_disk_source_full(j->utf8, TRUE, j->encoding, cancel);
        if (!text) j->failed = TRUE;
        else
        {
            j->parsed = TRUE;
            if (j->lang >= 0 && !g_cancellable_is_cancelled(cancel))
            {
                TMSourceFile *file = tm_source_file_new(j->path, j->language);
                if (file)
                {
                    file->lang = j->lang;
                    if (!insight_native_parse(j->index, file, j->utf8, text, strlen(text))) j->failed = TRUE;
                    else
                    {
                        j->declarations = outline_capture(file, j->utf8);
                        if (!insight_native_outline_import(j->outline, file, j->utf8)) j->failed = TRUE;
                    }
                    tm_source_file_free(file);
                }
                else j->failed = TRUE;
            }
            gboolean semantic = FALSE;
            if (!j->failed && insight_clang_available() && insight_clang_source(j->utf8) && !g_cancellable_is_cancelled(cancel))
            {
                InsightClangResult *parsed = insight_clang_parse(j->utf8, text, j->compile_root, j->clang_resources, NULL, cancel, NULL);
                if (insight_clang_result_usable(parsed))
                {
                    semantic = insight_clang_import(parsed, j->utf8, j->index, j->calls);
                    if (semantic)
                    {
                        TMSourceFile *tags = insight_native_clang_tags(parsed, j->path, j->language, j->lang);
                        if (tags)
                        {
                            outline_free(j->declarations);
                            j->declarations = outline_capture(tags, j->utf8);
                            if (!insight_native_outline_import(j->outline, tags, j->utf8)) j->failed = TRUE;
                            for (guint n = 0;n<tags->tags_array->len;n++) ((TMTag *)g_ptr_array_index(tags->tags_array, n))->file = NULL;
                            tm_source_file_free(tags);
                        }
                    }
                }
                insight_clang_result_free(parsed);
            }
            InsightCodeDialect dialect;
            GeanyFiletype *ft = filetypes_lookup_by_name(j->language ? j->language:"");
            if (!j->failed && !semantic && source_dialect(ft, &dialect))
            {
                gchar *mask = insight_source_code_mask(text, strlen(text), dialect, NULL), *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, text, -1);
                GArray *rows = g_array_new(FALSE, FALSE, sizeof(InsightSymbol));
                GPtrArray *owned = g_ptr_array_new_with_free_func(g_free);
                size_t cursor = 0, a = 0, b = 0, line = 1, line_at = 0;
                while (mask && rows->len<32000 && insight_next_call(mask, strlen(text), &cursor, &a, &b) == 1)
                {
                    while (line_at<a)
                    {
                        if (text[line_at] == '\n') line++;
                        else if (text[line_at] == '\r' && (line_at+1 >= a || text[line_at+1] != '\n')) line++;
                        line_at++;
                    }
                    size_t start = a;
                    while (start && text[start-1] != '\n' && text[start-1] != '\r') start--;
                    gchar *name = g_strndup(text+a, b-a), *positions = g_strdup_printf("%zu:%zu:%lu", a, b, (gulong)g_utf8_strlen(text+start, a-start)+1), *snippet = reference_snippet(text, a);
                    g_ptr_array_add(owned, name);
                    g_ptr_array_add(owned, positions);
                    g_ptr_array_add(owned, snippet);
                    InsightSymbol row =
                    {
                        name, j->utf8, "", "@call", line, positions, snippet, digest
                    };
                    g_array_append_val(rows, row);
                }
                InsightSymbol coverage =
                {
                    "@file", j->utf8, "", "@file", 1, "", "", digest
                };
                g_array_append_val(rows, coverage);
                if (!mask || rows->len >= 32001 || !insight_index_update(j->calls, j->utf8, (InsightSymbol *)rows->data, rows->len)) j->failed = TRUE;
                g_ptr_array_unref(owned);
                g_array_unref(rows);
                g_free(mask);
                g_free(digest);
            }
            gchar *after = source_stamp(j->path, j->language, j->encoding);
            if (g_strcmp0(after, j->stamp)) j->failed = TRUE;
            g_free(after);
            g_free(text);
        }
    }
    g_task_return_boolean(task, TRUE);
}

/* EN: Apply result to the current scan when still valid. source and data are unused.
 * RU: Применяет result к текущему сканированию. source и data не используются. */
static void index_job_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    IndexJob *j = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.scan_generation) return;
    g_clear_object(&wb.scan_worker);
    if (j->failed || !ok)
    {
        retire_index(wb.base);
        wb.base = wb.scan_backup ? insight_index_ref(wb.scan_backup):insight_index_new();
    }
    else
    {
        retire_index(wb.base);
        wb.base = j->index;
        j->index = NULL;
        retire_index(wb.disk_calls);
        wb.disk_calls = j->calls;
        j->calls = NULL;
        retire_index(wb.disk_declarations);
        wb.disk_declarations = j->outline;
        j->outline = NULL;
        wb.index_dirty = TRUE;
    }
    retire_index(wb.scan_backup);
    wb.scan_backup = NULL;
    if (!j->failed && j->stamp)
        g_hash_table_replace(wb.file_stamps, g_strdup(j->utf8), g_strdup(j->stamp));
    if (j->declarations && !j->failed)
    {
        g_hash_table_replace(wb.declarations, g_strdup(j->utf8), j->declarations);
        j->declarations = NULL;
    }
    if (!j->parsed && !j->failed)
        g_hash_table_remove(wb.declarations, j->utf8);
    if (j->parsed)
        wb.parsed++;
    if (j->failed || j->skipped || !ok)
        wb.skipped++;
    if (j->failed)
        wb.scan_failed = TRUE;
    if ((wb.parsed+wb.reused)%25 == 0)
    {
        if (!wb.scan_keep_files) files_filter(NULL, NULL);
        ui_set_statusbar(TRUE, "Workspace: indexing… %u files", wb.parsed);
    }
    if (g_get_monotonic_time() >= wb.publish_due)
    {
        wb.publish_due = g_get_monotonic_time()+1000000;
        refresh_index();
    }
    wb.scan = g_idle_add(scan_step, NULL);
}
