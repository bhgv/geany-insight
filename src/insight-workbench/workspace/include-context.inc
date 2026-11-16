/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Build project include ownership off the GUI thread before indexing.
 * RU: Строит принадлежность подключений проекта в фоне перед индексацией. */
typedef struct
{
    GPtrArray *paths;
    GHashTable *buffers, *languages;
    gchar *root;
    guint64 epoch, generation;
    gint encoding;
} IncludeContextJob;

/* EN: Free include job data, snapshots and inferred languages.
 * RU: Освобождает задачу data, снимки и определённые языки. */
static void include_context_free(gpointer data)
{
    IncludeContextJob *j = data;
    g_ptr_array_unref(j->paths);
    g_hash_table_unref(j->buffers);
    g_clear_pointer(&j->languages, g_hash_table_unref);
    g_free(j->root);
    g_free(j);
}

/* EN: Build ownership from snapshot data in task; honor cancel. source is unused.
 * Reading is limited to 64 MiB and the existing per-file source bound.
 * RU: Строит принадлежность из снимка data задачи task с учётом cancel; source
 * не используется. Читает до 64 МиБ с существующим ограничением размера файла. */
static void include_context_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    IncludeContextJob *j = data;
    InsightIncludeGraph *graph = insight_include_graph_new(j->paths, j->root);
    gsize bytes = 0;
    guint n = 0;
    for (; n < j->paths->len && bytes < 64u * 1024u * 1024u; n++)
    {
        if (g_cancellable_is_cancelled(cancel)) break;
        const gchar *path = g_ptr_array_index(j->paths, n);
        if (!insight_include_graph_candidate(path)) continue;
        const gchar *buffer = g_hash_table_lookup(j->buffers, path);
        gchar *disk = buffer ? NULL : read_disk_source_full(path, TRUE, j->encoding, cancel);
        const gchar *text = buffer ? buffer : disk;
        if (text)
        {
            bytes += strlen(text);
            insight_include_graph_add(graph, path, text);
        }
        g_free(disk);
    }
    if (!g_cancellable_is_cancelled(cancel) && n == j->paths->len) j->languages = insight_include_graph_resolve(graph);
    insight_include_graph_free(graph);
    g_task_return_boolean(task, j->languages != NULL);
}

/* EN: Apply result to the current scan and resume indexing; source/data unused.
 * Retype default session .inc buffers only after their include owners are known.
 * RU: Применяет result к текущему обходу и продолжает индекс; source/data не нужны.
 * Меняет исходный тип открытых .inc только после определения владельцев include. */
static void include_context_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    IncludeContextJob *j = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.scan_generation) return;
    g_clear_object(&wb.scan_worker);
    g_clear_pointer(&wb.include_languages, g_hash_table_unref);
    wb.include_languages = ok ? g_hash_table_ref(j->languages) : NULL;
    wb.scan_includes_ready = TRUE;
    for (guint n = 0; n < documents_array->len; n++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, n);
        if (!DOC_VALID(doc) || !doc->file_name || !doc->file_type) continue;
        const gchar *dot = strrchr(doc->file_name, '.');
        guint automatic = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(doc->editor->sci),
            "insight-inferred-include-type"));
        if (!dot || g_ascii_strcasecmp(dot, ".inc") ||
            (doc->file_type->id != GEANY_FILETYPES_PASCAL &&
             doc->file_type->id != GEANY_FILETYPES_NONE &&
             automatic != doc->file_type->id + 1)) continue;
        GeanyFiletype *detected = insight_languages_detect(doc->file_name);
        if (detected && detected != doc->file_type) document_set_filetype(doc, detected);
        g_object_set_data(G_OBJECT(doc->editor->sci), "insight-inferred-include-type",
            detected && insight_workbench_include_language(doc->file_name) ?
            GUINT_TO_POINTER(detected->id + 1) : NULL);
    }
    browser_filter(NULL, NULL);
    hdl_invalidate_colors();
    wb.scan = g_idle_add(scan_step, NULL);
}

/* EN: Capture paths/buffers and return TRUE when a worker was queued.
 * Projects without .inc become ready immediately and return FALSE.
 * RU: Сохраняет пути/буферы и возвращает TRUE при запуске фоновой задачи.
 * Проекты без .inc сразу готовы и возвращают FALSE. */
