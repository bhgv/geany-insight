/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Execution, pause, restart and observer setup.
 * RU: Выполнение, пауза, перезапуск и настройка наблюдателей. */

/* Capture edited Python modules under the active project's base directory. */
/* Снимает несохранённые исходники Python внутри корневого каталога проекта. */
static GHashTable *debug_capture_python_overlays(const gchar *root, const gchar *entry, GError **error)
{
    GHashTable *overlays = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (!root || !g_path_is_absolute(root))
        return overlays;

    GFile *base = g_file_new_for_path(root);
    gsize total = 0;
    gboolean valid = TRUE;
    for (guint i = 0; i < documents_array->len && valid; i++)
    {
        GeanyDocument *document = g_ptr_array_index(documents_array, i);
        if (!DOC_VALID(document) || !document->changed || !document->file_name ||
            !python_language(document->file_type) || debug_same_file(entry, document->file_name))
            continue;

        gchar *locale_filename = utils_get_locale_from_utf8(document->file_name);
        if (!locale_filename)
            continue;
        GFile *file = g_file_new_for_path(locale_filename);
        gchar *relative = g_file_get_relative_path(base, file);
        gboolean include = relative && g_file_test(locale_filename, G_FILE_TEST_IS_REGULAR);
        g_free(relative);
        g_object_unref(file);
        g_free(locale_filename);
        if (!include)
            continue;

        gint length = sci_get_length(document->editor->sci);
        if (length < 0 || length > 2 * 1024 * 1024 || total + (gsize)length > 16 * 1024 * 1024 ||
            g_hash_table_size(overlays) >= 64)
        {
            valid = FALSE;
            break;
        }
        gchar *text = sci_get_contents(document->editor->sci, length + 1);
        if (memchr(text, 0, length) || !g_utf8_validate(text, length, NULL))
        {
            g_free(text);
            valid = FALSE;
            break;
        }
        g_hash_table_insert(overlays, debug_file_key(document->file_name), text);
        total += (gsize)length;
    }
    g_object_unref(base);
    if (!valid)
    {
        g_hash_table_unref(overlays);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
            "Edited Python modules exceed the debugger snapshot limits or contain invalid text");
        return NULL;
    }
    return overlays;
}

/* Resolve the interpreter and start the Python bridge for a source snapshot. */
/* Находит интерпретатор и запускает процесс отладки для снимка исходника. */
static InsightPythonDebug *debug_new_python_engine(const gchar *filename, const gchar *project_root,
                                                    const gchar *source,
                                                    gsize length, GHashTable *overlays, GError **error)
{
    const gchar *configured = g_getenv("GEANY_INSIGHT_PYTHON");
    gchar *interpreter = g_find_program_in_path(configured && *configured ? configured : "python3");
    if (!interpreter && (!configured || !*configured))
        interpreter = g_find_program_in_path("python");
    if (!interpreter)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
            "Python 3 not found; set GEANY_INSIGHT_PYTHON to its executable");
        return NULL;
    }

    gchar *helper = g_build_filename(app->datadir, "insight", "tools", "python_debug.py", NULL);
    InsightPythonDebug *engine = insight_python_debug_new(interpreter, helper,
        filename && *filename ? filename : "<buffer>", project_root, source, length, overlays, error);
    g_free(helper);
    g_free(interpreter);
    return engine;
}

