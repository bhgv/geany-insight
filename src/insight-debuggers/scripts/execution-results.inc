/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Stack frames, execution results and Lua slices.
 * RU: Кадры стека, результаты выполнения и шаги Lua. */

/* Fill the frame selector from the bounded source-frame headings. */
/* Заполняет список кадров по заголовкам кадров исходников. */
static void debug_update_stack_frames(ScriptDebugWindow *view, const gchar *stack)
{
    view->updating_stack_frames = TRUE;
    GtkComboBoxText *frames = GTK_COMBO_BOX_TEXT(view->stack_frames);
    gtk_combo_box_text_remove_all(frames);
    guint count = 0;
    while (stack && *stack == '#' && count < 16)
    {
        const gchar *end = strchr(stack, '\n');
        const gchar *space = strchr(stack, ' ');
        guint64 level;
        if (!end || !space || space >= end)
            break;
        gchar *id = g_strndup(stack + 1, space - stack - 1);
        if (g_ascii_string_to_unsigned(id, 10, 0, 15, &level, NULL))
        {
            gchar *label = g_utf8_make_valid(stack, end - stack);
            gtk_combo_box_text_append(frames, id, label);
            g_free(label);
            count++;
        }
        g_free(id);
        stack = end + 1;
    }

    gchar id[16];
    g_snprintf(id, sizeof id, "%u", view->inspection_frame);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(frames), id);
    gtk_widget_set_sensitive(view->stack_frames, count > 1);
    view->updating_stack_frames = FALSE;
}

/* EN: Update view for result and line. Publish error to Messages on failure when
 * the Python protocol has not already supplied diagnostic rows.
 * RU: Обновляет view по result и line. При ошибке публикует error в «Сообщениях»,
 * если протокол Python ещё не передал строки диагностики. */
static void debug_show_result(ScriptDebugWindow *view, gint result, unsigned long line, const gchar *error)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->source_view));
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    gtk_text_buffer_remove_tag(buffer, view->current_line_tag, &first, &last);
    view->paused_line = 0;
    InsightLuaDebug *observer = view->python ? view->python_observer : view->engine;

    if (result == 1 && line > 0)
    {
        view->paused_line = line;
        if (view->display_source && line <= (gulong)gtk_text_buffer_get_line_count(buffer))
        {
            gtk_text_buffer_get_iter_at_line(buffer, &first, (gint)line - 1);
            last = first;
            gtk_text_iter_forward_line(&last);
            gtk_text_buffer_apply_tag(buffer, view->current_line_tag, &first, &last);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(view->source_view), &first, 0.15, FALSE, 0, 0);
        }
        gchar lua_stack[8192];
        const gchar *stack = view->python_stack;
        if (!view->python)
        {
            insight_lua_debug_inspect(view->engine, lua_stack, sizeof lua_stack);
            stack = lua_stack;
        }
        debug_set_text(view->stack_view, stack ? stack : "");
        debug_update_stack_frames(view, stack);
        gchar *label;
        if (view->inspection_frame)
            label = g_strdup_printf("Paused, inspecting frame #%u at %s:%lu", view->inspection_frame,
                                    view->display_filename ? view->display_filename : "<buffer>", line);
        else if (view->display_filename)
            label = g_strdup_printf("Paused at %s:%lu", view->display_filename, line);
        else
            label = g_strdup_printf("Paused at line %lu", line);
        if (view->unresolved_breakpoints)
        {
            gchar *summary = g_strdup_printf("%s. Unresolved breakpoints: %u",
                                             label, view->unresolved_breakpoints);
            g_free(label);
            label = summary;
        }
        gtk_label_set_text(GTK_LABEL(view->status), label);
        g_free(label);
    }
    else
    {
        gchar *label = result == 0 ? g_strdup(view->python ? "Python script finished" : "Lua script finished") :
            g_utf8_make_valid(*error ? error : "Debugger stopped", -1);
        if (result < 0 && (!view->python || !view->python_diagnostic_seen))
        {
            gchar *native = utils_get_locale_from_utf8(view->source_filename);
            gchar *directory = native ? g_path_get_dirname(native) : NULL;
            msgwin_debugger_report(label, directory, native);
            g_free(directory);
            g_free(native);
        }
        gtk_label_set_text(GTK_LABEL(view->status), label);
        debug_set_text(view->stack_view, result < 0 ? label : "");
        debug_update_stack_frames(view, NULL);
        g_free(label);
        gtk_widget_set_sensitive(view->step_controls, FALSE);
    }
    gtk_widget_set_sensitive(view->go_to_source, view->paused_line > 0);
    gtk_widget_set_sensitive(view->stop, result == 1);
    gtk_widget_set_sensitive(view->pause, FALSE);
    gtk_widget_set_sensitive(view->step_controls, result == 1);
    gtk_widget_set_sensitive(view->breakpoints, TRUE);
    if (view->file_breakpoint_button)
        gtk_widget_set_sensitive(view->file_breakpoint_button, TRUE);
    view->watch_graph = NULL;
    if (result == 1)
        view->watch_graph = view->python ? view->python_graph : insight_lua_debug_watch_graph(view->engine);
    if (observer)
    {
        insight_lua_debug_calculate_watches(observer, view->watch_graph);
        if (view->observer_log)
            debug_set_text(view->observer_log, insight_lua_debug_observer_log(observer));
        if (!view->observer_graph)
            view->observer_graph = g_new0(InsightLuaWatchGraph, 1);
        if (view->watch_graph)
            *view->observer_graph = *view->watch_graph;
        else
            memset(view->observer_graph, 0, sizeof *view->observer_graph);
        insight_lua_debug_observer_watch(observer, view->observer_graph);
        if (view->observer_graph->node_count)
            view->watch_graph = view->observer_graph;
    }
    if (view->inspecting_frame)
    {
        memset(view->watch_changed, 0, sizeof view->watch_changed);
        view->watch_change_count = 0;
    }
    else
        debug_watch_compare(view);
    view->inspecting_frame = FALSE;
    view->watch_selected = -1;
    debug_watch_layout(view);
}

