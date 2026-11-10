/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Background symbol relation queries.
 * RU: Фоновые запросы связей символов. */

static void usage_start(GtkWidget *, gpointer);

/* EN: Release the relation query in data.
 * RU: Освобождает запрос связей из data. */
static void relation_job_free(gpointer data)
{
    RelationJob *j = data;
    insight_language_filter_free(j->language);
    retire_index(j->index);
    g_free(j->query);
    g_clear_pointer(&j->descriptions, g_ptr_array_unref);
    g_clear_pointer(&j->existing, g_hash_table_destroy);
    g_free(j);
}

/* EN: Check whether symbol matches the language filter in data.
 * RU: Проверяет соответствие symbol языковому фильтру из data. */
static int relation_eligible(const InsightSymbol *symbol, void *data)
{
    return insight_language_filter_match(data, symbol->path);
}

/* EN: Query relations for data and complete task. cancel stops work. source is unused.
 * RU: Ищет связи для data и завершает task. cancel отменяет работу. source не используется. */
static void relation_job_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    RelationJob *j = data;
    j->total = SIZE_MAX;
    InsightRelationMode mode = j->mode == 1 ? INSIGHT_RELATION_DEFINITIONS : (j->mode == 2 ? INSIGHT_RELATION_MEMBERS : (j->mode == 4 ? INSIGHT_RELATION_BASES : INSIGHT_RELATION_DERIVED));
    if (!g_cancellable_is_cancelled(cancel))
    {
        if (mode != INSIGHT_RELATION_DEFINITIONS) j->total = insight_relation_query_transitive(j->index, j->query, mode, j->transitive ? 32 : (mode == INSIGHT_RELATION_MEMBERS ? 0 : 1), relation_eligible, j->language, 0, j->found, G_N_ELEMENTS(j->found), j->transitive ? &j->depth_limited : NULL, &j->ambiguous);
        else j->total = insight_relation_query(j->index, j->query, mode, 0, j->found, G_N_ELEMENTS(j->found));
    }
    if (j->total != SIZE_MAX)
    {
        gsize eligible = 0;
        gsize original = j->total;
        j->limited = original>G_N_ELEMENTS(j->found);
        for (gsize n = 0;n<MIN(original, G_N_ELEMENTS(j->found));n++) if (insight_language_filter_match(j->language, j->found[n]->path)) j->found[eligible++] = j->found[n];
        j->total = eligible;
    }
    j->descriptions = g_ptr_array_new_with_free_func(g_free);
    if (j->total != SIZE_MAX) for (gsize n = 0;n<MIN(j->total, G_N_ELEMENTS(j->found)) && !g_cancellable_is_cancelled(cancel);n++)
    {
        const InsightSymbol *tag = j->found[n];
        g_ptr_array_add(j->descriptions, g_strdup_printf("%s • %s %s%s • %s • bases: %s", tag->kind, tag->type, tag->name, tag->signature, tag->scope, tag->inheritance));
    }
    g_task_return_boolean(task, !g_cancellable_is_cancelled(cancel));
}