/* Capture the selected action and schedule its first VM slice. */
/* Сохраняет выбранное действие и планирует первую порцию выполнения. */
static void debug_window_step(ScriptDebugWindow *view, InsightLuaStep mode)
{
    if ((!view->engine && !view->python_engine) || debug_is_running(view))
        return;
    if (!debug_parse_breakpoints(gtk_entry_get_text(GTK_ENTRY(view->breakpoints)),
                               view->pending_breakpoints, &view->pending_breakpoint_count))
    {
        gtk_label_set_text(GTK_LABEL(view->status), "Enter up to 128 positive breakpoint line numbers.");
        return;
    }

    if (!view->python)
    {
        debug_store_file_breakpoints(view, view->display_filename,
                                     gtk_entry_get_text(GTK_ENTRY(view->breakpoints)));
        view->pending_breakpoint_count = 0;
        GHashTableIter files;
        gpointer filename, text;
        g_hash_table_iter_init(&files, view->file_breakpoints);
        while (g_hash_table_iter_next(&files, &filename, &text))
        {
            unsigned long lines[128];
            gsize count = 0;
            if (!debug_parse_breakpoints(text, lines, &count) ||
                count > G_N_ELEMENTS(view->pending_lua_breakpoints) - view->pending_breakpoint_count)
            {
                gtk_label_set_text(GTK_LABEL(view->status), "Lua supports up to 128 breakpoints across all files");
                return;
            }
            for (gsize i = 0; i < count; i++)
            {
                if (debug_has_conditional_point(view, filename, lines[i]))
                    continue;
                InsightLuaBreakpoint point = {filename, lines[i]};
                view->pending_lua_breakpoints[view->pending_breakpoint_count++] = point;
            }
        }
    }
    view->pending_mode = mode;
    view->inspection_frame = 0;
    view->inspecting_frame = FALSE;
    gtk_widget_set_sensitive(view->stack_frames, FALSE);
    view->paused_line = 0;
    view->watch_graph = NULL;
    gtk_widget_trigger_tooltip_query(view->source_view);
    view->watch_selected = -1;
    debug_watch_layout(view);
    gtk_widget_set_sensitive(view->go_to_source, FALSE);
    gtk_widget_set_sensitive(view->step_controls, FALSE);
    gtk_widget_set_sensitive(view->breakpoints, FALSE);
    if (view->file_breakpoint_button)
        gtk_widget_set_sensitive(view->file_breakpoint_button, FALSE);
    gtk_widget_set_sensitive(view->stop, TRUE);
    gtk_widget_set_sensitive(view->pause, TRUE);
    gtk_label_set_text(GTK_LABEL(view->status), view->python ? "Python script running…" : "Lua script running…");
    if (view->python)
        debug_python_step(view);
    else
        view->execution_source = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 1, debug_execute_slice, view, NULL);
}

/* Cancel the GTK source before stopping, restarting, or closing the VM. */
/* Отменяет источник GTK перед остановкой, перезапуском или закрытием VM. */
static void debug_cancel_execution(ScriptDebugWindow *view)
{
    view->python_running = FALSE;
    view->python_request++;
    if (view->python_input_row)
    {
        gtk_widget_set_sensitive(view->python_input_row, FALSE);
        gtk_entry_set_text(GTK_ENTRY(view->python_input_entry), "");
    }
    if (view->execution_source)
    {
        g_source_remove(view->execution_source);
        view->execution_source = 0;
    }
}

/* Prevent scheduled execution after the debugger window is destroyed. */
/* Отменяет запланированное выполнение при уничтожении окна отладчика. */
static void debug_window_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    ScriptDebugWindow *view = data;
    view->closing = TRUE;
    debug_cancel_execution(view);
    g_clear_pointer(&view->python_engine, insight_python_debug_free);
}

/* Request a source stop without discarding VM state. */
/* Запрашивает остановку на строке с сохранением состояния VM. */
static void debug_pause(GtkButton *button, gpointer data)
{
    (void)button;
    ScriptDebugWindow *view = data;
    if (!debug_is_running(view))
        return;
    if (view->python)
    {
        GError *error = NULL;
        if (!insight_python_debug_send(view->python_engine, "pause\n", &error))
        {
            gtk_label_set_text(GTK_LABEL(view->status), error ? error->message : "Cannot pause Python");
            g_clear_error(&error);
            return;
        }
    }
    else
        insight_lua_debug_request_pause(view->engine);
    gtk_widget_set_sensitive(view->pause, FALSE);
    gtk_label_set_text(GTK_LABEL(view->status), "Pausing…");
}

/* Dispatch the step mode associated with a toolbar button. */
/* Выбирает режим шага, связанный с кнопкой панели. */
static void debug_window_action(GtkButton *button, gpointer data)
{
    InsightLuaStep mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "insight-lua-step"));
    debug_window_step(data, mode);
}