static gboolean debug_show_script_file(ScriptDebugWindow *view, const gchar *filename);
static void debug_mark_breakpoints(GtkEditable *entry, gpointer data);

/* Execute one VM slice and return control to GTK between slices. */
/* Выполняет одну порцию VM и возвращает управление GTK между порциями. */
static gboolean debug_execute_slice(gpointer data)
{
    ScriptDebugWindow *view = data;
    const gchar *project_filename = app->project ? app->project->file_name : NULL;
    const InsightIndex *index = wb.alive && !g_strcmp0(view->project_filename, project_filename) ?
        wb.index : NULL;
    InsightHost host = {
        .context = view,
        .text = debug_snapshot_text,
        .selection = debug_snapshot_selection,
        .index = index,
        .api = &debug_snapshot_api,
        .read_only = 1
    };
    unsigned long line = 0;
    gchar error[1024];
    gint result = insight_lua_debug_resume(view->engine, view->pending_mode,
        view->pending_lua_breakpoints, view->pending_breakpoint_count,
        &host, &line, error, sizeof error);
    if (result == 2)
        return G_SOURCE_CONTINUE;
    if (result == 1)
        debug_show_script_file(view, insight_lua_debug_filename(view->engine));

    view->execution_source = 0;
    debug_show_result(view, result, line, error);
    return G_SOURCE_REMOVE;
}

static gchar *debug_number_source(const gchar *source, gsize length,
                                  GArray *line_offsets, gboolean python);

/* Canonicalize a breakpoint filename for consistent lookup in the editor. */
/* Нормализует имя файла точки останова для поиска в редакторе. */
static gchar *debug_file_key(const gchar *filename)
{
    if (!filename || *filename == '<')
        return g_strdup(filename ? filename : "<buffer>");
    gchar *path = insight_canonicalize_filename(filename, NULL);
#ifdef G_OS_WIN32
    gchar *folded = g_utf8_strdown(path, -1);
    g_free(path);
    return folded;
#else
    return path;
#endif
}
