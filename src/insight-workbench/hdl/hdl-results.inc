/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: HDL coloring, result publication and source snapshots.
 * RU: Подсветка HDL, публикация результатов и снимки исходников. */


/* EN: Paint visible HDL indicators for the editor in data.
 * RU: Отрисовывает видимые индикаторы HDL в редакторе из data. */
static gboolean hdl_paint_visible(gpointer data)
{
    (void)data;
    GeanyDocument *doc = wb.alive ? document_get_current():NULL;
    if (!DOC_VALID(doc))
    {
        hdl_paint_idle = 0;
        return G_SOURCE_REMOVE;
    }
    ScintillaObject *sci = doc->editor->sci;
    GArray *spans = g_object_get_data(G_OBJECT(sci), "insight-hdl-spans");
    if (!spans)
    {
        hdl_paint_idle = 0;
        return G_SOURCE_REMOVE;
    }
    gint top = SSM(sci, SCI_GETFIRSTVISIBLELINE, 0, 0), height = SSM(sci, SCI_LINESONSCREEN, 0, 0);
    guint first = MAX(0, SSM(sci, SCI_DOCLINEFROMVISIBLE, MAX(0, top-8), 0));
    guint last = MAX(first, SSM(sci, SCI_DOCLINEFROMVISIBLE, top+height+8, 0));
    guint lo = 0, hi = spans->len;
    while (lo<hi)
    {
        guint mid = lo+(hi-lo)/2;
        if (g_array_index(spans, HdlSpan, mid).line<first) lo = mid+1;
        else hi = mid;
    }
    guint bg = SSM(sci, SCI_STYLEGETBACK, STYLE_DEFAULT, 0);
    const guint light[] =
    {
        0, 0x8b3c66, 0x405d9c, 0x9b5b18, 0x925c77, 0x467c35
    };
    const guint dark[] =
    {
        0, 0xd9a0d5, 0x8fc9ed, 0xe8cf8c, 0xd5a7bd, 0x8fcaa0
    };
    const guint *palette = ((bg&255)+((bg>>8)&255)+((bg>>16)&255))<384 ? dark:light;
    if (hdl_paint_doc != doc->id || hdl_paint_first != first || hdl_paint_last != last) hdl_paint_cursor = 0;
    hdl_paint_doc = doc->id;
    hdl_paint_first = first;
    hdl_paint_last = last;
    gint64 deadline = g_get_monotonic_time()+1500;
    SSM(sci, SCI_SETINDICATORCURRENT, INSIGHT_HDL_COLOR, 0);
    for (guint i = MAX(lo, hdl_paint_cursor);i<spans->len;i++)
    {
        if ((i&31) == 0 && g_get_monotonic_time() >= deadline)
        {
            hdl_paint_cursor = i;
            return G_SOURCE_CONTINUE;
        }
        HdlSpan *span = &g_array_index(spans, HdlSpan, i);
        if (span->line>last) break;
        if (span->painted || !SSM(sci, SCI_GETLINEVISIBLE, span->line, 0)) continue;
        if (span->start >= (guint)SSM(sci, SCI_GETENDSTYLED, 0, 0)) break;
        span->painted = TRUE;
        if (SSM(sci, SCI_GETSTYLEAT, span->start, 0) == SCE_V_IDENTIFIER)
        {
            SSM(sci, SCI_SETINDICATORVALUE, SC_INDICVALUEBIT|palette[span->role], 0);
            SSM(sci, SCI_INDICATORFILLRANGE, span->start, span->length);
        }
    }
    hdl_paint_idle = 0;
    return G_SOURCE_REMOVE;
}

/* EN: Schedule painting in sci.
 * RU: Планирует подсветку в sci. */
static void hdl_queue_paint(ScintillaObject *sci)
{
    if (!hdl_paint_idle && g_object_get_data(G_OBJECT(sci), "insight-hdl-spans"))
    {
        hdl_paint_cursor = 0;
        hdl_paint_idle = g_idle_add_full(G_PRIORITY_LOW, hdl_paint_visible, NULL, NULL);
    }
}

/* EN: Apply color metadata from j and queue painting.
 * RU: Применяет данные подсветки из j и планирует отрисовку. */
