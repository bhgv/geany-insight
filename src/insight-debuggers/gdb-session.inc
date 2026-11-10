/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: GDB session state, command queue and watch graph.
 * RU: Состояние GDB, очередь команд и граф наблюдения. */

typedef enum {
    NATIVE_RUN, NATIVE_CONTINUE, NATIVE_INTO, NATIVE_OVER, NATIVE_OUT,
    NATIVE_PAUSE, NATIVE_STACK, NATIVE_LOCALS, NATIVE_ACTION_COUNT
} NativeDebugAction;

typedef struct {
    gchar *expression, *value;
} NativeWatch;

typedef enum
{
    NATIVE_CONTEXT_NONE, NATIVE_CONTEXT_THREADS, NATIVE_CONTEXT_STACK,
    NATIVE_CONTEXT_SELECT_THREAD, NATIVE_CONTEXT_SELECT_FRAME
} NativeContextRequest;

typedef struct {
    GSubprocess *process;
    GDataInputStream *output;
    GCancellable *cancel;
    GtkWidget *status, *command, *location, *breakpoint, *load_lua, *unload_lua, *source, *buttons[NATIVE_ACTION_COUNT];
    GtkTextView *console, *watches;
    GtkWidget *inspection, *observer_log, *tabs, *watch_entry, *watch_add, *watch_remove;
    GPtrArray *expressions, *locals;
    GtkListStore *local_rows, *point_rows, *stack_rows;
    GtkWidget *threads, *stack;
    NativeContextRequest context_pending;
    gboolean threads_needed, stack_needed, filling_context;
    gboolean locals_needed, locals_pending;
    ScriptDebugWindow *graph_view;
    InsightLuaWatchGraph *graph;
    guint watch_next;
    NativeWatch *watch_pending;
    gboolean inspecting;
    InsightLuaDebug *observer;
    gsize log_offset;
    gchar *stop_file;
    guint stop_line;
    gboolean resume;
    GHashTable *editor_points;
    gchar *editor_point_pending;
    gboolean editor_point_removing;
    gboolean closed, ready, running, stopped, pending, writing;
} NativeDebug;

/* EN: Refresh controls and status for session.
 * RU: Обновляет кнопки и состояние session. */
static void native_debug_update(NativeDebug *session)
{
    gboolean idle = session->ready && !session->pending && !session->writing;
    gtk_label_set_text(GTK_LABEL(session->status),
        insight_tr(!session->ready ? "Starting or disconnected" :
                   session->running ? "Running" : session->stopped ? "Paused" : "Ready"));
    for (guint i = 0; i < NATIVE_ACTION_COUNT; i++)
        gtk_widget_set_sensitive(session->buttons[i], idle &&
            (i == NATIVE_PAUSE ? session->running :
             i == NATIVE_RUN ? !session->running : session->stopped && !session->running));
    gtk_widget_set_sensitive(session->threads, idle && session->stopped);
    gtk_widget_set_sensitive(session->stack, idle && session->stopped);
    gtk_widget_set_sensitive(session->command, idle && !session->running);
    gtk_widget_set_sensitive(session->watch_entry, idle && !session->running);
    gtk_widget_set_sensitive(session->watch_add, idle && !session->running);
    gtk_widget_set_sensitive(session->watch_remove, idle && !session->running);
    gtk_widget_set_sensitive(session->location, idle && !session->running);
    gtk_widget_set_sensitive(session->breakpoint, idle && !session->running);
    gtk_widget_set_sensitive(session->source, session->stopped && session->stop_file && session->stop_line);
    gtk_widget_set_sensitive(session->load_lua, !session->running);
    gtk_widget_set_sensitive(session->unload_lua, !session->running && session->observer != NULL);
}

/* EN: Append line to the console of session.
 * RU: Добавляет line в журнал session. */
static void native_debug_log(NativeDebug *session, const gchar *line)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(session->console);
    GtkTextIter first, last;
    if (gtk_text_buffer_get_line_count(buffer) > 4000)
    {
        gtk_text_buffer_get_start_iter(buffer, &first);
        gtk_text_buffer_get_iter_at_line(buffer, &last, 500);
        gtk_text_buffer_delete(buffer, &first, &last);
    }
    gchar *text = g_utf8_make_valid(line, -1);
    gtk_text_buffer_get_end_iter(buffer, &last);
    gtk_text_buffer_insert(buffer, &last, text, -1);
    gtk_text_buffer_insert(buffer, &last, "\n", 1);
    gtk_text_view_scroll_to_iter(session->console, &last, 0, FALSE, 0, 0);
    g_free(text);
}

/* EN: Release the watch stored in data.
 * RU: Освобождает наблюдение из data. */
static void native_watch_free(gpointer data)
{
    NativeWatch *watch = data;
    g_free(watch->expression);
    g_free(watch->value);
    g_free(watch);
}

/* EN: Rebuild session watches and compare them with the previous snapshot if compare is true.
 * RU: Обновляет наблюдения session и сравнивает их с предыдущим снимком, если задан compare. */
