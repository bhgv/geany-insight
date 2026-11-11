/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: GDB session state, command queue and watch graph.
 * RU: Состояние GDB, очередь команд и граф наблюдения. */

typedef enum {
    NATIVE_RUN, NATIVE_CONTINUE, NATIVE_INTO, NATIVE_OVER, NATIVE_OUT,
    NATIVE_PAUSE, NATIVE_STACK, NATIVE_LOCALS, NATIVE_STOP, NATIVE_RESTART, NATIVE_UNTIL, NATIVE_ACTION_COUNT
} NativeDebugAction;

typedef struct {
    gchar *expression, *value;
} NativeWatch;

typedef struct
{
    gchar *location, *condition, *number;
    GPtrArray *disabled_locations;
    gboolean enabled;
    guint ignore;
} NativeSavedPoint;

typedef enum
{
    NATIVE_CONTEXT_NONE, NATIVE_CONTEXT_THREADS, NATIVE_CONTEXT_STACK,
    NATIVE_CONTEXT_SELECT_THREAD, NATIVE_CONTEXT_SELECT_FRAME, NATIVE_CONTEXT_ALL_STACK
} NativeContextRequest;

typedef enum
{
    NATIVE_DATA_INSPECT, NATIVE_DATA_WATCH, NATIVE_DATA_ASSIGN,
    NATIVE_DATA_WRITE, NATIVE_DATA_READ, NATIVE_DATA_ACCESS, NATIVE_DATA_MEMORY
} NativeDataAction;

typedef enum
{
    NATIVE_VALUE_NONE, NATIVE_VALUE_CREATE, NATIVE_VALUE_CHILDREN,
    NATIVE_VALUE_ASSIGN, NATIVE_VALUE_DELETE, NATIVE_VALUE_PATH
} NativeValueRequest;

