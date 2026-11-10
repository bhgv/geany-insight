/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Reference search rows, results and incremental updates.
 * RU: Строки поиска ссылок, результаты и поэтапное обновление. */


/* EN: Build a unique key from a result row’s displayed symbol, source coordinates and document identity.
 * RU: Строит уникальный ключ из символа, координат исходника и идентификатора документа. */
static gchar *search_row_key(const gchar *display, const gchar *path, const gchar *name, const gchar *snippet, const gchar *digest, gulong line, gulong start, gulong end, guint id, gulong column)
{
    GString *key = g_string_new(NULL);
    const gchar *fields[] =
    {
        display, path, name, snippet, digest
    };
    for (guint n = 0;n<G_N_ELEMENTS(fields);n++)
    {
        if (fields[n])
            g_string_append_printf(key, "%" G_GSIZE_FORMAT ":%s", strlen(fields[n]), fields[n]);
        else g_string_append(key, "-1:");
    }
    g_string_append_printf(key, ":%lu:%lu:%lu:%u:%lu", line, start, end, id, column);
    return g_string_free(key, FALSE);
}

/* EN: Return the key for iter in model.
 * RU: Возвращает ключ iter в model. */
static gchar *search_existing_key(GtkTreeModel *model, GtkTreeIter *iter)
{
    gchar *display = NULL, *path = NULL, *name = NULL, *snippet = NULL, *digest = NULL;
    gulong line = 0, start = 0, end = 0, column = 0;
    guint id = 0;
    gtk_tree_model_get(model, iter, USAGE_DISPLAY, &display, USAGE_PATH, &path, USAGE_SYMBOL, &name, USAGE_SNIPPET, &snippet, USAGE_DIGEST, &digest, USAGE_LINE, &line, USAGE_START, &start, USAGE_END, &end, USAGE_ID, &id, USAGE_COLUMN, &column, -1);
    gchar *key = search_row_key(display, path, name, snippet, digest, line, start, end, id, column);
    g_free(display);
    g_free(path);
    g_free(name);
    g_free(snippet);
    g_free(digest);
    return key;
}

/* EN: Apply a batch of results from data to the UI.
 * RU: Добавляет порцию результатов из data в интерфейс. */
static gboolean search_results_apply(gpointer data)
{
    GTask *task = data;
    SearchJob *j = g_task_get_task_data(task);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.usage_generation)
        return G_SOURCE_REMOVE;
    gint64 deadline = g_get_monotonic_time()+3000;
    while (j->apply_position<MIN(j->total, j->capacity))
    {
        gsize n = j->apply_position++;
        InsightOccurrence *hit = &j->found[n];
        GtkTreeIter iter;
        gchar *key = search_row_key(j->path ? j->path : "untitled", j->path, j->query, g_ptr_array_index(j->snippets, n), j->digest, hit->line, hit->start, hit->end, j->id, j->columns[n]);
        GQueue *q = g_hash_table_lookup(wb.usage_existing, key);
        GtkTreeIter *old = q ? g_queue_pop_head(q) : NULL;
        g_free(key);
        if (old)
        {
            iter = *old;
            g_free(old);
        }
        else
        {
            gtk_list_store_append(wb.usage_target, &iter);
            gtk_list_store_set(wb.usage_target, &iter, USAGE_DISPLAY, j->path ? j->path : "untitled", USAGE_PATH, j->path, USAGE_LINE, hit->line, USAGE_COLUMN, j->columns[n], USAGE_START, (gulong)hit->start, USAGE_END, (gulong)hit->end, USAGE_ID, j->id, USAGE_DIGEST, j->digest, USAGE_SYMBOL, j->query, USAGE_SNIPPET, (gchar *)g_ptr_array_index(j->snippets, n), -1);
        }
        if (j->mode == 3 && wb.search_links)
            insight_search_links_capture(wb.search_links, &iter);
        wb.usage_shown++;
        if (g_get_monotonic_time() >= deadline)
            return G_SOURCE_CONTINUE;
    }
    gchar *label = g_strdup_printf("Searching… %u results • %u/%u files", wb.usage_shown, wb.usage_position, wb.usage_paths->len);
    set_usage_status(label);
    g_free(label);
    wb.usage_idle = g_idle_add(usage_step, NULL);
    return G_SOURCE_REMOVE;
}

/* EN: Publish result for the current search. source and data are unused.
 * RU: Публикует result текущего поиска. source и data не используются. */
static void search_job_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    SearchJob *j = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.usage_generation) return;
    g_clear_object(&wb.search_worker);
    if (j->partial)
        wb.usage_skipped++;
    if (ok && j->valid)
        wb.usage_idle = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, search_results_apply, g_object_ref(task), g_object_unref);
    else
    {
        wb.usage_skipped++;
        wb.usage_idle = g_idle_add(usage_step, NULL);
    }
}

/* EN: Advance the reference search by one idle slice. data is unused.
 * RU: Продвигает поиск ссылок за один проход. data не используется. */
