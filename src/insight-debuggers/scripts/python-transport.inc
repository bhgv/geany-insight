/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Python debugger transport, input and replies.
 * RU: Обмен с отладчиком Python, ввод и ответы. */

/* Append a normalized filename and line to the Python breakpoint command. */
/* Добавляет нормализованное имя файла и строку в команду для Python. */
static void debug_append_file_breakpoint(GString *command, const gchar *filename, unsigned long line)
{
    g_string_append_c(command, ' ');
    for (const guchar *byte = (const guchar *)filename; *byte; byte++)
        g_string_append_printf(command, "%02x", (guint)*byte);
    g_string_append_printf(command, ":%lu", line);
}

typedef struct {
    GWeakRef window;
    guint64 request;
} PythonDebugReply;

/* Report whether either backend has an action in progress. */
/* Проверяет, выполняется ли действие в одном из движков отладки. */
static gboolean debug_is_running(const ScriptDebugWindow *view)
{
    return view->execution_source != 0 || view->python_running;
}

/* Append bounded Python output without replacing the stack or watch snapshot. */
/* Добавляет ограниченный вывод Python, сохраняя стек и граф наблюдения. */
static void debug_python_append_output(ScriptDebugWindow *view, const gchar *text)
{
    const gsize limit = 260 * 1024;
    if (!view->python_console || view->python_console_bytes >= limit || !text)
        return;

    gsize length = MIN(strlen(text), limit - view->python_console_bytes);
    gchar *valid = g_utf8_make_valid(text, (gssize)length);
    GtkTextView *console = GTK_TEXT_VIEW(view->python_console);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(console);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, valid, -1);
    gtk_text_view_scroll_to_iter(console, &end, 0, FALSE, 0, 0);
    view->python_console_bytes += length;
    g_free(valid);
}

/* Send a line or end-of-file to the waiting Python program. */
/* Передаёт строку или конец файла ожидающей программе Python. */
static void debug_python_submit_input(ScriptDebugWindow *view, gboolean eof)
{
    if (!view->python_engine || !gtk_widget_get_sensitive(view->python_input_row))
        return;

    GString *command = g_string_new(eof ? "eof" : "input ");
    if (!eof)
    {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(view->python_input_entry));
        for (const guchar *byte = (const guchar *)text; *byte; byte++)
            g_string_append_printf(command, "%02x", (guint)*byte);
    }
    g_string_append_c(command, '\n');

    GError *error = NULL;
    if (insight_python_debug_send(view->python_engine, command->str, &error))
    {
        gtk_entry_set_text(GTK_ENTRY(view->python_input_entry), "");
        gtk_widget_set_sensitive(view->python_input_row, FALSE);
        if (view->python_running)
            gtk_label_set_text(GTK_LABEL(view->status), "Python is running");
    }
    else
        gtk_label_set_text(GTK_LABEL(view->status), error ? error->message : "Cannot send Python input");
    g_clear_error(&error);
    g_string_free(command, TRUE);
}

/* Submit the console entry when the user presses Enter. */
/* Отправляет строку консоли по клавише Enter. */
static void debug_python_input_enter(GtkEntry *entry, gpointer data)
{
    (void)entry;
    debug_python_submit_input(data, FALSE);
}

/* Close Python text input for the current run. */
/* Завершает текстовый ввод Python в текущем запуске. */
static void debug_python_input_eof(GtkButton *button, gpointer data)
{
    (void)button;
    debug_python_submit_input(data, TRUE);
}

/* Decode one protocol field without interpreting it as source code. */
/* Декодирует поле протокола без выполнения его как исходного кода. */
static gchar *debug_decode_observer_field(const gchar *hex, gsize *length)
{
    gsize size = strlen(hex);
    if (size % 2)
        return NULL;
    gchar *text = g_malloc(size / 2 + 1);
    for (gsize i = 0; i < size; i += 2)
    {
        gint high = g_ascii_xdigit_value(hex[i]);
        gint low = g_ascii_xdigit_value(hex[i + 1]);
        if (high < 0 || low < 0)
        {
            g_free(text);
            return NULL;
        }
        text[i / 2] = (gchar)((high << 4) | low);
    }
    *length = size / 2;
    text[*length] = 0;
    return text;
}