/* Quote a filename byte for Lua without letting a path become source code. */
/* Экранирует байты имени файла, чтобы путь не стал кодом Lua. */
static void debug_append_lua_filename(GString *program, const gchar *filename)
{
    g_string_append_c(program, '"');
    for (const guchar *byte = (const guchar *)filename; *byte; byte++)
        g_string_append_printf(program, "\\%03u", (guint)*byte);
    g_string_append_c(program, '"');
}

/* Compile the breakpoint expressions and keep separate hit counts per point. */
/* Компилирует условия точек и ведёт отдельный счётчик каждой точки. */
static void debug_append_conditional_points(GString *program, GHashTable *points)
{
    g_string_append(program, "local points = {}\n");
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, points);
    while (g_hash_table_iter_next(&iter, NULL, &value))
    {
        const DebugConditionalPoint *point = value;
        if (point->disabled)
            continue;
        g_string_append(program, "do\nlocal file = ");
        debug_append_lua_filename(program, point->filename);
        g_string_append_printf(program,
            "\npoints[file] = points[file] or {}\n"
            "local hits = 0\npoints[file][%lu] = function(event)\n"
            "  if (%s\n) ~= true then return false end\n"
            "  hits = hits + 1\n"
            "  if hits < %u then return false end\n",
            point->line, point->expression, point->minimum_hits);
        if (point->log_only)
            g_string_append(program, "  log(event.file .. ':' .. event.line .. ' hit ' .. hits)\n  return false\n");
        else
            g_string_append(program, "  return true\n");
        g_string_append(program, "end\nend\n");
    }
}

/* Combine the optional line condition with a stateful Lua observer. */
/* Объединяет условие остановки на строке со сценарием Lua, сохраняющим состояние. */
static gboolean debug_apply_lua_observer(InsightLuaDebug *engine, const gchar *condition,
                                        const gchar *source, GHashTable *points, gchar *error, gsize capacity)
{
    if ((!condition || !*condition) && (!source || !*source) && !g_hash_table_size(points))
        return insight_lua_debug_set_observer(engine, NULL, 0, error, capacity);

    GString *program = g_string_new("local observer\n");
    if (source && *source)
    {
        g_string_append(program, "observer = (function()\n");
        g_string_append(program, source);
        g_string_append(program, "\nend)()\n"
            "if type(observer) ~= 'function' then error('Observer must return a function') end\n");
    }
    debug_append_conditional_points(program, points);
    g_string_append(program, "return function(event)\n"
        "  local stop = observer and observer(event) == true\n"
        "  if event.kind == 'line' then\n"
        "    local file_points = points[event.file_key]\n"
        "    local point = file_points and file_points[event.line]\n"
        "    if point and point(event) then stop = true end\n"
        "  end\n"
        "  if stop then return true end\n");
    if (condition && *condition)
        g_string_append_printf(program, "  if event.kind == 'line' then return (%s\n) == true end\n", condition);
    g_string_append(program, "end\n");
    gboolean applied = insight_lua_debug_set_observer(engine, program->str, program->len, error, capacity);
    g_string_free(program, TRUE);
    return applied;
}

/* Configure the common observer for a Python session or clear it. */
/* Настраивает общий обработчик для сеанса Python или отключает его. */
static gboolean debug_apply_python_observer(InsightLuaDebug **observer, const gchar *condition,
                                           const gchar *source, GHashTable *points, gchar *error, gsize capacity)
{
    if ((!condition || !*condition) && (!source || !*source) && !g_hash_table_size(points))
    {
        g_clear_pointer(observer, insight_lua_debug_free);
        return TRUE;
    }

    InsightLuaDebug *engine = *observer ? *observer : insight_lua_debug_new_observer();
    if (!engine)
    {
        g_strlcpy(error, "Cannot allocate debugger observer", capacity);
        return FALSE;
    }
    if (!debug_apply_lua_observer(engine, condition, source, points, error, capacity))
    {
        if (!*observer)
            insight_lua_debug_free(engine);
        return FALSE;
    }
    *observer = engine;
    return TRUE;
}

