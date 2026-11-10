/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: HDL color scheduling and source capture.
 * RU: Планирование подсветки HDL и сбор исходников. */


/* EN: Handle result from the color worker. source and data are unused.
 * RU: Обрабатывает result подсветки. source и data не используются. */
static void hdl_colors_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    HdlJob *j = g_task_get_task_data(task);
    if (hdl_color_worker == g_task_get_cancellable(task))
        g_clear_object(&hdl_color_worker);
    if (!g_task_propagate_boolean(task, NULL) || !wb.alive || j->epoch != workbench_epoch ||
    j->generation != hdl_color_revision || j->scan_generation != wb.scan_generation ||
    g_strcmp0(j->project, app->project ? app->project->file_name:NULL)) return;
    GeanyDocument *active = document_get_current();
    if (DOC_VALID(active) && active->id == j->paint_doc) hdl_paint(j);
}
typedef struct
{
    HdlJob *job;
    guint next;
    gsize total;
    gint offset;
    GString *text;
}
HdlCapture;

/* EN: Release the capture state in data.
 * RU: Освобождает состояние сбора из data. */
static void hdl_capture_free(gpointer data)
{
    HdlCapture *c = data;
    if (c->job) hdl_job_free(c->job);
    if (c->text)
        g_string_free(c->text, TRUE);
    g_free(c);
}

/* EN: Collect source data during an idle slice.
 * RU: Собирает исходники из data за один проход. */
static gboolean hdl_capture_step(gpointer data)
{
    HdlCapture *c = data;
    HdlJob *j = c->job;
    if (!wb.alive || j->generation != hdl_color_revision || j->scan_generation != wb.scan_generation) goto done;
    gint64 deadline = g_get_monotonic_time()+1500;
    while (c->next<documents_array->len)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, c->next);
        if (!DOC_VALID(doc) || !insight_slang_source(doc->file_name))
        {
            c->next++;
            continue;
        }
        gint length = sci_get_length(doc->editor->sci);
        if (!c->text)
        {
            if (length<0 || length>2*1024*1024 || c->total+(gsize)length>32u*1024u*1024u ||
            g_hash_table_size(j->buffers) >= 256) goto done;
            c->text = g_string_new(NULL);
            c->offset = 0;
            c->total+ = length;
            if (!g_ptr_array_find_with_equal_func(j->paths, doc->file_name, (GEqualFunc)g_str_equal, NULL))
            g_ptr_array_add(j->paths, g_strdup(doc->file_name));
            if (j->paths->len>256) goto done;
        }
        if (c->offset<length)
        {
            gint end = MIN(length, c->offset+65536), n = end-c->offset;
            gchar *part = sci_get_contents_range(doc->editor->sci, c->offset, end);
            if (!part || memchr(part, 0, n))
            {
                g_free(part);
                goto done;
            }
            g_string_append_len(c->text, part, n);
            g_free(part);
            c->offset = end;
        }
        if (c->offset == length)
        {
            g_hash_table_insert(j->buffers, g_strdup(doc->file_name), g_string_free(c->text, FALSE));
            c->text = NULL;
            c->next++;
        }
        if (g_get_monotonic_time() >= deadline)
            return G_SOURCE_CONTINUE;
    }
    if (!j->root && j->paths->len) j->root = g_path_get_dirname(g_ptr_array_index(j->paths, 0));
    hdl_color_worker = g_cancellable_new();
    GTask *task = g_task_new(NULL, hdl_color_worker, hdl_colors_done, NULL);
    g_task_set_task_data(task, j, hdl_job_free);
    c->job = NULL;
    g_task_run_in_thread(task, hdl_job_run);
    g_object_unref(task);
    done:
    hdl_snapshot_idle = 0;
    return G_SOURCE_REMOVE;
}

/* EN: Start automatic HDL coloring. data is unused.
 * RU: Запускает автоматическую подсветку HDL. data не используется. */
static gboolean hdl_auto_colors(gpointer data)
{
    (void)data;
    GeanyDocument *doc = wb.alive ? document_get_current():NULL;
    if (!DOC_VALID(doc) || !insight_slang_source(doc->file_name) ||
    SSM(doc->editor->sci, SCI_GETLEXER, 0, 0) != SCLEX_VERILOG ||
    g_object_get_data(G_OBJECT(doc->editor->sci), "insight-hdl-painted"))
    {
        hdl_color_timer = 0;
        return G_SOURCE_REMOVE;
    }
    if (hdl_color_worker || hdl_snapshot_idle || wb.search_worker || (app->project && wb.restore_worker))
        return G_SOURCE_CONTINUE;
    hdl_color_timer = 0;
    gchar *service = insight_completion_slang_service();
    gboolean available = g_file_test(service, G_FILE_TEST_IS_EXECUTABLE);
    g_free(service);
    if (!available)
        return G_SOURCE_REMOVE;
    HdlCapture *c = g_new0(HdlCapture, 1);
    c->job = hdl_job_new(NULL, FALSE, TRUE);
    if (c->job->paths->len>256)
    {
        hdl_capture_free(c);
        return G_SOURCE_REMOVE;
    }
    hdl_snapshot_idle = g_idle_add_full(G_PRIORITY_LOW, hdl_capture_step, c, hdl_capture_free);
    return G_SOURCE_REMOVE;
}

/* EN: Schedule a refresh of HDL colors.
 * RU: Планирует обновление подсветки HDL. */
static void hdl_schedule_colors(void)
{
    if (!wb.alive) return;
    if (hdl_color_timer)
    {
        g_source_remove(hdl_color_timer);
        hdl_color_timer = 0;
    }
    GeanyDocument *doc = document_get_current();
    if (DOC_VALID(doc) && insight_slang_source(doc->file_name))
    hdl_color_timer = g_timeout_add(650, hdl_auto_colors, NULL);
}

/* EN: Run the HDL reference query, optionally collecting diagnostics.
 * RU: Ищет ссылки HDL по query с учётом diagnostics. */
static void hdl_references(const gchar *query, gboolean diagnostics)
{
    hdl_cancel_colors();
    HdlJob *j = hdl_snapshot(query, diagnostics);
    wb.search_worker = g_cancellable_new();
    GTask *task = g_task_new(NULL, wb.search_worker, hdl_job_done, NULL);
    g_task_set_task_data(task, j, hdl_job_free);
    g_task_run_in_thread(task, hdl_job_run);
    g_object_unref(task);
    set_usage_status("Preparing HDL semantic project index…");
    hdl_schedule_colors();
}