/* Deliver a Python event to Lua and acknowledge it before Python continues. */
/* Передаёт событие Python в Lua и подтверждает его перед продолжением Python. */
static gboolean debug_python_observer_event(ScriptDebugWindow *view, const gchar *packet,
                                            gsize length, GError **error)
{
    gint decision = -1;
    if (length <= 256 * 1024 && !memchr(packet, 0, length))
    {
        gchar **fields = g_strsplit(packet, "\t", -1);
        guint count = g_strv_length(fields);
        guint64 line;
        if (count >= 5 && count <= 5 + 192 * 3 && (count - 5) % 3 == 0 &&
            (!strcmp(fields[1], "call") || !strcmp(fields[1], "line") || !strcmp(fields[1], "return")) &&
            g_ascii_string_to_unsigned(fields[3], 10, 0, G_MAXINT, &line, NULL))
        {
            gsize file_length = 0, name_length = 0;
            gchar *file = debug_decode_observer_field(fields[2], &file_length);
            gchar *name = debug_decode_observer_field(fields[4], &name_length);
            if (file && name && !memchr(file, 0, file_length) && !memchr(name, 0, name_length))
            {
                InsightObserverValue values[192];
                guint used = 0;
                for (guint i = 5; i < count; i += 3)
                {
                    const gchar *tag = fields[i];
                    if (strlen(tag) != 2 || tag[0] < '0' || tag[0] > '2' || !strchr("bifs", tag[1]))
                        continue;
                    gsize key_length = 0, value_length = 0;
                    gchar *key = debug_decode_observer_field(fields[i + 1], &key_length);
                    gchar *value = debug_decode_observer_field(fields[i + 2], &value_length);
                    if (!key || !value || memchr(key, 0, key_length))
                    {
                        g_free(key);
                        g_free(value);
                        continue;
                    }
                    values[used++] = (InsightObserverValue){
                        .scope = (guint)(tag[0] - '0'), .type = tag[1], .name = key, .text = value,
                        .length = value_length,
                        .integer = tag[1] == 'i' || tag[1] == 'b' ? g_ascii_strtoll(value, NULL, 10) : 0,
                        .number = tag[1] == 'f' ? g_ascii_strtod(value, NULL) : 0
                    };
                }
                gchar *file_key = debug_file_key(file);
                InsightObserverEvent event = {
                    .kind = fields[1], .file = file, .name = name, .line = (unsigned long)line,
                    .values = values, .count = used, .file_key = file_key
                };
                decision = insight_lua_debug_observe(view->python_observer, &event);
                g_free(file_key);
                for (guint i = 0; i < used; i++)
                {
                    g_free((gchar *)values[i].name);
                    g_free((gchar *)values[i].text);
                }
            }
            g_free(file);
            g_free(name);
        }
        g_strfreev(fields);
    }
    return insight_python_debug_send(view->python_engine,
        decision < 0 ? "observe error\n" : decision ? "observe stop\n" : "observe continue\n", error);
}

/* EN: Decode a Python diagnostic packet of length bytes and append it for view.
 * Jump to the first available location in the current reply.
 * RU: Декодирует пакет Python packet длиной length и добавляет его в сообщения
 * view. Переходит к первому доступному месту текущего ответа. */
static void debug_python_diagnostic(ScriptDebugWindow *view, const gchar *packet, gsize length)
{
    if (length > 16384 || memchr(packet, 0, length))
        return;
    gchar **fields = g_strsplit(packet, "\t", 8);
    if (g_strv_length(fields) == 7)
    {
        gint64 line, column, end_line, end_column;
        if (g_ascii_string_to_signed(fields[2], 10, 1, G_MAXINT, &line, NULL) &&
            g_ascii_string_to_signed(fields[3], 10, -1, G_MAXINT, &column, NULL) &&
            g_ascii_string_to_signed(fields[4], 10, 1, G_MAXINT, &end_line, NULL) &&
            g_ascii_string_to_signed(fields[5], 10, -1, G_MAXINT, &end_column, NULL))
        {
            gsize filename_length, message_length;
            gchar *filename = debug_decode_observer_field(fields[1], &filename_length);
            gchar *message = debug_decode_observer_field(fields[6], &message_length);
            if (filename && message && filename_length &&
                !memchr(filename, 0, filename_length) && !memchr(message, 0, message_length))
            {
                gchar *native = utils_get_locale_from_utf8(filename);
                gchar *caption = g_strdup_printf("%s:%" G_GINT64_FORMAT ": %s", filename, line, message);
                gboolean jump = !view->python_diagnostic_jumped && native &&
                                g_file_test(native, G_FILE_TEST_IS_REGULAR);
                msgwin_debugger_add(caption, native, line, column, end_line, end_column, jump);
                view->python_diagnostic_seen = TRUE;
                view->python_diagnostic_jumped |= jump;
                g_free(caption);
                g_free(native);
            }
            g_free(filename);
            g_free(message);
        }
    }
    g_strfreev(fields);
}