/* Start a fresh VM on the same source snapshot, keeping the breakpoints. */
/* Создаёт новую виртуальную машину для того же снимка и сохраняет точки останова. */
static void debug_restart(GtkButton *button, gpointer data)
{
    (void)button;
    ScriptDebugWindow *view = data;
    gchar error[1024] = {0};
    InsightLuaDebug *lua_replacement = NULL;
    InsightPythonDebug *python_replacement = NULL;
    InsightLuaDebug *observer_replacement = NULL;
    GError *python_error = NULL;
    if (view->python)
    {
        python_replacement = debug_new_python_engine(view->source_filename, view->project_root, view->source_snapshot,
                                                     view->source_length, view->python_overlays, &python_error);
        if (python_replacement && !debug_apply_python_observer(&observer_replacement, view->stop_condition,
                                                               view->observer_source, view->conditional_points, error, sizeof error))
            g_clear_pointer(&python_replacement, insight_python_debug_free);
    }
    else
    {
        gchar *filename = debug_file_key(view->source_filename);
        lua_replacement = insight_lua_debug_new(filename, view->source_snapshot, view->source_length,
                                                error, sizeof error);
        g_free(filename);
        insight_lua_debug_set_module_loader(lua_replacement, debug_lua_resolve_module, view);
        if (lua_replacement && !debug_apply_lua_observer(lua_replacement, view->stop_condition,
                                                       view->observer_source, view->conditional_points, error, sizeof error))
            g_clear_pointer(&lua_replacement, insight_lua_debug_free);
    }
    if (!lua_replacement && !python_replacement)
    {
        const gchar *description = python_error ? python_error->message :
            *error ? error : "Unable to restart debugger";
        gchar *message = g_utf8_make_valid(description, -1);
        gtk_label_set_text(GTK_LABEL(view->status), message);
        g_free(message);
        g_clear_error(&python_error);
        return;
    }

    debug_cancel_execution(view);
    view->watch_graph = NULL;
    view->watch_selected = -1;
    debug_watch_layout(view);
    insight_lua_debug_free(view->engine);
    insight_python_debug_free(view->python_engine);
    g_clear_pointer(&view->python_graph, g_free);
    g_clear_pointer(&view->python_stack, g_free);
    view->engine = lua_replacement;
    view->python_engine = python_replacement;
    insight_lua_debug_free(view->python_observer);
    view->python_observer = observer_replacement;
    if (view->observer_log)
        debug_set_text(view->observer_log, "");
    if (view->python_console)
    {
        debug_set_text(view->python_console, "");
        view->python_console_bytes = 0;
    }
    g_clear_pointer(&view->watch_previous_values, g_hash_table_unref);
    view->paused_line = 0;
    gtk_widget_set_sensitive(view->go_to_source, FALSE);
    gtk_widget_set_sensitive(view->step_controls, TRUE);
    gtk_widget_set_sensitive(view->stop, TRUE);
    debug_window_step(view, INSIGHT_LUA_STEP_INTO);
}

/* End the current VM while keeping the source and breakpoint list. */
/* Завершает текущую VM, сохраняя исходник и список точек останова. */
static void debug_stop(GtkButton *button, gpointer data)
{
    (void)button;
    ScriptDebugWindow *view = data;
    if (!view->engine && !view->python_engine)
        return;

    debug_cancel_execution(view);
    view->watch_graph = NULL;
    view->watch_selected = -1;
    debug_watch_layout(view);
    g_clear_pointer(&view->engine, insight_lua_debug_free);
    g_clear_pointer(&view->python_engine, insight_python_debug_free);
    g_clear_pointer(&view->python_graph, g_free);
    g_clear_pointer(&view->python_stack, g_free);
    view->paused_line = 0;
    gtk_widget_set_sensitive(view->step_controls, FALSE);
    gtk_widget_set_sensitive(view->go_to_source, FALSE);
    gtk_widget_set_sensitive(view->stop, FALSE);
    gtk_widget_set_sensitive(view->pause, FALSE);
    gtk_widget_set_sensitive(view->breakpoints, TRUE);
    if (view->file_breakpoint_button)
        gtk_widget_set_sensitive(view->file_breakpoint_button, TRUE);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->source_view));
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    gtk_text_buffer_remove_tag(buffer, view->current_line_tag, &first, &last);
    debug_set_text(view->stack_view, "");
    gtk_label_set_text(GTK_LABEL(view->status), "Debugging stopped");
}
