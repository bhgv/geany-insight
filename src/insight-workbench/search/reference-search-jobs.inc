/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Reference search workers and cached relation lookup.
 * RU: Фоновые задачи поиска ссылок и чтение кэша связей. */

static gboolean usage_step(gpointer);

/* EN: Release the search job from data.
 * RU: Освобождает задачу поиска из data. */
static void search_job_free(gpointer data)
{
    SearchJob *j = data;
    g_free(j->path);
    g_free(j->query);
    g_free(j->text);
    g_free(j->digest);
    g_free(j->cached_stamp);
    g_free(j->cached_language);
    g_free(j->clang_root);
    g_free(j->clang_resources);
    g_free(j->usr);
    retire_index(j->cached_calls);
    g_free(j->found);
    g_clear_pointer(&j->snippets, g_ptr_array_unref);
    g_free(j->columns);
    g_free(j);
}

/* EN: Collect results for j from relations with source digest.
 * RU: Собирает результаты j из relations с контрольной суммой digest. */
static void clang_search_result(SearchJob *j, InsightIndex *relations, const gchar *digest)
{
    gsize total = insight_index_query(relations, j->query, TRUE, NULL, "@clangref", j->path, 0, NULL, 0);
    if (total>100000) return;
    const InsightSymbol **rows = g_new(const InsightSymbol *, total);
    insight_index_query(relations, j->query, TRUE, NULL, "@clangref", j->path, 0, rows, total);
    j->digest = g_strdup(digest);
    j->found = g_new0(InsightOccurrence, j->capacity);
    j->columns = g_new0(gulong, j->capacity);
    j->snippets = g_ptr_array_new_with_free_func(g_free);
    j->valid = TRUE;
    for (gsize i = 0;i<total;i++)
    {
        if (j->usr && strcmp(j->usr, rows[i]->scope)) continue;
        gsize n = j->total++;
        if (n >= j->capacity) continue;
        gchar **parts = g_strsplit(rows[i]->signature, ":", 3);
        if (g_strv_length(parts) != 3) j->valid = FALSE;
        else
        {
            j->found[n] = (InsightOccurrence)
            {
                g_ascii_strtoull(parts[0], NULL, 10), g_ascii_strtoull(parts[1], NULL, 10), rows[i]->line, 0
            };
            j->columns[n] = g_ascii_strtoull(parts[2], NULL, 10);
            g_ptr_array_add(j->snippets, g_strdup(rows[i]->type));
        }
        g_strfreev(parts);
    }
    g_free(rows);
}

/* EN: Search references for data and complete task. cancel stops work. source is unused.
 * RU: Ищет ссылки для data и завершает task. cancel отменяет работу. source не используется. */
