/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: GDB events, breakpoints and user actions.
 * RU: События GDB, точки останова и действия пользователя. */

/* EN: Read stop coordinates from record and notify observers of session.
 * RU: Читает позицию остановки из record и уведомляет наблюдателей session. */
static void native_debug_observe(NativeDebug *session, const gchar *record)
{
    g_free(session->stop_file);
    session->stop_file = native_debug_field(record, "fullname");
    gchar *line = native_debug_field(record, "line");
    guint64 number = 0;
    session->stop_line = line && g_ascii_string_to_unsigned(line, 10, 1, G_MAXINT, &number, NULL) ?
        (guint)number : 0;
    g_free(line);
    if (!session->stop_requested)
        native_source_stopped(session);
    if (session->stop_requested || !insight_lua_debug_observer_active(session->observer) ||
        !g_str_has_prefix(record, "*stopped,reason=\"breakpoint-hit\""))
        return;

    gchar *point = native_debug_field(record, "bkptno");
    gchar *function = native_debug_field(record, "func");
    InsightObserverValue values[] = {
        {.type = 's', .name = "breakpoint", .text = point ? point : "", .length = point ? strlen(point) : 0},
        {.type = 's', .name = "reason", .text = "breakpoint-hit", .length = sizeof("breakpoint-hit") - 1}
    };
    InsightObserverEvent event = {
        .kind = "native-point", .file = session->stop_file ? session->stop_file : "",
        .name = function ? function : "", .line = session->stop_line,
        .values = values, .count = G_N_ELEMENTS(values)
    };
    session->resume = insight_lua_debug_observe(session->observer, &event) == 0;
    const gchar *log = insight_lua_debug_observer_log(session->observer);
    gsize length = strlen(log);
    if (length > session->log_offset)
        native_debug_log(session, log + session->log_offset);
    session->log_offset = length;

    gchar *valid = g_utf8_make_valid(log, -1);
    debug_set_text(session->observer_log, valid);
    g_free(valid);
    g_free(point);
    g_free(function);
}

/* EN: Process a GDB line from stream and result for the dialog in data.
 * RU: Обрабатывает строку GDB из stream и result для диалога из data. */