static gboolean include_context_start(void)
{
    gboolean needed = FALSE;
    for (guint n = 0; n < wb.files->len && !needed; n++)
    {
        const gchar *dot = strrchr(g_ptr_array_index(wb.files, n), '.');
        needed = dot && !g_ascii_strcasecmp(dot, ".inc");
    }
    if (!needed)
    {
        wb.scan_includes_ready = TRUE;
        return FALSE;
    }
    IncludeContextJob *j = g_new0(IncludeContextJob, 1);
    j->paths = g_ptr_array_new_with_free_func(g_free);
    j->buffers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    j->root = utils_get_utf8_from_locale(wb.root);
    j->encoding = file_prefs.default_open_encoding;
    j->epoch = workbench_epoch;
    j->generation = wb.scan_generation;
    for (guint n = 0; n < wb.files->len; n++)
        g_ptr_array_add(j->paths, g_strdup(g_ptr_array_index(wb.files, n)));
    gsize bytes = 0;
    for (guint n = 0; n < documents_array->len && bytes < 32u * 1024u * 1024u; n++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, n);
        if (!DOC_VALID(doc) || !doc->file_name) continue;
        gint length = sci_get_length(doc->editor->sci);
        if (length < 0 || length > 2 * 1024 * 1024) continue;
        gchar *text = sci_get_contents(doc->editor->sci, length + 1);
        if (!g_utf8_validate(text, length, NULL) || memchr(text, 0, length))
        {
            g_free(text);
            text = g_strdup("");
        }
        g_hash_table_insert(j->buffers, g_strdup(doc->file_name), text);
        bytes += length;
    }
    wb.scan_worker = g_cancellable_new();
    GTask *task = g_task_new(NULL, wb.scan_worker, include_context_done, NULL);
    g_task_set_task_data(task, j, include_context_free);
    g_task_run_in_thread(task, include_context_run);
    g_object_unref(task);
    return TRUE;
}

/* EN: Borrow the current include language for path; NULL means unknown/conflicting.
 * RU: Возвращает заимствованный язык path; NULL означает неизвестный/спорный язык. */
const gchar *insight_workbench_include_language(const gchar *path)
{
    return wb.alive && wb.include_languages && path ?
        g_hash_table_lookup(wb.include_languages, path) : NULL;
}

/* EN: Return an immutable include-language snapshot; caller unrefs it. NULL when absent.
 * RU: Возвращает неизменяемый снимок языков; вызывающий делает unref. NULL при отсутствии. */
GHashTable *insight_workbench_include_languages(void)
{
    return wb.alive && wb.include_languages ? g_hash_table_ref(wb.include_languages) : NULL;
}

/* EN: Refresh inferred ownership after editing an include directive in editor.
 * nt supplies the modified text/position. Debounce full scans, keeping disk files
 * untouched; ordinary source edits do not request an include scan.
 * RU: Обновляет принадлежность после правки include в editor. nt содержит текст/
 * позицию изменения. Откладывает обход без записи файлов; обычные правки его не запускают. */
static void include_context_edited(GeanyEditor *editor, SCNotification *nt)
{
    if (ignore_callback || !wb.root || !editor || !DOC_VALID(editor->document) ||
        !insight_include_graph_candidate(editor->document->file_name)) return;
    gboolean directive = nt->text && nt->length > 0 &&
        (memchr(nt->text, '#', nt->length) || memchr(nt->text, '`', nt->length));
    gint line = (gint)SSM(editor->sci, SCI_LINEFROMPOSITION, nt->position, 0);
    for (guint n = 0; !directive && line >= 0 && n < 16; n++, line--)
    {
        gchar *text = sci_get_line(editor->sci, line);
        gchar *start = text;
        while (*start == ' ' || *start == '\t') start++;
        directive = *start == '#' || *start == '`';
        g_free(text);
        if (!line || directive) break;
        gchar *previous = sci_get_line(editor->sci, line - 1);
        g_strchomp(previous);
        gboolean continued = *previous && previous[strlen(previous) - 1] == '\\';
        g_free(previous);
        if (!continued) break;
    }
    if (!directive) return;
    if (wb.watch_idle) g_source_remove(wb.watch_idle);
    wb.watch_idle = g_timeout_add(800, rescan_changed, NULL);
}
