/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: GDB debugger window and executable selection.
 * RU: Окно отладчика GDB и выбор исполняемого файла. */

/* EN: Launch gdb with target and display the native debugger window.
 * RU: Запускает gdb для target и открывает окно отладчика. */
static void native_debug_show(const gchar *gdb, const gchar *target)
{
    gchar *directory = g_path_get_dirname(target);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, directory);
    GError *error = NULL;
    GSubprocess *process = g_subprocess_launcher_spawn(launcher, &error, gdb, "--nx", "--quiet",
        "--interpreter=mi2", "--args", target, NULL);
    g_object_unref(launcher);
    g_free(directory);
    if (!process)
    {
        ui_set_statusbar(TRUE, insight_tr("Cannot open GDB: %s"), error->message);
        g_clear_error(&error);
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons(insight_tr("C/C++ Debugger"),
        GTK_WINDOW(main_widgets.window), GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 940, 520);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    insight_panel_help_attach(box, "native-debugger.md");
    NativeDebug *session = g_new0(NativeDebug, 1);
    session->editor_points = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    session->process = process;
    session->output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(process));
    session->cancel = g_cancellable_new();
    session->status = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(box), session->status, FALSE, FALSE, 4);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 4);
    const gchar *labels[] = {"Run", "Continue", "Step Into", "Step Over", "Step Out",
                             "Pause", "Stack", "Locals"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        session->buttons[i] = button;
        g_object_set_data(G_OBJECT(button), "native-debug-action", GUINT_TO_POINTER(i));
        g_signal_connect(button, "clicked", G_CALLBACK(native_debug_clicked), dialog);
        gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    }
    session->source = gtk_button_new_with_label(insight_tr("Open Source"));
    g_signal_connect(session->source, "clicked", G_CALLBACK(native_debug_source), session);
    gtk_box_pack_start(GTK_BOX(toolbar), session->source, FALSE, FALSE, 0);
    GtkWidget *scripts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    session->location = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(session->location), 512);
    gtk_entry_set_placeholder_text(GTK_ENTRY(session->location), insight_tr("Function or file:line"));
    session->breakpoint = gtk_button_new_with_label(insight_tr("Add Breakpoint"));
    g_signal_connect(session->breakpoint, "clicked", G_CALLBACK(native_debug_breakpoint), dialog);
    gtk_box_pack_start(GTK_BOX(scripts), session->location, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(scripts), session->breakpoint, FALSE, FALSE, 0);
    session->load_lua = gtk_button_new_with_label(insight_tr("Load Lua Observer…"));
    session->unload_lua = gtk_button_new_with_label(insight_tr("Disable Lua Observer"));
    g_signal_connect(session->load_lua, "clicked", G_CALLBACK(native_debug_load), dialog);
    g_signal_connect(session->unload_lua, "clicked", G_CALLBACK(native_debug_unload), session);
    gtk_box_pack_start(GTK_BOX(scripts), session->load_lua, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(scripts), session->unload_lua, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), scripts, FALSE, FALSE, 4);
    GtkWidget *tabs = gtk_notebook_new();
    session->tabs = tabs;
    session->expressions = g_ptr_array_new_with_free_func(native_watch_free);
    session->locals = g_ptr_array_new_with_free_func(native_watch_free);
    session->local_rows = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *locals = gtk_tree_view_new_with_model(GTK_TREE_MODEL(session->local_rows));
    gtk_widget_set_name(locals, "insight-gdb-frame-variables");
    const gchar *local_columns[] = {"Name", "Value", "Kind"};
    for (guint i = 0; i < G_N_ELEMENTS(local_columns); i++)
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(locals), -1,
            insight_tr(local_columns[i]), gtk_cell_renderer_text_new(), "text", i, NULL);
    GtkWidget *local_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(local_scroll), locals);
    session->graph = g_new0(InsightLuaWatchGraph, 1);
    session->graph_view = g_new0(ScriptDebugWindow, 1);
    session->graph_view->watch_paths = g_ptr_array_new_with_free_func(g_free);
    session->graph_view->watch_selected = -1;
    session->graph_view->watch_graph = session->graph;
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    session->console = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(session->console, FALSE);
    gtk_text_view_set_monospace(session->console, TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(session->console));
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), scroll, gtk_label_new(insight_tr("Console")));
    GtkWidget *watch_scroll = gtk_scrolled_window_new(NULL, NULL);
    session->watches = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(session->watches, FALSE);
    gtk_text_view_set_monospace(session->watches, TRUE);
    gtk_container_add(GTK_CONTAINER(watch_scroll), GTK_WIDGET(session->watches));
    GtkWidget *watch_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *watch_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    session->watch_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(session->watch_entry), 80);
    gtk_entry_set_placeholder_text(GTK_ENTRY(session->watch_entry), insight_tr("GDB watch expression"));
    session->watch_add = gtk_button_new_with_label(insight_tr("Add Watch"));
    session->watch_remove = gtk_button_new_with_label(insight_tr("Remove Watch"));
    g_signal_connect(session->watch_add, "clicked", G_CALLBACK(native_watch_edit), dialog);
    g_signal_connect(session->watch_remove, "clicked", G_CALLBACK(native_watch_edit), dialog);
    gtk_box_pack_start(GTK_BOX(watch_row), session->watch_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(watch_row), session->watch_add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(watch_row), session->watch_remove, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(watch_page), watch_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(watch_page), watch_scroll, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), watch_page, gtk_label_new(insight_tr("Watches")));
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), debug_create_watch_tab(session->graph_view),
                             gtk_label_new(insight_tr("Watch graph")));
    GtkWidget *inspection = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    session->inspection = debug_create_view(inspection, TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), inspection, gtk_label_new(insight_tr("Stack and locals")));
    GtkWidget *log = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    session->observer_log = debug_create_view(log, TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), log, gtk_label_new(insight_tr("Observer log")));
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), local_scroll, gtk_label_new(insight_tr("Variables")));
    session->point_rows = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *points = gtk_tree_view_new_with_model(GTK_TREE_MODEL(session->point_rows));
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(points), -1,
        insight_tr("Function or file:line"), gtk_cell_renderer_text_new(), "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(points), -1,
        insight_tr("Number"), gtk_cell_renderer_text_new(), "text", 1, NULL);
    GtkWidget *point_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(point_scroll), points);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), point_scroll,
                             gtk_label_new(insight_tr("Source breakpoints")));
    GtkWidget *context = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    session->threads = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(context), session->threads, FALSE, FALSE, 0);
    session->stack_rows = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                             G_TYPE_STRING, G_TYPE_STRING);
    session->stack = gtk_tree_view_new_with_model(GTK_TREE_MODEL(session->stack_rows));
    gtk_widget_set_name(session->stack, "insight-gdb-stack");
    const gchar *stack_columns[] = {"Frame", "Function", "File", "Line number"};
    const gchar *stack_colors[] = {"#8a50ae", "#167c70", NULL, "#a85416"};
    for (guint i = 0; i < G_N_ELEMENTS(stack_columns); i++)
    {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        if (stack_colors[i])
            g_object_set(renderer, "foreground", stack_colors[i], NULL);
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(session->stack), -1,
            insight_tr(stack_columns[i]), renderer, "text", i, NULL);
    }
    GtkWidget *stack_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(stack_scroll), session->stack);
    gtk_box_pack_start(GTK_BOX(context), stack_scroll, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), context,
                             gtk_label_new(insight_tr("Threads and stack")));
    g_signal_connect(session->threads, "changed", G_CALLBACK(native_select_thread), dialog);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(session->stack)), "changed",
                     G_CALLBACK(native_select_frame), dialog);
    g_signal_connect(session->stack, "row-activated", G_CALLBACK(native_open_frame), dialog);
    gtk_box_pack_start(GTK_BOX(box), tabs, TRUE, TRUE, 0);
    session->command = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(session->command), 1024);
    gtk_entry_set_placeholder_text(GTK_ENTRY(session->command), insight_tr("GDB command, e.g. break main"));
    g_signal_connect(session->command, "activate", G_CALLBACK(native_debug_command), dialog);
    gtk_box_pack_start(GTK_BOX(box), session->command, FALSE, FALSE, 4);
    g_object_set_data_full(G_OBJECT(dialog), "native-debug", session, native_debug_free);
    GtkWidget *debug_commands[INSIGHT_DEBUG_COMMAND_COUNT] = {
        [INSIGHT_DEBUG_RUN] = session->buttons[NATIVE_RUN],
        [INSIGHT_DEBUG_PAUSE] = session->buttons[NATIVE_PAUSE],
        [INSIGHT_DEBUG_CONTINUE] = session->buttons[NATIVE_CONTINUE],
        [INSIGHT_DEBUG_INTO] = session->buttons[NATIVE_INTO],
        [INSIGHT_DEBUG_OVER] = session->buttons[NATIVE_OVER],
        [INSIGHT_DEBUG_OUT] = session->buttons[NATIVE_OUT]
    };
    insight_debug_register(dialog, session->breakpoint, native_debug_toggle, dialog, debug_commands);
    g_signal_connect(dialog, "destroy", G_CALLBACK(native_debug_close), session);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show_all(dialog);

    g_data_input_stream_read_line_async(session->output, G_PRIORITY_DEFAULT,
        session->cancel, native_debug_read, g_object_ref(dialog));
    native_debug_send(dialog, "-gdb-set mi-async on");
}

