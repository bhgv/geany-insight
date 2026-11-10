/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Relation query results and list updates.
 * RU: Результаты поиска связей и обновление списка. */


/* EN: Return a row key from display, path, name, description and line.
 * RU: Возвращает ключ строки из display, path, name, description и line. */
static gchar *relation_row_key(const gchar *display, const gchar *path, const gchar *name, const gchar *description, gulong line)
{
    GString *key = g_string_new(NULL);
    const gchar *fields[] =
    {
        display, path, name, description
    };
    for (guint k = 0;k<G_N_ELEMENTS(fields);k++)
    {
        if (fields[k])
            g_string_append_printf(key, "%" G_GSIZE_FORMAT ":%s", strlen(fields[k]), fields[k]);
        else g_string_append(key, "-1:");
    }
    g_string_append_printf(key, ":%lu", line);
    return g_string_free(key, FALSE);
}

/* EN: Return the key for iter in model.
 * RU: Возвращает ключ iter в model. */
static gchar *relation_existing_key(GtkTreeModel *model, GtkTreeIter *iter)
{
    gchar *display = NULL, *path = NULL, *name = NULL, *description = NULL, *digest = NULL;
    gulong line = 0, start = 0, end = 0, column = 0;
    guint id = 0;
    gtk_tree_model_get(model, iter, USAGE_DISPLAY, &display, USAGE_PATH, &path, USAGE_SYMBOL, &name, USAGE_SNIPPET, &description, USAGE_LINE, &line, USAGE_START, &start, USAGE_END, &end, USAGE_ID, &id, USAGE_COLUMN, &column, USAGE_DIGEST, &digest, -1);
    gchar *key = relation_row_key(display, path, name, description, line);
    /* Reference rows also carry hit coordinates/digests and must not be reused
  * as definition rows even when their visible strings happen to coincide. */
    if (start || end || id || column || digest)
    {
        gchar *old = key;
        key = g_strconcat("reference:", old, NULL);
        g_free(old);
    }
    g_free(display);
    g_free(path);
    g_free(name);
    g_free(description);
    g_free(digest);
    return key;
}

/* EN: Apply a batch of relation rows from data.
 * RU: Добавляет порцию строк связей из data. */
static gboolean relation_results_apply(gpointer data)
{
    GTask *task = data;
    RelationJob *j = g_task_get_task_data(task);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.usage_generation)
        return G_SOURCE_REMOVE;
    if (j->index_version != wb.index_version)
    {
        wb.usage_idle = 0;
        usage_start(NULL, NULL);
        return G_SOURCE_REMOVE;
    }
    gint64 deadline = g_get_monotonic_time()+3000;
    if (j->collecting)
    {
        while (j->iter_valid)
        {
            gchar *key = relation_existing_key(GTK_TREE_MODEL(wb.matches), &j->iter);
            GQueue *q = g_hash_table_lookup(j->existing, key);
            if (!q)
            {
                q = g_queue_new();
                g_hash_table_insert(j->existing, key, q);
            }
            else g_free(key);
            GtkTreeIter *copy = g_new(GtkTreeIter, 1);
            *copy = j->iter;
            g_queue_push_tail(q, copy);
            j->iter_valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(wb.matches), &j->iter);
            if (g_get_monotonic_time() >= deadline)
                return G_SOURCE_CONTINUE;
        }
        j->collecting = FALSE;
    }
    while (j->apply_position<MIN(j->total, G_N_ELEMENTS(j->found)))
    {
        gsize n = j->apply_position++;
        const InsightSymbol *tag = j->found[n];
        const gchar *description = g_ptr_array_index(j->descriptions, n);
        gchar *key = relation_row_key(tag->path, tag->path, tag->name, description, tag->line);
        GQueue *q = g_hash_table_lookup(j->existing, key);
        GtkTreeIter *old = q ? g_queue_pop_head(q) : NULL;
        g_free(key);
        if (old)
            g_free(old);
        else
        {
            GtkTreeIter iter;
            gtk_list_store_append(wb.matches, &iter);
            gtk_list_store_set(wb.matches, &iter, USAGE_DISPLAY, tag->path, USAGE_PATH, tag->path, USAGE_LINE, tag->line, USAGE_SYMBOL, tag->name, USAGE_SNIPPET, description, -1);
        }
        if (g_get_monotonic_time() >= deadline)
            return G_SOURCE_CONTINUE;
    }
    GHashTableIter pending;
    gpointer value;
    g_hash_table_iter_init(&pending, j->existing);
    while (g_hash_table_iter_next(&pending, NULL, &value))
    {
        GQueue *q = value;
        GtkTreeIter *old;
        while ((old = g_queue_pop_head(q)))
        {
            gtk_list_store_remove(wb.matches, old);
            g_free(old);
            if (g_get_monotonic_time() >= deadline)
                return G_SOURCE_CONTINUE;
        }
        g_hash_table_iter_remove(&pending);
    }
    gchar *label = g_strdup_printf("%zu tag-based %s%s", j->total, j->mode == 1 ? "definitions/declarations" : (j->mode == 2 ? (j->transitive ? "direct and inherited members" : "direct members") : (j->mode == 4 ? "base definitions" : "derived types")), j->limited ? " • showing first 2000 matches" : (j->depth_limited ? " • inheritance depth limit reached" : (j->ambiguous ? " • ambiguous class names omitted" : "")));
    set_usage_status(j->invalid ? "Enter a symbol name (or qualified owner for Members)" : label);
    g_free(label);
    wb.usage_idle = 0;
    return G_SOURCE_REMOVE;
}

/* EN: Publish result for the active query. source and data are unused.
 * RU: Публикует result активного запроса. source и data не используются. */
static void relation_job_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    RelationJob *j = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.usage_generation) return;
    g_clear_object(&wb.search_worker);
    if (!ok) return;
    if (j->index_version != wb.index_version)
    {
        usage_start(NULL, NULL);
        return;
    }
    if (j->total == SIZE_MAX)
    {
        j->invalid = TRUE;
        j->total = 0;
    }
    if ((j->mode == 2 || j->mode == 4 || j->mode == 5) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle)))
    {
        GeanyDocument *doc = document_get_current();
        insight_relation_tree_query(wb.relation_tree, j->index, j->query, j->mode == 2 ? INSIGHT_RELATION_MEMBERS : (j->mode == 4 ? INSIGHT_RELATION_BASES : INSIGHT_RELATION_DERIVED), doc ? doc->file_name : NULL, doc ? doc->file_type : NULL);
        insight_relation_tree_filter(wb.relation_tree, gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));
    }
    j->existing = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, symbol_iter_queue_free);
    j->collecting = TRUE;
    j->iter_valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(wb.matches), &j->iter);
    wb.usage_idle = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, relation_results_apply, g_object_ref(task), g_object_unref);
}
typedef struct
{
    InsightIndex *base, *calls;
    GPtrArray *paths, *overlays;
    GHashTable *buffers;
    gchar *resources, *root, *query, *usr, *source;
    gboolean incoming;
    guint64 generation, epoch;
}
SemanticCallsJob;