/* EN: Read the Python packet from object/result for the weak window in data.
 * Route stops, output, and diagnostic packets without retaining a closed window.
 * RU: Читает пакет Python из object/result для слабой ссылки на окно в data.
 * Передаёт остановки, вывод и ошибки без удержания закрытого окна. */
static void debug_python_received(GObject *object, GAsyncResult *result, gpointer data)
{
    PythonDebugReply *reply = data;
    GError *error = NULL;
    gsize length = 0;
    gchar *packet = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(object),
                                                        result, &length, &error);
    GtkWidget *window = g_weak_ref_get(&reply->window);
    ScriptDebugWindow *view = window ? g_object_get_data(G_OBJECT(window), "insight-lua-debugger") : NULL;
    gboolean keep_reading = FALSE;
    if (view && !view->closing && reply->request == view->python_request)
    {
        unsigned long line = 0;
        gchar *filename = NULL;
        gchar *stack = NULL;
        gchar *message = NULL;
        InsightLuaWatchGraph *graph = NULL;
        gboolean observer_event = packet && length >= 2 && packet[0] == 'V' && packet[1] == '\t';
        gboolean diagnostic = packet && length >= 2 && packet[0] == 'X' && packet[1] == '\t';
        gint status = diagnostic ? 5 : observer_event ? 4 :
            insight_python_debug_parse(packet, length, &line, &filename, &stack, &graph, &message);
        if (observer_event && !error && !debug_python_observer_event(view, packet, length, &error))
            status = -1;
        if (status >= 2 && !error)
        {
            if (diagnostic)
                debug_python_diagnostic(view, packet, length);
            else if (status == 2)
                debug_python_append_output(view, message);
            else if (status == 3)
            {
                GtkWidget *page = gtk_widget_get_parent(view->python_input_row);
                GtkNotebook *notebook = GTK_NOTEBOOK(gtk_widget_get_parent(page));
                gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, page));
                gtk_widget_set_sensitive(view->python_input_row, TRUE);
                gtk_widget_grab_focus(view->python_input_entry);
                gtk_label_set_text(GTK_LABEL(view->status), "Python is waiting for input");
            }
            keep_reading = TRUE;
            g_data_input_stream_read_line_async(G_DATA_INPUT_STREAM(object), G_PRIORITY_DEFAULT,
                                                 NULL, debug_python_received, reply);
        }
        else
        {
            view->python_running = FALSE;
            if (status != 1 || error)
                gtk_widget_set_sensitive(view->python_input_row, FALSE);
            g_free(view->python_stack);
            g_free(view->python_graph);
            view->python_stack = g_steal_pointer(&stack);
            view->python_graph = g_steal_pointer(&graph);
            if (status == 1)
                debug_show_script_file(view, filename);
            debug_show_result(view, status, line, error ? error->message : message ? message : "");
        }
        g_free(filename);
        g_free(stack);
        g_free(graph);
        g_free(message);
    }
    if (window)
        g_object_unref(window);
    g_free(packet);
    g_clear_error(&error);
    if (!keep_reading)
    {
        g_weak_ref_clear(&reply->window);
        g_free(reply);
    }
}

/* EN: Read the next Python reply for view and reset its diagnostic batch.
 * RU: Ждёт следующий ответ Python для view и сбрасывает состояние диагностики. */
static void debug_python_read_reply(ScriptDebugWindow *view)
{
    PythonDebugReply *reply = g_new0(PythonDebugReply, 1);
    g_weak_ref_init(&reply->window, view->window);
    reply->request = ++view->python_request;
    view->python_diagnostic_seen = FALSE;
    view->python_diagnostic_jumped = FALSE;
    view->python_running = TRUE;
    g_data_input_stream_read_line_async(insight_python_debug_output(view->python_engine),
        G_PRIORITY_DEFAULT, NULL, debug_python_received, reply);
}