static void search_job_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    SearchJob *j = data;
    if (j->mode == 8 && j->cached_calls && !j->text)
    {
        gchar *native = utils_get_locale_from_utf8(j->path), *now = source_stamp(native, j->cached_language, j->encoding);
        g_free(native);
        const InsightSymbol *coverage = NULL;
        gboolean usable = !g_strcmp0(now, j->cached_stamp) && insight_index_query(j->cached_calls, "@clangfile", TRUE, NULL, "@clangfile", j->path, 0, &coverage, 1) == 1 && !strcmp(coverage->type, "complete") && insight_clang_dependencies_fresh(j->cached_calls, j->path);
        g_free(now);
        if (usable)
        {
            clang_search_result(j, j->cached_calls, coverage->inheritance);
            g_task_return_boolean(task, TRUE);
            return;
        }
    }
    if (j->mode == 7 && !j->text && j->cached_calls && j->cached_stamp)
    {
        gchar *native = utils_get_locale_from_utf8(j->path);
        gchar *now = source_stamp(native, j->cached_language, j->encoding);
        g_free(native);
        const InsightSymbol *coverage = NULL;
        gboolean usable = !g_strcmp0(now, j->cached_stamp) && insight_index_query(j->cached_calls, "@file", TRUE, NULL, "@file", j->path, 0, &coverage, 1) == 1;
        g_free(now);
        if (usable)
        {
            j->digest = g_strdup(coverage->inheritance);
            j->total = insight_index_query(j->cached_calls, j->query, TRUE, NULL, "@call", j->path, 0, NULL, 0);
            gsize count = MIN(j->total, j->capacity);
            const InsightSymbol **rows = g_new(const InsightSymbol *, count);
            insight_index_query(j->cached_calls, j->query, TRUE, NULL, "@call", j->path, 0, rows, count);
            j->found = g_new0(InsightOccurrence, count);
            j->columns = g_new0(gulong, count);
            j->snippets = g_ptr_array_new_with_free_func(g_free);
            j->valid = TRUE;
            for (gsize n = 0;n<count;n++)
            {
                gchar **parts = g_strsplit(rows[n]->signature, ":", 3);
                if (g_strv_length(parts) != 3) j->valid = FALSE;
                else
                {
                    j->found[n] = (InsightOccurrence)
                    {
                        g_ascii_strtoull(parts[0], NULL, 10), g_ascii_strtoull(parts[1], NULL, 10), rows[n]->line, 0
                    };
                    j->columns[n] = g_ascii_strtoull(parts[2], NULL, 10);
                    g_ptr_array_add(j->snippets, g_strdup(rows[n]->type));
                }
                g_strfreev(parts);
            }
            g_free(rows);
            g_task_return_boolean(task, TRUE);
            return;
        }
    }
    if (!j->text && j->path && !g_cancellable_is_cancelled(cancel)) j->text = read_disk_source_full(j->path, TRUE, j->encoding, cancel);
    if (j->mode == 8)
    {
        if (j->text && insight_clang_source(j->path))
        {
            InsightClangResult *r = insight_clang_parse(j->path, j->text, j->clang_root, j->clang_resources, NULL, cancel, NULL);
            if (r && !r->limited)
            {
                clang_search_result(j, r->relations, r->digest);
                j->partial = r->errors || r->ambiguous_command;
            }
            insight_clang_result_free(r);
        }
        g_task_return_boolean(task, TRUE);
        return;
    }
    char *mask = j->text && j->region != INSIGHT_REGION_ALL && j->have_dialect ? insight_source_region_mask(j->text, strlen(j->text), j->dialect, j->region, NULL) : NULL;
    const char *text = j->region != INSIGHT_REGION_ALL ? mask : j->text;
    j->found = g_new(InsightOccurrence, j->capacity);
    if (text && !g_cancellable_is_cancelled(cancel))
    {
        if (j->mode == 3)
        {
            InsightSearchMatch *matches = g_new(InsightSearchMatch, j->capacity);
            j->valid = j->keywords ? insight_keyword_collect(text, strlen(text), j->query, j->flags, j->context, matches, j->capacity, &j->total, NULL) : insight_context_collect(text, strlen(text), j->query, j->flags, matches, j->capacity, &j->total, NULL);
            if (j->valid) for (gsize n = 0;n<MIN(j->total, j->capacity);n++) j->found[n] = (InsightOccurrence)
            {
                matches[n].start, matches[n].end, matches[n].line, matches[n].column
            };
            g_free(matches);
        }
        else j->valid = insight_occurrences_find(text, strlen(text), j->query, strlen(j->query), 0, j->found, j->capacity, &j->total);
        if (j->valid && j->mode == 7)
        {
            gsize kept = 0;
            size_t text_length = strlen(text);
            for (gsize n = 0;n<MIN(j->total, j->capacity);n++)
            {
                size_t cursor = j->found[n].start, a = 0, b = 0;
                if (insight_next_call(text, text_length, &cursor, &a, &b) == 1 && a == j->found[n].start && b == j->found[n].end) j->found[kept++] = j->found[n];
            }
            j->total = kept;
        }
        if (j->valid)
        {
            j->digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, j->text, -1);
            gsize count = MIN(j->total, j->capacity);
            j->snippets = g_ptr_array_new_with_free_func(g_free);
            j->columns = g_new(gulong, count);
            for (gsize n = 0;n<count && !g_cancellable_is_cancelled(cancel);n++)
            {
                InsightOccurrence *hit = &j->found[n];
                g_ptr_array_add(j->snippets, reference_snippet(j->text, hit->start));
                j->columns[n] = (gulong)g_utf8_strlen(j->text+hit->start-hit->column, (gssize)hit->column)+1;
            }
            if (g_cancellable_is_cancelled(cancel)) j->valid = FALSE;
        }
    }
    g_free(mask);
    g_task_return_boolean(task, TRUE);
}
