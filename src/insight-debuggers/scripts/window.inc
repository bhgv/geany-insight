/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Script debugger window and entry point selection.
 * RU: Окно отладчика скриптов и выбор точки входа. */

/* EN: Start the debugger for the active script. item is unused and data selects
 * Python when true, otherwise Lua. Publish startup failures in Messages.
 * RU: Запускает отладчик активного скрипта. item не используется, data выбирает
 * Python при true и Lua иначе. Ошибки запуска публикуются в «Сообщениях». */
static void debug_current_script(GtkMenuItem *item, gpointer data)
{
    (void)item;
    gboolean python = GPOINTER_TO_INT(data);
    GeanyDocument *document = document_get_current();
    if (!DOC_VALID(document) || !document->file_type ||
        document->file_type->lang != (python ? TM_PARSER_PYTHON : TM_PARSER_LUA))
    {
        ui_set_statusbar(TRUE, "Open a %s file to start the debugger", python ? "Python" : "Lua");
        return;
    }

    gint length = sci_get_length(document->editor->sci);
    if (length < 0 || length > 2 * 1024 * 1024)
    {
        ui_set_statusbar(TRUE, "Debugging supports buffers up to 2 MiB");
        return;
    }

    gchar *source = sci_get_contents(document->editor->sci, length + 1);
    if (memchr(source, 0, length) || !g_utf8_validate(source, length, NULL))
    {
        ui_set_statusbar(TRUE, "The debugger requires a UTF-8 text buffer");
        g_free(source);
        return;
    }

    gchar error[1024] = {0};
    GError *python_error = NULL;
    InsightLuaDebug *lua_engine = NULL;
    InsightPythonDebug *python_engine = NULL;
    gchar *project_root = app->project ? project_get_base_path() : NULL;
    GHashTable *python_overlays = NULL;
    if (python)
    {
        python_overlays = debug_capture_python_overlays(project_root, document->file_name, &python_error);
        if (python_overlays)
            python_engine = debug_new_python_engine(document->file_name, project_root, source, length,
                                                    python_overlays, &python_error);
    }
    else
    {
        gchar *filename = debug_file_key(document->file_name);
        lua_engine = insight_lua_debug_new(filename, source, length, error, sizeof error);
        g_free(filename);
    }
    if (!lua_engine && !python_engine)
    {
        const gchar *description = python_error ? python_error->message :
            *error ? error : "Unable to start debugger";
        gchar *message = g_utf8_make_valid(description, -1);
        gchar *native = utils_get_locale_from_utf8(document->file_name);
        gchar *directory = native ? g_path_get_dirname(native) : NULL;
        msgwin_debugger_report(message, directory, native);
        g_free(directory);
        g_free(native);
        g_free(message);
        g_clear_error(&python_error);
        g_clear_pointer(&python_overlays, g_hash_table_unref);
        g_free(project_root);
        g_free(source);
        return;
    }

    ScriptDebugWindow *view = g_new0(ScriptDebugWindow, 1);
    view->python = python;
    view->engine = lua_engine;
    view->python_engine = python_engine;
    view->python_overlays = python_overlays;
    view->project_root = project_root;
    view->file_breakpoints = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    view->conditional_points = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, debug_conditional_point_free);
    view->disabled_plain_points = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    view->lua_modules = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, debug_lua_module_free);
    insight_lua_debug_set_module_loader(lua_engine, debug_lua_resolve_module, view);
    view->watch_paths = g_ptr_array_new_with_free_func(g_free);
    view->watch_selected = -1;
    view->document_id = document->id;
    view->source_snapshot = source;
    view->source_length = (gsize)length;
    view->source_code_mask = insight_source_code_mask(source, view->source_length,
        python ? INSIGHT_CODE_PYTHON : INSIGHT_CODE_LUA, NULL);
    view->source_line_offsets = g_array_new(FALSE, FALSE, sizeof(gsize));
    view->source_filename = g_strdup(document->file_name);
    view->display_filename = g_strdup(view->source_filename ? view->source_filename : "<buffer>");
    view->display_source = g_strdup(source);
    view->display_length = (gsize)length;
    view->project_filename = g_strdup(app->project ? app->project->file_name : NULL);
    debug_load_breakpoints(view);
    debug_load_conditional_points(view);
    if (g_hash_table_size(view->conditional_points))
    {
        gchar error[1024] = "";
        gboolean applied = view->python ?
            debug_apply_python_observer(&view->python_observer, NULL, NULL, view->conditional_points,
                                        error, sizeof error) :
            debug_apply_lua_observer(view->engine, NULL, NULL, view->conditional_points, error, sizeof error);
        if (!applied)
        {
            g_hash_table_remove_all(view->conditional_points);
            gchar *message = g_utf8_make_valid(*error ? error : "Cannot restore conditional breakpoints", -1);
            ui_set_statusbar(TRUE, "%s", message);
            g_free(message);
        }
    }
    ScintillaObject *sci = document->editor->sci;
    view->selection_snapshot = sci_get_selection_contents(sci);
    view->position = (unsigned long)sci_get_current_position(sci);
    view->selection_start = (unsigned long)sci_get_selection_start(sci);
    view->selection_end = (unsigned long)sci_get_selection_end(sci);
    view->line_starts = g_array_new(FALSE, FALSE, sizeof(gsize));
    gint line_count = sci_get_line_count(sci);
    for (gint line = 0; line < line_count; line++)
    {
        gsize offset = (gsize)sci_get_position_from_line(sci, line);
        g_array_append_val(view->line_starts, offset);
    }
    view->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(view->window), python ? "Python Source Debugger" : "Lua Source Debugger");
    gtk_window_set_default_size(GTK_WINDOW(view->window), 940, 520);
    gtk_window_set_transient_for(GTK_WINDOW(view->window), GTK_WINDOW(main_widgets.window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(view->window), TRUE);
    g_object_set_data_full(G_OBJECT(view->window), "insight-lua-debugger", view, debug_window_free);
    g_signal_connect(view->window, "destroy", G_CALLBACK(debug_window_destroy), view);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    gtk_container_add(GTK_CONTAINER(view->window), box);
    insight_panel_help_attach(box, python ? "python-debugger.md" : "lua-debugger.md");
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);

    view->step_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(toolbar), view->step_controls, FALSE, FALSE, 0);
    GtkWidget *debug_commands[INSIGHT_DEBUG_COMMAND_COUNT] = {NULL};
    const InsightDebugCommand actions[] = {INSIGHT_DEBUG_INTO, INSIGHT_DEBUG_OVER,
                                           INSIGHT_DEBUG_OUT, INSIGHT_DEBUG_CONTINUE};
    const gchar *labels[] = {"Step Into", "Step Over", "Step Out", "Continue"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(labels[i]);
        g_object_set_data(G_OBJECT(button), "insight-lua-step", GUINT_TO_POINTER(i));
        g_signal_connect(button, "clicked", G_CALLBACK(debug_window_action), view);
        gtk_box_pack_start(GTK_BOX(view->step_controls), button, FALSE, FALSE, 0);
        debug_commands[actions[i]] = button;
    }

    GtkWidget *restart = gtk_button_new_with_label("Restart");
    g_signal_connect(restart, "clicked", G_CALLBACK(debug_restart), view);
    gtk_box_pack_start(GTK_BOX(toolbar), restart, FALSE, FALSE, 0);
    view->stop = gtk_button_new_with_label("Stop");
    g_signal_connect(view->stop, "clicked", G_CALLBACK(debug_stop), view);
    gtk_box_pack_start(GTK_BOX(toolbar), view->stop, FALSE, FALSE, 0);
    view->pause = gtk_button_new_with_label("Pause");
    g_signal_connect(view->pause, "clicked", G_CALLBACK(debug_pause), view);
    gtk_box_pack_start(GTK_BOX(toolbar), view->pause, FALSE, FALSE, 0);
    view->go_to_source = gtk_button_new_with_label("Go to Source");
    g_signal_connect(view->go_to_source, "clicked", G_CALLBACK(debug_go_to_source), view);
    gtk_box_pack_start(GTK_BOX(toolbar), view->go_to_source, FALSE, FALSE, 0);

    view->breakpoints = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(view->breakpoints), 2048);
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->breakpoints), "Breakpoint lines (e.g. 3, 12, 27)");
    gchar *breakpoint_key = debug_file_key(view->display_filename);
    const gchar *saved_breakpoints = g_hash_table_lookup(view->file_breakpoints, breakpoint_key);
    if (saved_breakpoints)
        gtk_entry_set_text(GTK_ENTRY(view->breakpoints), saved_breakpoints);
    g_free(breakpoint_key);
    GtkWidget *breakpoint_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), breakpoint_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(breakpoint_row), view->breakpoints, TRUE, TRUE, 0);
    view->file_breakpoint_button = gtk_button_new_with_label("Breakpoints in File…");
    g_signal_connect(view->file_breakpoint_button, "clicked",
                     G_CALLBACK(debug_breakpoints_in_file), view);
    gtk_box_pack_start(GTK_BOX(breakpoint_row), view->file_breakpoint_button, FALSE, FALSE, 0);
    GtkWidget *conditional_button = gtk_button_new_with_label("All Breakpoints…");
    g_signal_connect(conditional_button, "clicked", G_CALLBACK(debug_list_points), view);
    gtk_box_pack_start(GTK_BOX(breakpoint_row), conditional_button, FALSE, FALSE, 0);
    GtkWidget *pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), pane, TRUE, TRUE, 0);
    view->source_view = debug_create_view(pane, TRUE);
    GtkWidget *inspection = gtk_notebook_new();
    gtk_paned_pack2(GTK_PANED(pane), inspection, TRUE, FALSE);
    GtkWidget *stack_pane = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    view->stack_view = debug_create_view(stack_pane, TRUE);
    GtkWidget *stack_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    view->stack_frames = gtk_combo_box_text_new();
    gtk_widget_set_tooltip_text(view->stack_frames, "Choose a stack frame to inspect its source and values");
    gtk_widget_set_sensitive(view->stack_frames, FALSE);
    gtk_box_pack_start(GTK_BOX(stack_page), view->stack_frames, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stack_page), stack_pane, TRUE, TRUE, 0);
    g_signal_connect(view->stack_frames, "changed", G_CALLBACK(debug_stack_frame_changed), view);
    gtk_notebook_append_page(GTK_NOTEBOOK(inspection), stack_page, gtk_label_new("Stack and locals"));
    gtk_notebook_append_page(GTK_NOTEBOOK(inspection), debug_create_watch_tab(view), gtk_label_new("Watch graph"));
    {
        GtkWidget *log_pane = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
        view->observer_log = debug_create_view(log_pane, TRUE);
        gtk_notebook_append_page(GTK_NOTEBOOK(inspection), log_pane, gtk_label_new("Observer log"));
    }
    if (python)
    {
        GtkWidget *console_pane = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
        view->python_console = debug_create_view(console_pane, TRUE);
        GtkWidget *console_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_box_pack_start(GTK_BOX(console_page), console_pane, TRUE, TRUE, 0);
        view->python_input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        view->python_input_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(view->python_input_entry), "Python input (Enter to send)");
        gtk_entry_set_max_length(GTK_ENTRY(view->python_input_entry), 1024);
        GtkWidget *eof = gtk_button_new_with_label("Send EOF");
        gtk_box_pack_start(GTK_BOX(view->python_input_row), view->python_input_entry, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(view->python_input_row), eof, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(console_page), view->python_input_row, FALSE, FALSE, 0);
        g_signal_connect(view->python_input_entry, "activate", G_CALLBACK(debug_python_input_enter), view);
        g_signal_connect(eof, "clicked", G_CALLBACK(debug_python_input_eof), view);
        gtk_widget_set_sensitive(view->python_input_row, FALSE);
        gtk_notebook_append_page(GTK_NOTEBOOK(inspection), console_page, gtk_label_new("Console"));
    }
    gtk_paned_set_position(GTK_PANED(pane), 535);
    gchar *numbered = debug_number_source(view->source_snapshot, view->source_length,
                                          view->source_line_offsets, python);
    debug_set_text(view->source_view, numbered);
    g_free(numbered);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->source_view));
    view->current_line_tag = gtk_text_buffer_create_tag(buffer, "insight-debug-line",
        "paragraph-background", "#416184", "foreground", "#ffffff", NULL);
    view->breakpoint_tag = gtk_text_buffer_create_tag(buffer, "insight-debug-breakpoint",
        "background", "#962e37", "foreground", "#ffffff", "weight", PANGO_WEIGHT_BOLD, NULL);
    view->conditional_point_tag = gtk_text_buffer_create_tag(buffer, "insight-debug-conditional",
        "background", "#805400", "foreground", "#ffffff", "weight", PANGO_WEIGHT_BOLD, NULL);
    g_signal_connect(view->breakpoints, "changed", G_CALLBACK(debug_breakpoint_entry_changed), view);
    debug_mark_breakpoints(GTK_EDITABLE(view->breakpoints), view);
    g_signal_connect(view->source_view, "button-press-event", G_CALLBACK(debug_click_line_number), view);
    gtk_widget_set_has_tooltip(view->source_view, TRUE);
    g_signal_connect(view->source_view, "query-tooltip", G_CALLBACK(debug_source_tooltip), view);
    g_signal_connect(view->window, "key-press-event", G_CALLBACK(debug_handle_key), view);
    view->status = gtk_label_new(NULL);
    gtk_label_set_selectable(GTK_LABEL(view->status), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(view->status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(box), view->status, FALSE, FALSE, 0);
    debug_commands[INSIGHT_DEBUG_PAUSE] = view->pause;
    debug_commands[INSIGHT_DEBUG_STOP] = view->stop;
    insight_debug_register(view->window, view->breakpoints, debug_script_toggle, view, debug_commands);
    gtk_widget_show_all(view->window);
    if (view->unresolved_breakpoints)
        ui_set_statusbar(TRUE, "%u saved breakpoints could not be located and are inactive.",
                         view->unresolved_breakpoints);
    debug_window_step(view, INSIGHT_LUA_STEP_INTO);
}

/* Open a selected entry file, then start the debugger from its editor tab. */
/* Открывает выбранную точку входа и запускает отладчик из её вкладки. */
static void debug_select_script_entry(GtkMenuItem *item, gpointer data)
{
    (void)item;
    gboolean python = GPOINTER_TO_INT(data);
    gchar *root = app->project ? project_get_base_path() : NULL;
    gchar *filename = debug_choose_script_file(GTK_WINDOW(main_widgets.window),
        python ? "Python Debug Entry" : "Lua Debug Entry", root, python);
    g_free(root);
    if (!filename)
        return;
    gchar *utf8 = utils_get_utf8_from_locale(filename);
    GeanyDocument *document = utf8 ? document_find_by_filename(utf8) : NULL;
    if (!DOC_VALID(document))
        document = document_open_file(filename, FALSE, NULL, NULL);
    if (DOC_VALID(document))
    {
        document_show_tab(document);
        document_grab_focus(document);
        debug_current_script(NULL, GINT_TO_POINTER(python));
    }
    g_free(utf8);
    g_free(filename);
}