/* Change the inspected frame while leaving the execution point paused. */
/* Меняет просматриваемый кадр, оставляя выполнение на паузе. */
static void debug_stack_frame_changed(GtkComboBox *frames, gpointer data)
{
    ScriptDebugWindow *view = data;
    if (view->updating_stack_frames || !view->paused_line || debug_is_running(view))
        return;
    const gchar *id = gtk_combo_box_get_active_id(frames);
    guint64 level;
    if (!id || !g_ascii_string_to_unsigned(id, 10, 0, 15, &level, NULL) ||
        level == view->inspection_frame)
        return;

    if (view->python)
    {
        gchar command[24];
        g_snprintf(command, sizeof command, "frame %u\n", (guint)level);
        GError *error = NULL;
        if (!insight_python_debug_send(view->python_engine, command, &error))
        {
            gtk_label_set_text(GTK_LABEL(view->status), error ? error->message : "Cannot inspect Python frame");
            g_clear_error(&error);
            return;
        }
        view->inspection_frame = (guint)level;
        view->inspecting_frame = TRUE;
        gtk_widget_set_sensitive(view->stack_frames, FALSE);
        gtk_widget_set_sensitive(view->step_controls, FALSE);
        debug_python_read_reply(view);
    }
    else
    {
        const char *filename;
        unsigned long line;
        if (!insight_lua_debug_select_frame(view->engine, (guint)level, &filename, &line))
            return;
        view->inspection_frame = (guint)level;
        view->inspecting_frame = TRUE;
        debug_show_script_file(view, filename);
        debug_show_result(view, 1, line, "");
    }
}

/* Send one Python action and wait asynchronously for its next stop. */
/* Отправляет действие Python и асинхронно ждёт следующую остановку. */
static void debug_python_step(ScriptDebugWindow *view)
{
    const gchar *actions[] = {"into", "over", "out", "continue"};
    GString *command = g_string_new(actions[view->pending_mode]);
    debug_store_file_breakpoints(view, view->display_filename,
                                 gtk_entry_get_text(GTK_ENTRY(view->breakpoints)));
    GHashTableIter files;
    gpointer filename, text;
    guint total = 0;
    g_hash_table_iter_init(&files, view->file_breakpoints);
    while (g_hash_table_iter_next(&files, &filename, &text))
    {
        unsigned long lines[128];
        gsize count = 0;
        if (!debug_parse_breakpoints(text, lines, &count) || count > 128 - total)
        {
            g_string_free(command, TRUE);
            debug_show_result(view, -1, 0, "Python supports up to 128 breakpoints across all files");
            gtk_widget_set_sensitive(view->step_controls, TRUE);
            return;
        }
        for (gsize i = 0; i < count; i++)
            if (!debug_has_conditional_point(view, filename, lines[i]))
                debug_append_file_breakpoint(command, filename, lines[i]);
        total += (guint)count;
    }
    if (insight_lua_debug_observer_active(view->python_observer))
    {
        g_string_append(command, " observe");
        GHashTableIter points;
        gpointer value;
        g_hash_table_iter_init(&points, view->conditional_points);
        while (g_hash_table_iter_next(&points, NULL, &value))
        {
            const DebugConditionalPoint *point = value;
            g_string_append(command, " trace:");
            for (const guchar *byte = (const guchar *)point->filename; *byte; byte++)
                g_string_append_printf(command, "%02x", (guint)*byte);
        }
    }
    if (command->len > 64 * 1024)
    {
        g_string_free(command, TRUE);
        debug_show_result(view, -1, 0, "Python breakpoint paths exceed the command limit");
        gtk_widget_set_sensitive(view->step_controls, TRUE);
        return;
    }
    g_string_append_c(command, '\n');

    GError *error = NULL;
    gboolean sent = insight_python_debug_send(view->python_engine, command->str, &error);
    g_string_free(command, TRUE);
    if (!sent)
    {
        debug_show_result(view, -1, 0, error ? error->message : "Cannot send Python debugger command");
        g_clear_error(&error);
        return;
    }

    debug_python_read_reply(view);
}
