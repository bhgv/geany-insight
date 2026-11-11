/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Semantic call tree jobs and display.
 * RU: Задачи построения и показа семантического дерева вызовов. */


/* EN: Release the semantic calls job in data.
 * RU: Освобождает задачу дерева вызовов из data. */
static void semantic_calls_free(gpointer data)
{
    SemanticCallsJob *j = data;
    insight_index_free(j->base);
    insight_index_free(j->calls);
    g_ptr_array_unref(j->paths);
    g_ptr_array_unref(j->overlays);
    g_hash_table_unref(j->buffers);
    g_free(j->resources);
    g_free(j->root);
    g_free(j->query);
    g_free(j->usr);
    g_free(j->source);
    g_free(j);
}

/* EN: Build the call index for data and finish task. cancel stops work. source is unused.
 * RU: Строит индекс вызовов для data и завершает task. cancel отменяет работу. source не используется. */
static void semantic_calls_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    SemanticCallsJob *j = data;
    j->calls = insight_index_clone(j->base);
    if (!j->calls)
    {
        g_task_return_boolean(task, FALSE);
        return;
    }
    for (guint i = 0;i<j->paths->len && !g_cancellable_is_cancelled(cancel);i++)
    {
        const gchar *path = g_ptr_array_index(j->paths, i);
        InsightIndex *overlay = g_ptr_array_index(j->overlays, i);
        InsightClangResult *parsed = NULL;
        /* Inactive open files may never have had a foreground Clang parse. Build
   * their overlay in this worker using the same captured unsaved headers. */
        if (!overlay && g_hash_table_lookup(j->buffers, path))
        {
            gchar *root = j->root ? g_strdup(j->root):g_path_get_dirname(path);
            parsed = insight_clang_parse(path, g_hash_table_lookup(j->buffers, path), root, j->resources, j->buffers, cancel, NULL);
            g_free(root);
            if (insight_clang_result_usable(parsed)) overlay = parsed->relations;
        }
        gsize n = insight_index_count(overlay);
        InsightSymbol *rows = g_new(InsightSymbol, n);
        for (gsize k = 0;k<n;k++) rows[k] = *insight_index_at(overlay, k);
        if (!insight_index_update(j->calls, g_ptr_array_index(j->paths, i), rows, n))
        {
            g_free(rows);
            insight_clang_result_free(parsed);
            g_task_return_boolean(task, FALSE);
            return;
        }
        g_free(rows);
        insight_clang_result_free(parsed);
    }
    if (!j->usr)
    {
        const InsightSymbol *candidate = NULL;
        guint count = 0;
        for (gsize i = 0;i<insight_index_count(j->calls) && !g_cancellable_is_cancelled(cancel);i++)
        {
            const InsightSymbol *row = insight_index_at(j->calls, i);
            if (strcmp(row->kind, "@clangdef") || strcmp(row->signature, j->query) || !strchr(row->type, '(')) continue;
            if (!candidate || strcmp(candidate->name, row->name) || (!strcmp(row->scope, "internal") && strcmp(candidate->path, row->path)))
            {
                candidate = row;
                count++;
            }
        }
        if (count == 1) j->usr = g_strdup(candidate->name);
    }
    g_task_return_boolean(task, !g_cancellable_is_cancelled(cancel));
}

/* EN: Publish result if the request is current. source and unused are ignored.
 * RU: Публикует result, если запрос актуален. source и unused не используются. */
static void semantic_calls_done(GObject *source, GAsyncResult *result, gpointer unused)
{
    (void)source;
    (void)unused;
    SemanticCallsJob *j = g_task_get_task_data(G_TASK(result));
    gboolean ok = g_task_propagate_boolean(G_TASK(result), NULL);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.usage_generation) return;
    g_clear_object(&wb.search_worker);
    if (!ok)
    {
        set_usage_status("Cannot prepare Clang call snapshot");
        return;
    }
    insight_relation_tree_calls(wb.relation_tree, j->calls, j->usr, j->query, j->incoming, j->source);
    insight_relation_tree_filter(wb.relation_tree, gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));
    set_usage_status(j->usr ? "Clang call snapshot; expand nodes for callers or callees":"Choose a function overload in the Clang tree");
    if (g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)), "routes")) gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle)) ? "treegraph":"tree");
}