static void hdl_paint(const HdlJob *j)
{
    GeanyDocument *doc = j->paint_path ? document_find_by_id(j->paint_doc):NULL;
    if (!DOC_VALID(doc) || g_strcmp0(doc->file_name, j->paint_path)) return;
    ScintillaObject *sci = doc->editor->sci;
    if (!g_hash_table_contains(j->buffers, j->paint_path) || SSM(sci, SCI_GETLEXER, 0, 0) != SCLEX_VERILOG) return;
    hdl_clear_document_colors(sci);
    hdl_paint_cursor = 0;
    g_object_set_data(G_OBJECT(sci), "insight-hdl-painted", GINT_TO_POINTER(1));
    g_object_set_data_full(G_OBJECT(sci), "insight-hdl-spans", g_array_ref(j->colors), (GDestroyNotify)g_array_unref);
    SSM(sci, SCI_INDICSETSTYLE, INSIGHT_HDL_COLOR, INDIC_TEXTFORE);
    SSM(sci, SCI_INDICSETFLAGS, INSIGHT_HDL_COLOR, SC_INDICFLAG_VALUEFORE);
    hdl_queue_paint(sci);
}

/* EN: Add the next batch of HDL results from data.
 * RU: Добавляет следующую порцию результатов HDL из data. */
static gboolean hdl_results_apply(gpointer data)
{
    GTask *task = data;
    HdlJob *j = g_task_get_task_data(task);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.usage_generation || g_strcmp0(j->project, app->project ? app->project->file_name:NULL))
        return G_SOURCE_REMOVE;
    if (!j->applied)
    {
        GPtrArray *paths = insight_workbench_source_paths(insight_slang_source, 257);
        GHashTable *current = g_hash_table_new(g_str_hash, g_str_equal);
        for (guint i = 0;i<paths->len;i++)
            g_hash_table_add(current, g_ptr_array_index(paths, i));
        for (guint i = 0;i<documents_array->len;i++)
        {
            GeanyDocument *doc = g_ptr_array_index(documents_array, i);
            if (DOC_VALID(doc) && insight_slang_source(doc->file_name))
                g_hash_table_add(current, doc->file_name);
        }
        gboolean same = g_hash_table_size(current) == j->paths->len && (!j->project || j->scan_generation == wb.scan_generation);
        for (guint i = 0;same && i<j->paths->len;i++) same = g_hash_table_contains(current, g_ptr_array_index(j->paths, i));
        g_hash_table_unref(current);
        g_ptr_array_unref(paths);
        if (!same)
        {
            wb.usage_idle = 0;
            set_usage_status("HDL sources changed; repeat the query");
            return G_SOURCE_REMOVE;
        }
        GHashTableIter iter;
        gpointer file, text;
        g_hash_table_iter_init(&iter, j->buffers);
        while (g_hash_table_iter_next(&iter, &file, &text))
        {
            GeanyDocument *doc = document_find_by_filename(file);
            gchar *now = DOC_VALID(doc) ? sci_get_contents(doc->editor->sci, sci_get_length(doc->editor->sci)+1):NULL;
            gboolean same = now && !strcmp(now, text);
            g_free(now);
            if (!same)
            {
                wb.usage_idle = 0;
                set_usage_status("HDL sources changed; repeat the query");
                return G_SOURCE_REMOVE;
            }
        }
        hdl_paint(j);
        gtk_list_store_clear(wb.matches);
    }
    if (j->hierarchy)
    {
        hdl_instance_publish(j);
        wb.usage_idle = 0;
        return G_SOURCE_REMOVE;
    }
    gint64 deadline = g_get_monotonic_time()+3000;
    while (j->applied<j->rows->len)
    {
        const InsightSymbol *row = g_ptr_array_index(j->rows, j->applied++);
        GtkTreeIter iter;
        gboolean global = j->diagnostics && !strcmp(row->inheritance, "global");
        gchar *description = j->diagnostics ? g_strdup_printf("%s [%s]", row->type, row->scope):g_strdup_printf("%s → %s:%s", row->inheritance, row->scope, row->type);
        gtk_list_store_append(wb.matches, &iter);
        gtk_list_store_set(wb.matches, &iter, USAGE_DISPLAY, global ? "Project diagnostic":row->path, USAGE_PATH, global ? NULL:row->path, USAGE_LINE, global ? 0:row->line, USAGE_COLUMN, (gulong)g_ascii_strtoull(row->signature, NULL, 10), USAGE_SYMBOL, row->name, USAGE_SNIPPET, description, -1);
        g_free(description);
        if (g_get_monotonic_time() >= deadline)
            return G_SOURCE_CONTINUE;
    }
    gchar *status = g_strdup_printf(j->diagnostics ? "slang: %u messages • %zu declarations • diagnostics: %u • %s%s":"slang: %u references • %zu declarations • diagnostics: %u • %s%s", j->rows->len, insight_index_query(j->result->index, "", FALSE, NULL, "@hdldef", NULL, 0, NULL, 0), j->result->diagnostics, insight_tr(j->result->restored ? "Restored HDL project index":"HDL project index rebuilt"), insight_tr(j->result->persisted ? " • saved":" • memory only"));
    set_usage_status(status);
    g_free(status);
    wb.usage_idle = 0;
    return G_SOURCE_REMOVE;
}

