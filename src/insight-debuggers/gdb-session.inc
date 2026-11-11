/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: GDB session state, command queue and watch graph.
 * RU: Состояние GDB, очередь команд и граф наблюдения. */

typedef enum {
    NATIVE_RUN, NATIVE_CONTINUE, NATIVE_INTO, NATIVE_OVER, NATIVE_OUT,
    NATIVE_PAUSE, NATIVE_STACK, NATIVE_LOCALS, NATIVE_STOP, NATIVE_RESTART, NATIVE_ACTION_COUNT
} NativeDebugAction;

typedef struct {
    gchar *expression, *value;
} NativeWatch;

typedef enum
{
    NATIVE_CONTEXT_NONE, NATIVE_CONTEXT_THREADS, NATIVE_CONTEXT_STACK,
    NATIVE_CONTEXT_SELECT_THREAD, NATIVE_CONTEXT_SELECT_FRAME, NATIVE_CONTEXT_ALL_STACK
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
    GtkWidget *launch_args, *launch_directory, *launch_status, *point_controls, *points_view;
    GQueue launch_commands;
    gboolean stop_requested, kill_pending, points_needed, points_pending, console_pending;
    GPtrArray *point_markers, *stop_markers;
    NativeContextRequest context_pending;
    GtkTreeStore *all_stack_rows;
    GtkWidget *all_stacks_page, *all_stacks_view, *all_stacks_status;
    GPtrArray *thread_ids;
    GHashTable *thread_roots;
    guint all_stack_next;
    gboolean all_stacks_visible, all_stacks_needed, frame_select_needed;
    gchar *all_stack_thread, *requested_frame, *selected_thread, *selected_frame;
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
    gboolean closed, ready, running, stopped, pending, writing;
} NativeDebug;

static void native_target_clear(NativeDebug *session);
static gchar *native_launch_next(NativeDebug *session);
static void native_source_stopped(NativeDebug *session);
static gchar *native_point_key(const gchar *filename, guint line);
static void native_debug_send(GtkWidget *dialog, const gchar *command);
static void native_all_stacks_reply(NativeDebug *session, const gchar *record);
static gboolean native_context_id(const gchar *value);

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
             i == NATIVE_STOP ? session->running || session->stopped :
             i == NATIVE_RESTART ? TRUE :
             i == NATIVE_RUN ? !session->running && !session->stopped :
                              session->stopped && !session->running));
    gtk_widget_set_sensitive(session->launch_args, idle && !session->running);
    gtk_widget_set_sensitive(session->launch_directory, idle && !session->running);
    gtk_widget_set_sensitive(session->point_controls, idle && !session->running);
    gtk_widget_set_sensitive(session->threads, idle && session->stopped);
    gtk_widget_set_sensitive(session->stack, idle && session->stopped);
    if (session->all_stacks_view)
    {
        gtk_widget_set_sensitive(session->all_stacks_view, idle && session->stopped);
        if (session->running || session->all_stacks_needed)
            gtk_label_set_text(GTK_LABEL(session->all_stacks_status), insight_tr(
                session->running ? "Previous stop; pause to refresh thread stacks." :
                                   "Collecting thread stacks…"));
    }
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
    insight_queue_clear_full(&session->launch_commands, g_free);
    g_ptr_array_unref(session->point_markers);
    g_ptr_array_unref(session->stop_markers);
    g_object_unref(session->stack_rows);
    g_object_unref(session->all_stack_rows);
    g_ptr_array_unref(session->thread_ids);
    g_hash_table_unref(session->thread_roots);
    g_free(session->all_stack_thread);
    g_free(session->requested_frame);
    g_free(session->selected_thread);
    g_free(session->selected_frame);
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
    g_ptr_array_set_size(session->stop_markers, 0);
    g_ptr_array_set_size(session->point_markers, 0);
    g_cancellable_cancel(session->cancel);
    g_subprocess_force_exit(session->process);
}

static void native_debug_send(GtkWidget *dialog, const gchar *command);