static void native_debug_read(GObject *stream, GAsyncResult *result, gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    GError *error = NULL;
    gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(stream), result, NULL, &error);
    if (!session->closed && line)
    {
        g_strchomp(line);
        native_assembly_console(session, line);
        gboolean quiet = FALSE;
        native_profile_debug_reply(session, line);
        native_debug_point_reply(session, line);
        if (g_str_has_prefix(line, "*running") || g_str_has_prefix(line, "^running"))
        {
            if (session->assembly)
            {
                session->assembly->instruction_pc_valid = FALSE;
                native_instruction_refresh(session);
                g_ptr_array_set_size(session->assembly->ranges, 0);
                g_hash_table_remove_all(session->assembly->registers);
                gtk_label_set_text(GTK_LABEL(session->assembly->status),
                    insight_tr("Previous instruction snapshot. Pause and read again to resolve names in the new context."));
            }
            session->running = TRUE;
            session->stopped = FALSE;
            native_values_reset(session);
            g_clear_pointer(&session->stop_detail, g_free);
            session->all_stacks_needed = FALSE;
            g_ptr_array_set_size(session->stop_markers, 0);
            g_clear_pointer(&session->requested_frame, g_free);
            session->frame_select_needed = FALSE;
        }
        else if (g_str_has_prefix(line, "*stopped"))
        {
            session->running = FALSE;
            session->stopped = !g_str_has_prefix(line, "*stopped,reason=\"exited");
            if (session->stopped)
            {
                if (session->assembly)
                    session->assembly->follow_needed = TRUE;
                native_values_reset(session);
                g_free(session->stop_detail);
                session->stop_detail = native_catch_stop_detail(session, line);
                if (!session->stop_detail)
                    session->stop_detail = native_stop_detail(line);
                session->watch_next = 0;
                g_clear_pointer(&session->selected_thread, g_free);
                g_clear_pointer(&session->selected_frame, g_free);
                g_clear_pointer(&session->requested_frame, g_free);
                session->frame_select_needed = FALSE;
                session->locals_needed = TRUE;
                session->threads_needed = session->stack_needed = TRUE;
                session->points_needed = TRUE;
                native_attach_stopped(session, line);
                native_debug_observe(session, line);
            }
        }
        if (g_str_has_prefix(line, "=thread-group-exited,") ||
            g_str_has_prefix(line, "*stopped,reason=\"exited"))
            native_target_clear(session);
        if (*line == '^')
        {
            quiet = native_assembly_reply(session, line);
            if (!quiet)
                quiet = native_remote_reply(session, line);
            if (!quiet)
                quiet = native_memory_reply(session, line);
            if (!quiet)
                quiet = native_values_reply(session, line);
            if (!quiet)
                native_lifecycle_reply(session, line);
            if (session->data_refresh_pending)
            {
                session->data_refresh_pending = FALSE;
                if (g_str_has_prefix(line, "^done") && session->stopped)
                    native_data_refresh(session);
            }
            if (!quiet && g_str_has_prefix(line, "^error"))
            {
                gchar *message = native_debug_field(line, "msg");
                if (message)
                    ui_set_statusbar(TRUE, "%s", message);
                g_free(message);
            }
            session->pending = FALSE;
            session->ready = !g_str_has_prefix(line, "^exit");
            session->inspecting = FALSE;
            if (session->console_pending)
            {
                session->console_pending = FALSE;
                if (session->stopped)
                {
                    native_values_reset(session);
                    g_clear_pointer(&session->selected_thread, g_free);
                    g_clear_pointer(&session->selected_frame, g_free);
                    session->threads_needed = session->stack_needed = session->locals_needed = TRUE;
                    session->watch_next = 0;
                }
            }
            if (session->context_pending != NATIVE_CONTEXT_NONE)
                native_context_reply(session, line);
            if (session->locals_pending)
            {
                session->locals_pending = FALSE;
                native_debug_locals(session, line);
                if (!session->expressions->len)
                    native_watch_refresh(session, TRUE);
            }
            if (session->watch_pending)
            {
                NativeWatch *watch = session->watch_pending;
                g_free(watch->value);
                watch->value = native_debug_field(line,
                    g_str_has_prefix(line, "^done") ? "value" : "msg");
                if (!watch->value)
                    watch->value = g_strdup("Unavailable");
                session->watch_pending = NULL;
                if (session->watch_next == session->expressions->len)
                    native_watch_refresh(session, TRUE);
            }
        }
        if ((*line == '~' || *line == '@' || *line == '&') &&
            line[1] == '"' && g_str_has_suffix(line, "\""))
        {
            line[strlen(line) - 1] = '\0';
            gchar *decoded = g_strcompress(line + 2);
            g_strchomp(decoded);
            native_debug_log(session, decoded);
            if (session->inspecting && *line == '~')
            {
                GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(session->inspection));
                GtkTextIter end;
                gtk_text_buffer_get_end_iter(buffer, &end);
                gchar *valid = g_utf8_make_valid(decoded, -1);
                gtk_text_buffer_insert(buffer, &end, valid, -1);
                gtk_text_buffer_insert(buffer, &end, "\n", 1);
                g_free(valid);
            }
            g_free(decoded);
        }
        else if (!quiet && *line && strcmp(line, "(gdb)") && *line != '=' && strcmp(line, "^done"))
            native_debug_log(session, line);
        native_debug_update(session);
        native_debug_resume(dialog, session);
        g_data_input_stream_read_line_async(session->output, G_PRIORITY_DEFAULT,
            session->cancel, native_debug_read, g_object_ref(dialog));
    }
    else if (!session->closed)
    {
        native_target_clear(session);
        insight_queue_clear_full(&session->launch_commands, g_free);
        session->ready = session->kill_pending = FALSE;
        native_debug_log(session, error ? error->message : insight_tr("GDB session ended."));
        native_debug_update(session);
    }
    g_free(line);
    g_clear_error(&error);
    g_object_unref(dialog);
}

/* EN: Send the GDB command associated with button using the dialog in data.
 * RU: Отправляет команду GDB кнопки button через диалог из data. */
static void native_debug_clicked(GtkButton *button, gpointer data)
{
    static const gchar *commands[] = {
        "-exec-run", "-exec-continue", "-exec-step", "-exec-next", "-exec-finish",
        "-exec-interrupt", "-interpreter-exec console \"backtrace\"",
        "-interpreter-exec console \"info locals\""
    };
    guint action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "native-debug-action"));
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (action == NATIVE_STOP || action == NATIVE_RESTART)
    {
        native_lifecycle_request(dialog, action == NATIVE_RESTART);
        return;
    }
    if (action == NATIVE_UNTIL)
    {
        native_run_to_cursor(dialog);
        return;
    }
    if (action == NATIVE_RUN)
    {
        if (native_launch_prepare(dialog))
            native_debug_resume(dialog, session);
        return;
    }
    if (action == NATIVE_STACK || action == NATIVE_LOCALS)
    {
        session->inspecting = TRUE;
        debug_set_text(session->inspection, "");
        gtk_notebook_set_current_page(GTK_NOTEBOOK(session->tabs), 3);
    }
    native_debug_send(dialog, commands[action]);
}

/* EN: Send the text in entry as a GDB command for the dialog in data.
 * RU: Отправляет текст entry как команду GDB для диалога из data. */