static void native_watch_refresh(NativeDebug *session, gboolean compare)
{
    InsightLuaWatchGraph *graph = session->graph;
    memset(graph, 0, sizeof *graph);
    graph->node_count = 1;
    g_strlcpy(graph->nodes[0].label, "GDB frame", sizeof graph->nodes[0].label);
    for (guint i = 0; i < session->expressions->len; i++)
    {
        NativeWatch *watch = session->expressions->pdata[i];
        guint id = graph->node_count++;
        InsightLuaWatchNode *node = &graph->nodes[id];
        g_strlcpy(node->label, watch->expression, sizeof node->label);
        g_strlcpy(node->value, watch->value ? watch->value : "Awaiting stop", sizeof node->value);
        g_snprintf(node->path, sizeof node->path, "gdb:%s", watch->expression);
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
        g_strlcpy(node->label, local->expression, sizeof node->label);
        g_strlcpy(node->value, local->value, sizeof node->value);
        g_snprintf(node->path, sizeof node->path, "gdb:local:%s", local->expression);
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
                               watch->value ? watch->value : "Awaiting stop");
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

/* EN: Release the closed GDB session in data.
 * RU: Освобождает закрытый сеанс GDB из data. */
static void native_debug_free(gpointer data)
{
    NativeDebug *session = data;
    insight_lua_debug_free(session->observer);
    g_hash_table_unref(session->editor_points);
    g_free(session->editor_point_pending);
    g_ptr_array_unref(session->expressions);
    g_ptr_array_unref(session->locals);
    g_object_unref(session->local_rows);
    g_object_unref(session->point_rows);
    g_object_unref(session->stack_rows);
    g_ptr_array_unref(session->graph_view->watch_paths);
    g_clear_pointer(&session->graph_view->watch_previous_values, g_hash_table_unref);
    g_free(session->graph_view);
    g_free(session->graph);
    g_free(session->stop_file);
    g_object_unref(session->cancel);
    g_object_unref(session->output);
    g_object_unref(session->process);
    g_free(session);
}

/* EN: Stop the GDB session in data when dialog closes.
 * RU: Останавливает сеанс GDB из data при закрытии dialog. */
static void native_debug_close(GtkWidget *dialog, gpointer data)
{
    NativeDebug *session = data;
    (void)dialog;
    session->closed = TRUE;
    g_cancellable_cancel(session->cancel);
    g_subprocess_force_exit(session->process);
}

static void native_debug_send(GtkWidget *dialog, const gchar *command);

/* EN: Send the next queued command for session in dialog if GDB is idle.
 * RU: Отправляет следующую команду session из dialog, когда GDB свободен. */
static void native_debug_resume(GtkWidget *dialog, NativeDebug *session)
{
    if (session->pending || session->writing || !session->ready || !session->stopped)
        return;
    if (session->resume)
    {
        session->resume = FALSE;
        native_debug_send(dialog, "-exec-continue");
    }
    else if (session->threads_needed)
    {
        session->threads_needed = FALSE;
        session->context_pending = NATIVE_CONTEXT_THREADS;
        native_debug_send(dialog, "-thread-info");
    }
    else if (session->stack_needed)
    {
        session->stack_needed = FALSE;
        session->context_pending = NATIVE_CONTEXT_STACK;
        native_debug_send(dialog, "-stack-list-frames 0 63");
    }
    else if (session->locals_needed)
    {
        session->locals_needed = FALSE;
        session->locals_pending = TRUE;
        native_debug_send(dialog, "-stack-list-variables --all-values");
    }
    else if (session->watch_next < session->expressions->len)
    {
        session->watch_pending = session->expressions->pdata[session->watch_next++];
        gchar *escaped = g_strescape(session->watch_pending->expression, NULL);
        gchar *command = g_strdup_printf("-data-evaluate-expression \"%s\"", escaped);
        native_debug_send(dialog, command);
        g_free(command);
        g_free(escaped);
    }
}

/* EN: Finish writing to stream from result and advance dialog in data.
 * RU: Завершает запись в stream по result и продолжает работу dialog из data. */
static void native_debug_written(GObject *stream, GAsyncResult *result, gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    GError *error = NULL;
    gboolean ok = g_output_stream_write_all_finish(G_OUTPUT_STREAM(stream), result, NULL, &error);
    session->writing = FALSE;
    if (!session->closed)
    {
        if (!ok)
        {
            native_debug_log(session, error->message);
            session->pending = session->ready = session->resume = FALSE;
            session->watch_pending = NULL;
            g_clear_pointer(&session->editor_point_pending, g_free);
            session->locals_pending = FALSE;
            session->context_pending = NATIVE_CONTEXT_NONE;
        }
        native_debug_update(session);
        native_debug_resume(dialog, session);
    }
    g_clear_error(&error);
    g_object_unref(dialog);
}

/* EN: Queue command for the GDB session attached to dialog.
 * RU: Ставит command в очередь GDB, связанного с dialog. */
static void native_debug_send(GtkWidget *dialog, const gchar *command)
{
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (session->closed || session->pending || session->writing)
        return;
    gchar *line = g_strconcat(command, "\n", NULL);
    session->pending = session->writing = TRUE;
    native_debug_update(session);
    g_object_set_data_full(G_OBJECT(dialog), "native-debug-command", line, g_free);
    g_output_stream_write_all_async(g_subprocess_get_stdin_pipe(session->process),
        line, strlen(line), G_PRIORITY_DEFAULT, session->cancel,
        native_debug_written, g_object_ref(dialog));
}