/* EN: Send the next queued command for session in dialog if GDB is idle.
 * RU: Отправляет следующую команду session из dialog, когда GDB свободен. */
static void native_debug_resume(GtkWidget *dialog, NativeDebug *session)
{
    if (session->pending || session->writing || !session->ready || session->running)
        return;
    gchar *launch = native_launch_next(session);
    if (launch)
    {
        native_debug_send(dialog, launch);
        g_free(launch);
        return;
    }
    if (session->points_needed)
    {
        session->points_needed = FALSE;
        session->points_pending = TRUE;
        native_debug_send(dialog, "-break-list");
        return;
    }
    if (!session->stopped)
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
    else if (session->frame_select_needed)
    {
        session->frame_select_needed = FALSE;
        session->context_pending = NATIVE_CONTEXT_SELECT_FRAME;
        gchar *command = g_strdup_printf("-stack-select-frame %s", session->requested_frame);
        native_debug_send(dialog, command);
        g_free(command);
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
    else if (session->all_stacks_visible && session->all_stacks_needed)
    {
        if (session->all_stack_next < session->thread_ids->len)
        {
            const gchar *thread = session->thread_ids->pdata[session->all_stack_next++];
            g_free(session->all_stack_thread);
            session->all_stack_thread = g_strdup(thread);
            session->context_pending = NATIVE_CONTEXT_ALL_STACK;
            gchar *command = g_strdup_printf("-stack-list-frames --thread %s 0 63", thread);
            native_debug_send(dialog, command);
            g_free(command);
        }
        else
        {
            session->all_stacks_needed = FALSE;
            gtk_label_set_text(GTK_LABEL(session->all_stacks_status),
                               insight_tr("Stacks at the current stop (up to 64 threads, 64 frames each)."));
        }
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

/* EN: Bind frame-sensitive command to session's UI selection, preserving explicit scopes.
 * RU: Привязывает command к выбранному в интерфейсе кадру session, сохраняя явный контекст. */
static gchar *native_debug_scoped(NativeDebug *session, const gchar *command)
{
    const gchar *space = strchr(command, ' ');
    gsize length = space ? (gsize)(space - command) : strlen(command);
    const gchar *scoped[] = {
        "-stack-list-frames", "-stack-list-variables", "-stack-select-frame",
        "-data-evaluate-expression", "-exec-continue", "-exec-step",
        "-exec-next", "-exec-finish", "-interpreter-exec"
    };
    gboolean matches = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(scoped); i++)
        matches |= strlen(scoped[i]) == length && !strncmp(command, scoped[i], length);
    const gchar *explicit_thread = strstr(command, " --thread ");
    const gchar *quote = strchr(command, '"');
    if (!matches || !native_context_id(session->selected_thread) ||
        (explicit_thread && (!quote || explicit_thread < quote)))
        return g_strdup(command);
    gchar *verb = g_strndup(command, length);
    gchar *result = g_strdup_printf("%s --thread %s%s%s%s", verb, session->selected_thread,
        native_context_id(session->selected_frame) ? " --frame " : "",
        native_context_id(session->selected_frame) ? session->selected_frame : "",
        space ? space : "");
    g_free(verb);
    return result;
}

/* EN: Queue command for the GDB session attached to dialog.
 * RU: Ставит command в очередь GDB, связанного с dialog. */
static void native_debug_send(GtkWidget *dialog, const gchar *command)
{
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (session->closed || session->pending || session->writing)
        return;
    gchar *scoped = native_debug_scoped(session, command);
    gchar *line = g_strconcat(scoped, "\n", NULL);
    g_free(scoped);
    session->pending = session->writing = TRUE;
    native_debug_update(session);
    g_object_set_data_full(G_OBJECT(dialog), "native-debug-command", line, g_free);
    g_output_stream_write_all_async(g_subprocess_get_stdin_pipe(session->process),
        line, strlen(line), G_PRIORITY_DEFAULT, session->cancel,
        native_debug_written, g_object_ref(dialog));
}