static gboolean usage_step(gpointer data)
{
    (void)data;
    gint64 deadline = g_get_monotonic_time()+3000;
    if (wb.usage_collecting)
    {
        while (wb.usage_iter_valid)
        {
            gchar *key = search_existing_key(GTK_TREE_MODEL(wb.usage_target), &wb.usage_iter);
            GQueue *q = g_hash_table_lookup(wb.usage_existing, key);
            if (!q)
            {
                q = g_queue_new();
                g_hash_table_insert(wb.usage_existing, key, q);
            }
            else g_free(key);
            GtkTreeIter *copy = g_new(GtkTreeIter, 1);
            *copy = wb.usage_iter;
            g_queue_push_tail(q, copy);
            wb.usage_iter_valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(wb.usage_target), &wb.usage_iter);
            if (g_get_monotonic_time() >= deadline)
                return G_SOURCE_CONTINUE;
        }
        wb.usage_collecting = FALSE;
    }
    if (wb.usage_position >= wb.usage_paths->len || wb.usage_shown >= 2000)
    {
        GHashTableIter pending;
        gpointer value;
        g_hash_table_iter_init(&pending, wb.usage_existing);
        while (g_hash_table_iter_next(&pending, NULL, &value))
        {
            GQueue *q = value;
            GtkTreeIter *old;
            while ((old = g_queue_pop_head(q)))
            {
                gtk_list_store_remove(wb.usage_target, old);
                g_free(old);
                if (g_get_monotonic_time() >= deadline)
                    return G_SOURCE_CONTINUE;
            }
            g_hash_table_iter_remove(&pending);
        }
        g_clear_pointer(&wb.usage_existing, g_hash_table_destroy);
        wb.usage_idle = 0;
        gchar *label = g_strdup_printf("%u results • %u files scanned • %u skipped%s", wb.usage_shown, wb.usage_position, wb.usage_skipped, wb.usage_shown >= 2000 ? " • result limit reached" : "");
        if (wb.usage_mode == 8)
        {
            gchar *semantic = g_strconcat(insight_tr(wb.usage_usr ? "Clang symbol identity:":"Clang references by name:"), " ", label, NULL);
            g_free(label);
            label = semantic;
        }
        set_usage_status(label);
        g_free(label);
        return G_SOURCE_REMOVE;
    }
    Location *location = g_ptr_array_index(wb.usage_paths, wb.usage_position++);
    GeanyDocument *doc = location->id ? document_find_by_id(location->id) : document_find_by_filename(location->path);
    SearchJob *j = g_new0(SearchJob, 1);
    j->usr = g_strdup(wb.usage_usr);
    j->clang_root = wb.root ? utils_get_utf8_from_locale(wb.root):NULL;
    j->clang_resources = g_build_filename(app->datadir, "insight", "clang", "resources", NULL);
    j->path = g_strdup(location->path);
    j->query = g_strdup(wb.usage_name);
    j->id = doc ? doc->id : 0;
    j->mode = wb.usage_mode;
    j->flags = wb.usage_flags;
    j->keywords = wb.usage_keywords;
    j->context = wb.usage_context;
    j->capacity = 2000-wb.usage_shown;
    j->encoding = file_prefs.default_open_encoding;
    j->generation = wb.usage_generation;
    j->epoch = workbench_epoch;
    if (doc)
    {
        gint length = sci_get_length(doc->editor->sci);
        if (length >= 0 && length <= 2*1024*1024)
        {
            j->text = sci_get_contents(doc->editor->sci, length+1);
            if (j->text && (!g_utf8_validate(j->text, length, NULL) || memchr(j->text, 0, length)))
                g_clear_pointer(&j->text, g_free);
        }
    }
    if ((j->mode == 7 || j->mode == 8) && !doc && j->path)
    {
        j->cached_calls = insight_index_ref(wb.disk_calls);
        j->cached_stamp = g_strdup(g_hash_table_lookup(wb.file_stamps, j->path));
    }
    j->region = j->mode == 7 ? INSIGHT_REGION_CODE : j->mode == 0 ? wb.usage_region : j->mode == 3 ? wb.project_region : INSIGHT_REGION_ALL;
    GeanyFiletype *ft = doc ? doc->file_type : (j->path ? filetypes_detect_from_extension(j->path) : NULL);
    if (j->cached_stamp && (!ft || ft->id == GEANY_FILETYPES_NONE))
    {
        const gchar *colon = strchr(j->cached_stamp, ':');
        if (colon)
        {
            gchar *language = g_strndup(j->cached_stamp, colon-j->cached_stamp);
            GeanyFiletype *cached = filetypes_lookup_by_name(language);
            if (cached) ft = cached;
            g_free(language);
        }
    }
    j->have_dialect = source_dialect(ft, &j->dialect);
    j->cached_language = g_strdup(ft ? ft->name:NULL);
    wb.search_worker = g_cancellable_new();
    GTask *task = g_task_new(NULL, wb.search_worker, search_job_done, NULL);
    g_task_set_task_data(task, j, search_job_free);
    g_task_run_in_thread(task, search_job_run);
    g_object_unref(task);
    wb.usage_idle = 0;
    return G_SOURCE_REMOVE;
}

/* EN: Check whether path belongs to the active search scope.
 * RU: Проверяет, входит ли path в текущую область поиска. */
static gboolean search_path_matches(const char *path)
{
    const char *input = gtk_entry_get_text(GTK_ENTRY(wb.search_mask));
    if (!*input)
        return TRUE;
    gchar **patterns = g_strsplit(input, ";", -1), *name = g_path_get_basename(path), *folded = g_utf8_casefold(name, -1);
    gboolean matched = FALSE;
    for (guint i = 0;patterns[i];i++)
    {
        gchar *pattern = g_utf8_casefold(g_strstrip(patterns[i]), -1);
        if (*pattern && g_pattern_match_simple(pattern, folded)) matched = TRUE;
        g_free(pattern);
    }
    g_free(folded);
    g_free(name);
    g_strfreev(patterns);
    return matched;
}
typedef struct
{
    InsightIndex *index;
    gchar *query;
    const InsightSymbol *found[2000];
    gsize total, apply_position;
    GPtrArray *descriptions;
    GHashTable *existing;
    GtkTreeIter iter;
    gboolean collecting, iter_valid, invalid;
    gint mode;
    guint64 epoch, generation, index_version;
    InsightLanguageFilter *language;
    gboolean limited, transitive;
    int depth_limited, ambiguous;
}
RelationJob;