static void native_debug_command(GtkEntry *entry, gpointer data)
{
    const gchar *text = gtk_entry_get_text(entry);
    if (!*text)
        return;
    gchar *escaped = g_strescape(text, NULL);
    gchar *command = g_strdup_printf("-interpreter-exec console \"%s\"", escaped);
    NativeDebug *session = g_object_get_data(G_OBJECT(data), "native-debug");
    session->points_needed = TRUE;
    session->console_pending = TRUE;
    native_debug_send(GTK_WIDGET(data), command);
    gtk_entry_set_text(entry, "");
    g_free(command);
    g_free(escaped);
}

/* EN: Insert the selected breakpoint for the dialog in data. button is unused.
 * RU: Добавляет выбранную точку для диалога из data. button не используется. */
static void native_debug_breakpoint(GtkButton *button, gpointer data)
{
    (void)button;
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    const gchar *location = gtk_entry_get_text(GTK_ENTRY(session->location));
    if (!*location)
        return;
    gchar *escaped = g_strescape(location, NULL);
    gchar *command = g_strdup_printf("-break-insert -f -- \"%s\"", escaped);
    native_debug_send(dialog, command);
    g_free(command);
    g_free(escaped);
}

/* EN: Open the source at the last stop in data. button is unused.
 * RU: Открывает исходник последней остановки из data. button не используется. */
static void native_debug_source(GtkButton *button, gpointer data)
{
    (void)button;
    NativeDebug *session = data;
    if (session->stop_file && session->stop_line)
    {
        gchar *path = utils_get_locale_from_utf8(session->stop_file);
        diff_source_open(path, session->stop_line);
        g_free(path);
    }
}

/* EN: Remove the observer from the session in data. button is unused.
 * RU: Удаляет наблюдателя сеанса из data. button не используется. */
static void native_debug_unload(GtkButton *button, gpointer data)
{
    (void)button;
    NativeDebug *session = data;
    g_clear_pointer(&session->observer, insight_lua_debug_free);
    session->log_offset = 0;
    debug_set_text(session->observer_log, "");
    native_watch_refresh(session, FALSE);
    native_debug_update(session);
}

/* EN: Load an observer for the dialog in data. button is unused.
 * RU: Загружает наблюдателя для диалога из data. button не используется. */
static void native_debug_load(GtkButton *button, gpointer data)
{
    (void)button;
    GtkWidget *dialog = g_object_ref(data);
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    GtkWidget *chooser = gtk_file_chooser_dialog_new(insight_tr("Load Lua Observer"),
        GTK_WINDOW(dialog), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    gchar *filename = gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT ?
        gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)) : NULL;
    gtk_widget_destroy(chooser);
    if (filename && !session->closed)
    {
        gchar source[64 * 1024 + 1];
        gsize length = 0;
        GFile *file = g_file_new_for_path(filename);
        GFileInputStream *stream = g_file_read(file, NULL, NULL);
        gboolean loaded = stream && g_input_stream_read_all(G_INPUT_STREAM(stream), source,
            sizeof source, &length, NULL, NULL) && length && length < sizeof source &&
            !memchr(source, 0, length) && g_utf8_validate(source, length, NULL);
        gchar error[1024] = "";
        InsightLuaDebug *observer = loaded ? insight_lua_debug_new_observer() : NULL;
        if (observer && insight_lua_debug_set_observer(observer, source, length, error, sizeof error))
        {
            native_debug_unload(NULL, session);
            session->observer = observer;
            native_debug_log(session, insight_tr("Lua observer loaded."));
        }
        else
        {
            insight_lua_debug_free(observer);
            native_debug_log(session, *error ? error : insight_tr("Cannot load Lua observer."));
        }
        g_clear_object(&stream);
        g_object_unref(file);
        native_debug_update(session);
    }
    g_free(filename);
    g_object_unref(dialog);
}

/* EN: Add or remove a watch for the dialog in data according to button.
 * RU: Добавляет или удаляет наблюдение для диалога из data по кнопке button. */
static void native_watch_edit(GtkButton *button, gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    gchar *expression = g_strdup(gtk_entry_get_text(GTK_ENTRY(session->watch_entry)));
    g_strstrip(expression);
    if (!*expression)
    {
        g_free(expression);
        return;
    }
    guint index = 0;
    while (index < session->expressions->len &&
           strcmp(((NativeWatch *)session->expressions->pdata[index])->expression, expression))
        index++;
    if (GTK_WIDGET(button) == session->watch_remove)
    {
        if (index < session->expressions->len)
            g_ptr_array_remove_index(session->expressions, index);
    }
    else if (index == session->expressions->len && index < 32)
    {
        NativeWatch *watch = g_new0(NativeWatch, 1);
        watch->expression = g_strdup(expression);
        g_ptr_array_add(session->expressions, watch);
    }
    g_free(expression);
    session->watch_next = 0;
    native_watch_refresh(session, FALSE);
    native_debug_resume(dialog, session);
}
