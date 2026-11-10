/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: GDB events, breakpoints and user actions.
 * RU: События GDB, точки останова и действия пользователя. */

/* EN: Toggle filename:line using GDB's breakpoint identifier once insertion succeeds.
 * RU: Переключает точку filename:line по идентификатору, который GDB вернул после вставки. */
static void native_debug_toggle(const gchar *filename, guint line, gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (session->pending || session->writing || session->editor_point_pending)
        return;

    gchar *location = g_strdup_printf("%s:%u", filename, line);
    const gchar *number = g_hash_table_lookup(session->editor_points, location);
    session->editor_point_removing = number != NULL;
    session->editor_point_pending = location;
    gchar *escaped = g_strescape(location, NULL);
    gchar *command = number ? g_strdup_printf("-break-delete %s", number)
                            : g_strdup_printf("-break-insert -- \"%s\"", escaped);
    native_debug_send(dialog, command);
    g_free(command);
    g_free(escaped);
}

/* EN: Fill the native breakpoint list with locations confirmed by GDB in session.
 * RU: Заполняет список точек координатами, которые GDB подтвердил для session. */
static void native_debug_refresh_points(NativeDebug *session)
{
    gtk_list_store_clear(session->point_rows);
    GList *keys = g_list_sort(g_hash_table_get_keys(session->editor_points),
                              (GCompareFunc)g_strcmp0);
    for (GList *it = keys; it; it = it->next)
        gtk_list_store_insert_with_values(session->point_rows, NULL, -1,
            0, it->data, 1, g_hash_table_lookup(session->editor_points, it->data), -1);
    g_list_free(keys);
}

/* EN: Apply a GDB MI reply to the pending editor breakpoint in session.
 * RU: Применяет ответ GDB MI к ожидающей точке редактора в session. */
static void native_debug_point_reply(NativeDebug *session, const gchar *record)
{
    if (g_str_has_prefix(record, "=breakpoint-deleted,"))
    {
        gchar *number = native_debug_field(record, "id");
        GHashTableIter iter;
        gpointer value;
        g_hash_table_iter_init(&iter, session->editor_points);
        while (number && g_hash_table_iter_next(&iter, NULL, &value))
            if (!strcmp(number, value))
                g_hash_table_iter_remove(&iter);
        g_free(number);
        native_debug_refresh_points(session);
    }
    if (!session->editor_point_pending || *record != '^')
        return;

    gchar *location = g_steal_pointer(&session->editor_point_pending);
    gboolean success = g_str_has_prefix(record, "^done");
    if (success && session->editor_point_removing)
        g_hash_table_remove(session->editor_points, location);
    else if (success)
    {
        gchar *number = native_debug_field(record, "number");
        success = number && *number && strspn(number, "0123456789") == strlen(number);
        if (success)
            g_hash_table_replace(session->editor_points, g_strdup(location), number);
        else
            g_free(number);
    }

    if (success)
        native_debug_refresh_points(session);
    gchar *detail = success ? NULL : native_debug_field(record, "msg");
    const gchar *message = !success ? "Could not change the source breakpoint."
                          : session->editor_point_removing ? "Source breakpoint removed."
                                                          : "Source breakpoint added.";
    ui_set_statusbar(FALSE, "%s %s%s%s", insight_tr(message), location,
                     detail ? " — " : "", detail ? detail : "");
    g_free(detail);
    g_free(location);
}

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
    if (!insight_lua_debug_observer_active(session->observer) ||
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
        native_debug_point_reply(session, line);
        if (g_str_has_prefix(line, "*running") || g_str_has_prefix(line, "^running"))
        {
            session->running = TRUE;
            session->stopped = FALSE;
        }
        else if (g_str_has_prefix(line, "*stopped"))
        {
            session->running = FALSE;
            session->stopped = !g_str_has_prefix(line, "*stopped,reason=\"exited");
            if (session->stopped)
            {
                session->watch_next = 0;
                session->locals_needed = TRUE;
                session->threads_needed = session->stack_needed = TRUE;
                native_debug_observe(session, line);
            }
        }
        if (*line == '^')
        {
            session->pending = FALSE;
            session->ready = !g_str_has_prefix(line, "^exit");
            session->inspecting = FALSE;
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
        else if (*line && strcmp(line, "(gdb)") && *line != '=' && strcmp(line, "^done"))
            native_debug_log(session, line);
        native_debug_update(session);
        native_debug_resume(dialog, session);
        g_data_input_stream_read_line_async(session->output, G_PRIORITY_DEFAULT,
            session->cancel, native_debug_read, g_object_ref(dialog));
    }
    else if (!session->closed)
    {
        session->ready = session->running = session->stopped = FALSE;
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
    gchar *command = g_strdup_printf("-break-insert -- \"%s\"", escaped);
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