/* EN: Apply result from the HDL worker. source and data are unused.
 * RU: Применяет result фонового анализа HDL. source и data не используются. */
static void hdl_job_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    HdlJob *j = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.usage_generation) return;
    g_clear_object(&wb.search_worker);
    if (ok)
        wb.usage_idle = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, hdl_results_apply, g_object_ref(task), g_object_unref);
    else set_usage_status("HDL semantic index unavailable or exceeds its limits");
}

/* EN: Create an HDL job for query with diagnostics and colors_only options.
 * RU: Создаёт задачу HDL для query с параметрами diagnostics и colors_only. */
static HdlJob *hdl_job_new(const gchar *query, gboolean diagnostics, gboolean colors_only)
{
    HdlJob *j = g_new0(HdlJob, 1);
    j->paths = insight_workbench_source_paths(insight_slang_source, 257);
    j->rows = g_ptr_array_new();
    j->colors = g_array_new(FALSE, FALSE, sizeof(HdlSpan));
    j->buffers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    j->service = insight_completion_slang_service();
    j->query = g_strdup(query);
    j->project = g_strdup(app->project ? app->project->file_name:NULL);
    j->vivado = insight_slang_vivado_config(j->project);
    j->root = wb.root && app->project ? utils_get_utf8_from_locale(wb.root):NULL;
    j->cache = app->project && wb.cache_path ? g_strconcat(wb.cache_path, ".hdl", NULL):NULL;
    j->valid = TRUE;
    j->diagnostics = diagnostics;
    j->colors_only = colors_only;
    j->hierarchy = !colors_only && wb.usage_mode == 13;
    j->epoch = workbench_epoch;
    j->generation = colors_only ? hdl_color_revision:wb.usage_generation;
    j->scan_generation = wb.scan_generation;
    GeanyDocument *active = document_get_current();
    if (DOC_VALID(active) && insight_slang_source(active->file_name))
    {
        j->paint_path = g_strdup(active->file_name);
        j->paint_doc = active->id;
        j->cursor_line = (guint)sci_get_current_line(active->editor->sci) + 1;
    }
    return j;
}

/* EN: Capture source inputs for query and diagnostics mode.
 * RU: Сохраняет снимок исходников для query и режима diagnostics. */
static HdlJob *hdl_snapshot(const gchar *query, gboolean diagnostics)
{
    HdlJob *j = hdl_job_new(query, diagnostics, FALSE);
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0;i<j->paths->len;i++)
        g_hash_table_add(seen, g_ptr_array_index(j->paths, i));
    gsize total = 0;
    for (guint i = 0;i<documents_array->len;i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        if (!DOC_VALID(doc) || !insight_slang_source(doc->file_name)) continue;
        if (!g_hash_table_contains(seen, doc->file_name))
        {
            g_ptr_array_add(j->paths, g_strdup(doc->file_name));
            g_hash_table_add(seen, doc->file_name);
        }
        gint length = sci_get_length(doc->editor->sci);
        if (length<0 || length>2*1024*1024 || total+(gsize)length>32u*1024u*1024u || g_hash_table_size(j->buffers) >= 256)
        {
            j->valid = FALSE;
            continue;
        }
        gchar *text = sci_get_contents(doc->editor->sci, length+1);
        if (memchr(text, 0, length) || !g_utf8_validate(text, length, NULL))
        {
            j->valid = FALSE;
            g_free(text);
        }
        else
        {
            g_hash_table_insert(j->buffers, g_strdup(doc->file_name), text);
            total += length;
        }
    }
    g_hash_table_unref(seen);
    if (!j->root && j->paths->len) j->root = g_path_get_dirname(g_ptr_array_index(j->paths, 0));
    return j;
}