/* EN: Open a modal executable picker from item and launch GDB on acceptance.
 * data is unused. Report a missing GDB in the status bar.
 * RU: Открывает модальный выбор исполняемого файла по item и запускает GDB
 * при подтверждении. data не используется. Отсутствие GDB показывает в статусе. */
static void debug_native_executable(GtkMenuItem *item, gpointer data)
{
    (void)item;
    (void)data;
#ifdef G_OS_WIN32
    const gchar *gdb_name = "gdb.exe";
#else
    const gchar *gdb_name = "gdb";
#endif
    gchar *gdb = g_find_program_in_path(gdb_name);
    if (!gdb)
    {
        ui_set_statusbar(TRUE, "%s", insight_tr("GDB was not found in PATH."));
        return;
    }
    GtkWidget *chooser = gtk_file_chooser_dialog_new(insight_tr("Debug C/C++ Executable"),
        GTK_WINDOW(main_widgets.window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Debug", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_modal(GTK_WINDOW(chooser), TRUE);
    gchar *root = app->project ? project_get_base_path() : NULL;
    if (root)
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), root);
    g_free(root);
    gchar *target = gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT ?
        gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)) : NULL;
    gtk_widget_destroy(chooser);
    if (target)
        native_debug_show(gdb, target);
    g_free(target);
    g_free(gdb);
}