/* EN: Schedule a semantic call tree for query, using incoming to choose direction.
 * RU: Планирует построение дерева для query. incoming задаёт направление. */
static void semantic_call_tree(const gchar *query, gboolean incoming)
{
    SemanticCallsJob *j = g_new0(SemanticCallsJob, 1);
    j->base = insight_index_ref(wb.disk_calls);
    j->paths = g_ptr_array_new_with_free_func(g_free);
    j->overlays = g_ptr_array_new_with_free_func((GDestroyNotify)insight_index_free);
    j->query = g_strdup(query);
    j->incoming = incoming;
    j->generation = wb.usage_generation;
    j->epoch = workbench_epoch;
    j->buffers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    j->resources = g_build_filename(app->datadir, "insight", "clang", "resources", NULL);
    if (app->project && wb.root) j->root = utils_get_utf8_from_locale(wb.root);
    gsize bytes = 0;
    for (guint i = 0;i<documents_array->len;i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        if (!DOC_VALID(doc) || !doc->file_name) continue;
        gint length = sci_get_length(doc->editor->sci);
        if (length >= 0 && length <= 2*1024*1024 && bytes+(gsize)length <= 32u*1024u*1024u && g_hash_table_size(j->buffers)<256)
        {
            gchar *text = sci_get_contents(doc->editor->sci, length+1);
            if (!memchr(text, 0, length) && g_utf8_validate(text, length, NULL))
            {
                g_hash_table_insert(j->buffers, g_strdup(doc->file_name), text);
                bytes += length;
            }
            else g_free(text);
        }
        if (!insight_clang_source(doc->file_name)) continue;
        InsightClangResult *r = insight_clang_ui_result(doc->editor->sci);
        g_ptr_array_add(j->paths, g_strdup(doc->file_name));
        g_ptr_array_add(j->overlays, insight_clang_result_usable(r) ? insight_index_ref(r->relations):NULL);
    }
    gboolean fixed = g_object_steal_data(G_OBJECT(wb.usage_entry), "insight-semantic-restore") != NULL;
    j->usr = g_object_steal_data(G_OBJECT(wb.usage_entry), "insight-semantic-usr");
    j->source = g_object_steal_data(G_OBJECT(wb.usage_entry), "insight-semantic-source");
    if (!fixed)
    {
        GeanyDocument *doc = document_get_current();
        InsightClangResult *r = DOC_VALID(doc) ? insight_clang_ui_result(doc->editor->sci):NULL;
        if (insight_clang_result_usable(r)) j->usr = insight_clang_symbol_at(r->relations, doc->file_name, sci_get_current_position(doc->editor->sci), sci_get_current_line(doc->editor->sci)+1, query);
        j->source = DOC_VALID(doc) ? g_strdup(doc->file_name):NULL;
    }
    wb.search_worker = g_cancellable_new();
    GTask *task = g_task_new(NULL, wb.search_worker, semantic_calls_done, NULL);
    g_task_set_task_data(task, j, semantic_calls_free);
    g_task_run_in_thread(task, semantic_calls_run);
    g_object_unref(task);
    set_usage_status("Preparing Clang call snapshot…");
}
typedef struct
{
    GPtrArray *paths, *rows;
    GArray *colors;
    GHashTable *buffers;
    gchar *service, *root, *cache, *project, *query, *paint_path, *vivado;
    guint paint_doc;
    InsightSlangProject *result;
    guint64 epoch, generation, scan_generation;
    guint applied;
    gboolean valid, diagnostics, colors_only, hierarchy;
    guint cursor_line;
}
HdlJob;
