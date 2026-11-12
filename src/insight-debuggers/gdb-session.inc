/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: GDB session state, command queue and watch graph.
 * RU: Состояние GDB, очередь команд и граф наблюдения. */

#include "gdb-session/state.inc"

/* EN: Refresh controls and status for session.
 * RU: Обновляет кнопки и состояние session. */
static void native_debug_update(NativeDebug *session)
{
    native_memory_sync(session);
    native_assembly_context(session);
    gboolean idle = session->ready && !session->pending && !session->writing &&
        (!session->assembly || !session->assembly->active);
    const gchar *state = insight_tr(!session->ready ? "Starting or disconnected" :
        session->running ? "Running" : session->stopped ? "Paused" : "Ready");
    gchar *status = session->stopped && session->stop_detail ?
        g_strdup_printf("%s — %s", state, session->stop_detail) : g_strdup(state);
    gtk_label_set_text(GTK_LABEL(session->status), status);
    g_free(status);
    for (guint i = 0; i < session->paused_controls->len; i++)
        gtk_widget_set_sensitive(session->paused_controls->pdata[i], idle && session->stopped);
    native_navigation_controls(session);
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
    native_assembly_free(session->assembly);
    g_ptr_array_unref(session->paused_controls);
    g_object_unref(session->value_rows);
    g_free(session->value_root);
    g_free(session->value_pending_name);
    g_free(session->value_pending_expression);
    if (session->value_pending_row)
        gtk_tree_row_reference_free(session->value_pending_row);
    insight_queue_clear_full(&session->value_cleanup, g_free);
    g_clear_pointer(&session->profile_restore_data, g_key_file_unref);
    g_ptr_array_unref(session->saved_points);
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
    if (session->remote && session->remote->release_pending)
        return;
    if (session->remote && session->remote->busy && session->remote->timer)
        return;
    if (native_assembly_next(session))
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
        "-data-read-memory-bytes", "-data-disassemble", "-stack-info-frame",
        "-exec-step-instruction", "-exec-next-instruction", "-data-list-register-values"
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

/* EN: Send command to dialog's GDB session and record its frame generation before the asynchronous write.
 * Busy/closed sessions ignore the request; the write callback reports transport errors.
 * RU: Отправляет command сеансу GDB окна dialog и запоминает поколение кадра перед асинхронной записью.
 * Занятый/закрытый сеанс игнорирует запрос; обработчик записи сообщает ошибки транспорта. */
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
    session->frame_pending_generation = session->value_generation;
    session->pending = session->writing = TRUE;
    native_debug_update(session);
    g_object_set_data_full(G_OBJECT(dialog), "native-debug-command", line, g_free);
    g_output_stream_write_all_async(g_subprocess_get_stdin_pipe(session->process),
        line, strlen(line), G_PRIORITY_DEFAULT, session->cancel,
        native_debug_written, g_object_ref(dialog));
}
