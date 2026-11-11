/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Render native watches and frame values in the shared graph.
 * RU: Выводит наблюдения и значения кадра в общий граф. */

/* EN: Release the watch stored in data.
 * RU: Освобождает наблюдение из data. */
static void native_watch_free(gpointer data)
{
    NativeWatch *watch = data;
    g_free(watch->expression);
    g_free(watch->value);
    g_free(watch);
}

/* EN: Copy text to destination of size bytes without leaving a partial UTF-8 character.
 * RU: Копирует text в destination длиной size байт, не оставляя обрезанный символ UTF-8. */
static void native_watch_text(gchar *destination, gsize size, const gchar *text)
{
    g_strlcpy(destination, text, size);
    const gchar *end = NULL;
    if (!g_utf8_validate(destination, -1, &end))
        destination[end - destination] = '\0';
}

/* EN: Store a stable graph identity for kind/expression in destination; hash paths exceeding size.
 * RU: Сохраняет стабильный идентификатор kind/expression в destination; длинные пути size заменяет хэшем. */
static void native_watch_path(gchar *destination, gsize size, const gchar *kind, const gchar *expression)
{
    if (strlen(kind) + strlen(expression) + 2 <= size)
        g_snprintf(destination, size, "%s:%s", kind, expression);
    else
    {
        gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, expression, -1);
        g_snprintf(destination, size, "%s:hash:%s", kind, hash);
        g_free(hash);
    }
}

/* EN: Rebuild session watches and compare them with the previous snapshot if compare is true.
 * RU: Обновляет наблюдения session и сравнивает их с предыдущим снимком, если задан compare. */
static void native_watch_refresh(NativeDebug *session, gboolean compare)
{
    InsightLuaWatchGraph *graph = session->graph;
    memset(graph, 0, sizeof *graph);
    graph->node_count = 1;
    g_snprintf(graph->nodes[0].label, sizeof graph->nodes[0].label, "GDB: %s / #%s",
               session->selected_thread ? session->selected_thread : "—",
               session->selected_frame ? session->selected_frame : "0");
    for (guint i = 0; i < session->expressions->len; i++)
    {
        NativeWatch *watch = session->expressions->pdata[i];
        guint id = graph->node_count++;
        InsightLuaWatchNode *node = &graph->nodes[id];
        native_watch_text(node->label, sizeof node->label, watch->expression);
        native_watch_text(node->value, sizeof node->value, watch->value ? watch->value : insight_tr("Awaiting stop"));
        native_watch_path(node->path, sizeof node->path, "gdb", watch->expression);
        InsightLuaWatchEdge *edge = &graph->edges[graph->edge_count++];
        edge->from = 0;
        edge->to = id;
        g_strlcpy(edge->label, "watch", sizeof edge->label);
    }
    for (guint i = 0; i < session->locals->len &&
         graph->node_count < G_N_ELEMENTS(graph->nodes) &&
         graph->edge_count < G_N_ELEMENTS(graph->edges); i++)
    {
        NativeWatch *local = session->locals->pdata[i];
        guint id = graph->node_count++;
        InsightLuaWatchNode *node = &graph->nodes[id];
        native_watch_text(node->label, sizeof node->label, local->expression);
        native_watch_text(node->value, sizeof node->value, local->value);
        native_watch_path(node->path, sizeof node->path, "gdb:local", local->expression);
        InsightLuaWatchEdge *edge = &graph->edges[graph->edge_count++];
        edge->from = 0;
        edge->to = id;
        g_strlcpy(edge->label, "local", sizeof edge->label);
    }
    insight_lua_debug_observer_watch(session->observer, graph);
    session->graph_view->watch_selected = -1;
    GString *text = g_string_new(NULL);
    for (guint i = 0; i < session->expressions->len; i++)
    {
        NativeWatch *watch = session->expressions->pdata[i];
        g_string_append_printf(text, "%s = %s\n", watch->expression,
                               watch->value ? watch->value : insight_tr("Awaiting stop"));
    }
    for (guint i = 1; i < graph->node_count; i++)
        if (g_str_has_prefix(graph->nodes[i].path, "observer:"))
            g_string_append_printf(text, "%s = %s\n", graph->nodes[i].label, graph->nodes[i].value);
    gchar *valid = g_utf8_make_valid(text->str, -1);
    debug_set_text(GTK_WIDGET(session->watches), valid);
    g_free(valid);
    g_string_free(text, TRUE);
    if (compare)
        debug_watch_compare(session->graph_view);
    debug_watch_layout(session->graph_view);
}