typedef struct {
    NativeRemote *remote;
    GSubprocess *process;
    GDataInputStream *output;
    GCancellable *cancel;
    GtkWidget *status, *command, *location, *breakpoint, *load_lua, *unload_lua, *source, *buttons[NATIVE_ACTION_COUNT];
    GtkTextView *console, *watches;
    GtkWidget *inspection, *observer_log, *tabs, *watch_entry, *watch_add, *watch_remove;
    GPtrArray *expressions, *locals, *saved_source_points, *profile_disabled_pending;
    guint profile_insert_next, profile_insert_count, profile_insert_index;
    gboolean profile_insert_pending;
    gboolean profile_restoring, profile_restore_failed;
    GtkListStore *local_rows, *point_rows, *stack_rows;
    GtkWidget *threads, *stack;
    GtkWidget *window;
    GPtrArray *paused_controls;
    GtkWidget *memory_expression, *memory_size, *memory_auto, *memory_text, *memory_status;
    gchar *memory_address;
    guint memory_generation, memory_pending_generation, memory_count;
    gboolean memory_needed, memory_pending;
    GtkTreeStore *value_rows;
    GtkWidget *value_tree, *value_expression, *value_status, *locals_view;
    gchar *value_root, *value_pending_name, *value_pending_expression, *stop_detail, *data_command;
    GtkTreeRowReference *value_pending_row;
    GQueue value_cleanup;
    NativeValueRequest value_request;
    guint value_generation, value_pending_generation, value_serial, value_offset, value_nodes;
    NativeDataAction value_path_action;
    gboolean value_reload, data_refresh_pending;
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

static void native_profile_points_snapshot(NativeDebug *session, const gchar *record);
static void native_profile_debug_reply(NativeDebug *session, const gchar *record);
static void native_remote_drop(NativeDebug *session);
static void native_remote_update(NativeDebug *session);
static void native_remote_restore(NativeDebug *session);
static gboolean native_remote_selected(NativeDebug *session);
static gboolean native_remote_launch(NativeDebug *session);
static gchar *native_remote_next(NativeDebug *session);
static gboolean native_remote_reply(NativeDebug *session, const gchar *record);
static gchar *native_launch_arguments(const gchar *text, GError **error);
static void native_debug_resume(GtkWidget *dialog, NativeDebug *session);
static void native_memory_sync(NativeDebug *session);
static gchar *native_memory_next(NativeDebug *session);
static gboolean native_memory_reply(NativeDebug *session, const gchar *record);
static void native_memory_inspect(NativeDebug *session, const gchar *expression);
static void native_target_clear(NativeDebug *session);
static void native_values_reset(NativeDebug *session);
static gchar *native_values_next(NativeDebug *session, gboolean cleanup);
static gboolean native_values_reply(NativeDebug *session, const gchar *record);
static void native_data_refresh(NativeDebug *session);
static void native_data_use_expression(NativeDebug *session, const gchar *expression, NativeDataAction action);
static void native_assign_dialog(NativeDebug *session, const gchar *expression, const gchar *value,
                                 const gchar *object);
static void native_run_to_cursor(GtkWidget *dialog);
static gchar *native_stop_detail(const gchar *record);
static gchar *native_launch_next(NativeDebug *session);
static void native_source_stopped(NativeDebug *session);
static gchar *native_point_key(const gchar *filename, guint line);
static gboolean native_attach_interrupt(NativeDebug *session, const gchar *command);
static void native_attach_stopped(NativeDebug *session, const gchar *record);
static gboolean native_attach_interrupt_reply(NativeDebug *session, const gchar *line);
static void native_profile_command_sent(NativeDebug *session, const gchar *command);
static void native_debug_send(GtkWidget *dialog, const gchar *command);
static void native_all_stacks_reply(NativeDebug *session, const gchar *record);
static gboolean native_context_id(const gchar *value);

/* EN: Refresh controls and status for session.
 * RU: Обновляет кнопки и состояние session. */
static void native_debug_update(NativeDebug *session)
{
    native_memory_sync(session);
    gboolean idle = session->ready && !session->pending && !session->writing;
    const gchar *state = insight_tr(!session->ready ? "Starting or disconnected" :
        session->running ? "Running" : session->stopped ? "Paused" : "Ready");
    gchar *status = session->stopped && session->stop_detail ?
        g_strdup_printf("%s — %s", state, session->stop_detail) : g_strdup(state);
    gtk_label_set_text(GTK_LABEL(session->status), status);
    g_free(status);
    for (guint i = 0; i < session->paused_controls->len; i++)
        gtk_widget_set_sensitive(session->paused_controls->pdata[i], idle && session->stopped);
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
    native_remote_update(session);
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

/* EN: Release the closed GDB session in data.
 * RU: Освобождает закрытый сеанс GDB из data. */
static void native_debug_free(gpointer data)
{
    NativeDebug *session = data;
    if (session->remote)
    {
        native_remote_drop(session);
        g_free(session->remote->source_mapping);
        g_free(session->remote->original_sysroot);
        g_free(session->remote->profile_path);
        g_free(session->remote);
    }
    insight_lua_debug_free(session->observer);
    g_hash_table_unref(session->editor_points);
    g_free(session->editor_point_pending);
    g_free(session->stop_detail);
    g_free(session->data_command);
    g_free(session->memory_address);
    g_ptr_array_unref(session->paused_controls);
    g_object_unref(session->value_rows);
    g_free(session->value_root);
    g_free(session->value_pending_name);
    g_free(session->value_pending_expression);
    if (session->value_pending_row)
        gtk_tree_row_reference_free(session->value_pending_row);
    insight_queue_clear_full(&session->value_cleanup, g_free);
    g_ptr_array_unref(session->saved_source_points);
    g_ptr_array_unref(session->profile_disabled_pending);
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
    native_remote_drop(session);
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
    gchar *remote = native_remote_next(session);
    if (remote)
    {
        native_debug_send(dialog, remote);
        g_free(remote);
        return;
    }
    if (session->remote && session->remote->busy && session->remote->timer)
        return;
    gchar *launch = native_launch_next(session);
    if (launch)
    {
        native_debug_send(dialog, launch);
        g_free(launch);
        return;
    }
    if (session->data_command)
    {
        gchar *command = g_steal_pointer(&session->data_command);
        native_debug_send(dialog, command);
        g_free(command);
        return;
    }
    if (session->points_needed)
    {
        session->points_needed = FALSE;
        session->points_pending = TRUE;
        native_debug_send(dialog, "-break-list");
        return;
    }
    gchar *value_cleanup = native_values_next(session, TRUE);
    if (value_cleanup)
    {
        native_debug_send(dialog, value_cleanup);
        g_free(value_cleanup);
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
    else if (session->memory_needed)
    {
        gchar *command = native_memory_next(session);
        if (command)
        {
            native_debug_send(dialog, command);
            g_free(command);
        }
    }
    else if (session->value_reload)
    {
        gchar *command = native_values_next(session, FALSE);
        if (command)
        {
            native_debug_send(dialog, command);
            g_free(command);
        }
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
        "-exec-next", "-exec-finish", "-interpreter-exec", "-exec-until",
        "-var-create", "-var-list-children", "-var-assign", "-break-watch",
        "-data-read-memory-bytes"
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
    if (native_attach_interrupt(session, command))
        return;
    if (session->remote && !strcmp(command, "-gdb-show sysroot"))
        session->remote->pending = NATIVE_REMOTE_SYSROOT;
    if (session->remote && g_str_has_prefix(command, "-target-select "))
        session->remote->pending = NATIVE_REMOTE_CONNECT;
    native_profile_command_sent(session, command);
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
