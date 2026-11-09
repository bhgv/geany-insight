/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Debugger window, controls, inspection tabs and worker startup.
 * RU: Окно отладчика, органы управления, вкладки и запуск задачи.
 */

/* EN: Add contents to tabs in a scrollable page named title. The notebook owns it.
 * RU: Добавляет contents в прокручиваемую вкладку title. Вкладкой владеет tabs. */
static void simulation_add_tab(GtkWidget *tabs, GtkWidget *contents, const gchar *title)
{
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), contents);
    gtk_notebook_append_page(GTK_NOTEBOOK(tabs), scroll, gtk_label_new(insight_tr(title)));
}

/* EN: Launch Icarus from bin with include_dir and sources, owned by the caller.
 * parent owns setup dialogs, interactive enables debugger controls, navigate and
 * open_location handle HDL navigation, settings_directory and project key saved
 * settings, and configure_ports opens the port setup. show_context previews declarations. Errors appear in the UI.
 * RU: Запускает Icarus из bin для sources с include_dir. parent владеет диалогами,
 * interactive включает отладку, navigate и open_location открывают исходник,
 * settings_directory и project определяют сохранённые настройки, configure_ports
 * открывает настройку портов. show_context показывает контекст объявлений. Ошибки показываются в интерфейсе. */
void insight_waveforms_simulate(GtkWindow *parent, const gchar *bin, const gchar *include_dir,
                                const gchar *const *sources, gboolean interactive,
                                InsightWaveSource navigate, InsightWaveContext show_context,
                                InsightWaveLocation open_location, const gchar *settings_directory,
                                const gchar *project, gboolean configure_ports)
{
#ifdef G_OS_WIN32
    const gchar *compiler_name = "iverilog.exe", *runtime_name = "vvp.exe";
#else
    const gchar *compiler_name = "iverilog", *runtime_name = "vvp";
#endif
    gchar *compiler = simulation_tool(bin, compiler_name);
    gchar *runtime = simulation_tool(bin, runtime_name);
    if (!compiler || !runtime)
    {
        GtkWidget *dialog = gtk_message_dialog_new(
            parent, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Icarus Verilog is unavailable. Install the editor with its simulator.");
        g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
        gtk_widget_show(dialog);
        g_free(compiler);
        g_free(runtime);
        return;
    }

    WaveSimulation *run = g_new0(WaveSimulation, 1);
    g_weak_ref_init(&run->parent, parent);
    g_mutex_init(&run->lock);
    g_cond_init(&run->live_output_space);
    run->interactive = interactive;
    run->compiler = compiler;
    run->runtime = runtime;
    run->include_dir = g_strdup(include_dir);
    run->settings_directory = g_strdup(settings_directory);
    run->project = g_strdup(project);
    run->sources = g_strdupv((gchar **)sources);
    run->navigate = navigate;
    run->show_context = show_context;
    run->open_location = open_location;
    if (configure_ports)
    {
        gchar *dut_source[] = {run->sources[0], NULL};
        gchar *key = simulation_settings_path(settings_directory, project, dut_source);
        run->port_settings = g_strconcat(key, ".ports", NULL);
        g_free(key);
        gboolean accepted = simulation_edit_ports(parent, run->port_settings, run, &run->port_harness,
                                                  &run->port_model_sources, &run->port_images);
        if (!accepted)
        {
            simulation_free(run);
            return;
        }
    }
    simulation_rasters_prepare(run);
    run->dialog =
        gtk_dialog_new_with_buttons("Verilog Simulation", parent,
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    simulation_dialog_tools(
        run->dialog,
        "Use the top toolbar to continue, step or pause. Switch tabs to inspect signals, "
        "configure breakpoints or use test devices.",
        "Верхняя панель позволяет продолжить, выполнить шаг или поставить симуляцию на паузу. "
        "Переключайте вкладки для просмотра сигналов, настройки точек останова и работы с устройствами.");
    g_object_ref_sink(run->dialog);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(run->dialog));
    if (!interactive)
        gtk_box_pack_start(GTK_BOX(box),
                          gtk_label_new(insight_tr("Compiling and running the Verilog testbench…")),
                          FALSE, FALSE, 12);
    if (interactive)
    {
        GdkRectangle area = {0, 0, 1024, 768};
        GdkDisplay *display = gtk_widget_get_display(run->dialog);
        GdkWindow *owner = parent ? gtk_widget_get_window(GTK_WIDGET(parent)) : NULL;
        GdkMonitor *monitor = owner ? gdk_display_get_monitor_at_window(display, owner)
                                    : gdk_display_get_primary_monitor(display);
        if (monitor)
            gdk_monitor_get_workarea(monitor, &area);
        gtk_window_set_default_size(GTK_WINDOW(run->dialog), MIN(1050, area.width * 9 / 10),
                                    MIN(590, area.height * 8 / 10));
        gtk_window_set_position(GTK_WINDOW(run->dialog), GTK_WIN_POS_CENTER_ON_PARENT);
        run->protocol_line = g_string_new(NULL);
        run->live_output = g_byte_array_new();
        run->breakpoints = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        run->point_anchors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        run->unresolved_records = g_ptr_array_new_with_free_func(g_free);
        run->unresolved_anchors = g_ptr_array_new_with_free_func(g_free);
        run->settings_path = simulation_settings_path(settings_directory, project, run->sources);
        gchar *no_sources[] = {NULL};
        gchar *pin_key =
            simulation_settings_path(settings_directory, project, project ? no_sources : run->sources);
        run->signal_pins_path = pin_key ? g_strconcat(pin_key, ".pins", NULL) : NULL;
        run->signal_stop_path = pin_key ? g_strconcat(pin_key, ".signal-stops", NULL) : NULL;
        g_free(pin_key);
        simulation_load_settings(run);
        GtkWidget *running_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(box), running_actions, FALSE, FALSE, 0);
        run->pause_reason = gtk_label_new(insight_tr("Compiling Verilog…"));
        gtk_label_set_xalign(GTK_LABEL(run->pause_reason), 0);
        gtk_box_pack_start(GTK_BOX(box), run->pause_reason, FALSE, FALSE, 0);
        if (run->port_settings)
        {
            GtkWidget *ports = gtk_button_new_with_label("Port Signals…");
            g_signal_connect(ports, "clicked", G_CALLBACK(simulation_port_settings), run);
            gtk_box_pack_start(GTK_BOX(running_actions), ports, FALSE, FALSE, 0);
        }
        gboolean synchronous_console = run->port_harness &&
            strstr(run->port_harness, "// GEANY USART CONSOLE\n");
        if (synchronous_console ||
            (run->port_harness && strstr(run->port_harness, "// GEANY UART CONSOLE\n")))
        {
            run->uart_log = g_string_new(NULL);
            GtkWidget *uart = gtk_button_new_with_label(synchronous_console ? "USART Console…" : "UART Console…");
            g_signal_connect(uart, "clicked", G_CALLBACK(simulation_uart_show), run);
            gtk_box_pack_start(GTK_BOX(running_actions), uart, FALSE, FALSE, 0);
        }
        run->pause_button = gtk_button_new_with_label("Pause");
        gtk_widget_set_sensitive(run->pause_button, FALSE);
        g_signal_connect(run->pause_button, "clicked", G_CALLBACK(simulation_request_pause), run);
        gtk_box_pack_start(GTK_BOX(running_actions), run->pause_button, FALSE, FALSE, 0);
        run->source_button = gtk_button_new_with_label("Open HDL Line");
        gtk_widget_set_sensitive(run->source_button, FALSE);
        g_signal_connect(run->source_button, "clicked", G_CALLBACK(simulation_open_location), run);
        gtk_box_pack_start(GTK_BOX(running_actions), run->source_button, FALSE, FALSE, 0);
        run->console = gtk_text_view_new();
        if (run->moved_points || run->unresolved_records->len)
        {
            gchar *summary = g_strdup_printf(insight_tr("Restored points: %u moved, %u unresolved.\n"),
                                             run->moved_points, run->unresolved_records->len);
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console)), summary, -1);
            g_free(summary);
        }
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(run->console), TRUE);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(run->console), FALSE);
        GtkWidget *inspection_tabs = gtk_notebook_new();
        gtk_notebook_set_scrollable(GTK_NOTEBOOK(inspection_tabs), TRUE);
        gtk_widget_set_name(inspection_tabs, "insight-verilog-inspection-tabs");
        simulation_add_tab(inspection_tabs, run->console, "Simulator log");
        if (run->port_harness && strstr(run->port_harness, "task geany_live_yield;"))
        {
            GtkWidget *ports = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            simulation_live_panel(run, ports);
            simulation_add_tab(inspection_tabs, ports, "Live Port I/O");
        }
        simulation_add_tab(inspection_tabs, simulation_signal_tables(run), "HDL Signals");
        simulation_add_tab(inspection_tabs, simulation_signal_stop_page(run), "Signal stops");
        simulation_add_tab(inspection_tabs, simulation_frame_page(run), "Frame values");
        if (run->rasters)
            simulation_add_tab(inspection_tabs, simulation_raster_notebook(run, FALSE), "Displays");
        if (run->port_harness && strstr(run->port_harness, "$geany_lua_console_poll("))
        {
            run->lua_terminals = gtk_notebook_new();
            gtk_notebook_set_scrollable(GTK_NOTEBOOK(run->lua_terminals), TRUE);
            simulation_add_tab(inspection_tabs, run->lua_terminals, "Lua terminals");
        }
        run->live_controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_pack_start(GTK_BOX(box), run->live_controls, FALSE, FALSE, 6);
        GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->live_controls), actions, FALSE, FALSE, 0);
        GtkWidget *debug_commands[INSIGHT_DEBUG_COMMAND_COUNT] = {NULL};
        const gchar *labels[] = {"Step Event", "Continue", "Time", "Scope", "List", "Finish"};
        const gchar *commands[] = {"step\n", "cont\n", "time\n", "where\n", "list\n", "finish\n"};
        for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
        {
            GtkWidget *button = gtk_button_new_with_label(labels[i]);
            g_object_set_data(G_OBJECT(button), "vvp-command", (gpointer)commands[i]);
            g_signal_connect(button, "clicked", G_CALLBACK(simulation_send_command), run);
            gtk_box_pack_start(GTK_BOX(actions), button, FALSE, FALSE, 0);
            if (i == 1)
                debug_commands[INSIGHT_DEBUG_CONTINUE] = button;
            else if (i == 5)
                debug_commands[INSIGHT_DEBUG_STOP] = button;
        }
        run->source_step_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->live_controls), run->source_step_controls, FALSE, FALSE, 0);
        const gchar *step_labels[] = {"Step HDL", "Step Current Process", "Next Tick"};
        const gchar *step_commands[] = {"geany-step-source\n", "geany-step-process\n",
                                        "geany-step-tick\n"};
        for (guint i = 0; i < G_N_ELEMENTS(step_labels); i++)
        {
            GtkWidget *button = gtk_button_new_with_label(step_labels[i]);
            g_object_set_data(G_OBJECT(button), "vvp-command", (gpointer)step_commands[i]);
            g_signal_connect(button, "clicked", G_CALLBACK(simulation_send_command), run);
            gtk_box_pack_start(GTK_BOX(run->source_step_controls), button, FALSE, FALSE, 0);
            if (i == 0)
                debug_commands[INSIGHT_DEBUG_INTO] = button;
        }
        gtk_widget_set_sensitive(run->source_step_controls, FALSE);
        gtk_box_pack_start(GTK_BOX(box), inspection_tabs, TRUE, TRUE, 0);
        GtkWidget *process_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        simulation_add_tab(inspection_tabs, process_page, "Processes and memory");
        g_object_bind_property(run->live_controls, "sensitive", process_page, "sensitive",
                               G_BINDING_SYNC_CREATE);
        run->process_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(process_page), run->process_controls, FALSE, FALSE, 0);
        GtkWidget *processes = gtk_button_new_with_label("Processes");
        g_object_set_data(G_OBJECT(processes), "vvp-command", "geany-processes\n");
        g_signal_connect(processes, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(run->process_controls), processes, FALSE, FALSE, 0);
        run->process_number = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->process_number), 20);
        gtk_entry_set_width_chars(GTK_ENTRY(run->process_number), 10);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->process_number), "Process ID");
        gtk_box_pack_start(GTK_BOX(run->process_controls), run->process_number, FALSE, FALSE, 0);
        GtkWidget *select_process = gtk_button_new_with_label("Select Process");
        g_signal_connect(select_process, "clicked", G_CALLBACK(simulation_select_process), run);
        gtk_box_pack_start(GTK_BOX(run->process_controls), select_process, FALSE, FALSE, 0);
        GtkWidget *process_values = gtk_button_new_with_label("Process Values");
        g_object_set_data(G_OBJECT(process_values), "vvp-command", "geany-process-values\n");
        g_signal_connect(process_values, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(run->process_controls), process_values, FALSE, FALSE, 0);
        gtk_widget_set_sensitive(run->process_controls, FALSE);
        run->frame_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(process_page), run->frame_controls, FALSE, FALSE, 0);
        run->frame_depth = gtk_spin_button_new_with_range(0, 63, 1);
        gtk_box_pack_start(GTK_BOX(run->frame_controls), run->frame_depth, FALSE, FALSE, 0);
        GtkWidget *frame = gtk_button_new_with_label("Select Frame");
        g_signal_connect(frame, "clicked", G_CALLBACK(simulation_select_frame), run);
        gtk_box_pack_start(GTK_BOX(run->frame_controls), frame, FALSE, FALSE, 0);
        run->memory_word = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->memory_word), 128);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->memory_word), "name[index]");
        gtk_box_pack_start(GTK_BOX(run->frame_controls), run->memory_word, TRUE, TRUE, 0);
        GtkWidget *memory = gtk_button_new_with_label("Read Memory Word");
        gtk_widget_set_tooltip_text(memory,
                                    insight_tr("Read an element of memory, a dynamic array, or a queue"));
        g_signal_connect(memory, "clicked", G_CALLBACK(simulation_read_memory_word), run);
        gtk_box_pack_start(GTK_BOX(run->frame_controls), memory, FALSE, FALSE, 0);
        gtk_widget_set_sensitive(run->frame_controls, FALSE);
        GtkWidget *inspection = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *tools_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        simulation_add_tab(inspection_tabs, tools_page, "Inspection tools");
        g_object_bind_property(run->live_controls, "sensitive", tools_page, "sensitive",
                               G_BINDING_SYNC_CREATE);
        gtk_box_pack_start(GTK_BOX(tools_page), inspection, FALSE, FALSE, 0);
        run->signal_name = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->signal_name), 128);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->signal_name), "Signal or scope");
        gtk_box_pack_start(GTK_BOX(inspection), run->signal_name, TRUE, TRUE, 0);
        GtkWidget *read = gtk_button_new_with_label("Read Signal");
        g_signal_connect(read, "clicked", G_CALLBACK(simulation_inspect_name), run);
        gtk_box_pack_start(GTK_BOX(inspection), read, FALSE, FALSE, 0);
        GtkWidget *scope = gtk_button_new_with_label("Enter Scope");
        g_object_set_data(G_OBJECT(scope), "scope", GINT_TO_POINTER(1));
        g_signal_connect(scope, "clicked", G_CALLBACK(simulation_inspect_name), run);
        gtk_box_pack_start(GTK_BOX(inspection), scope, FALSE, FALSE, 0);
        GtkWidget *up = gtk_button_new_with_label("Parent Scope");
        g_object_set_data(G_OBJECT(up), "vvp-command", "pop\n");
        g_signal_connect(up, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(inspection), up, FALSE, FALSE, 0);
        GtkWidget *graph = gtk_button_new_with_label(insight_tr("Watch graph"));
        gtk_widget_set_name(graph, "insight-verilog-watch-graph");
        gtk_widget_set_sensitive(graph, run->navigate != NULL);
        g_signal_connect(graph, "clicked", G_CALLBACK(simulation_watch_graph), run);
        gtk_box_pack_start(GTK_BOX(inspection), graph, FALSE, FALSE, 0);

        GtkWidget *trace_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(tools_page), trace_actions, FALSE, FALSE, 0);
        GtkWidget *preview = gtk_button_new_with_label("Preview Waveform");
        g_signal_connect(preview, "clicked", G_CALLBACK(simulation_preview), run);
        gtk_box_pack_start(GTK_BOX(trace_actions), preview, FALSE, FALSE, 0);
        GtkWidget *trace = gtk_button_new_with_label("Trace On");
        g_object_set_data(G_OBJECT(trace), "vvp-command", "trace on\n");
        g_signal_connect(trace, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(trace_actions), trace, FALSE, FALSE, 0);
        GtkWidget *trace_off = gtk_button_new_with_label("Trace Off");
        g_object_set_data(G_OBJECT(trace_off), "vvp-command", "trace off\n");
        g_signal_connect(trace_off, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(trace_actions), trace_off, FALSE, FALSE, 0);
        run->breakpoint_controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget *breakpoint_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        simulation_add_tab(inspection_tabs, breakpoint_page, "Source breakpoints");
        g_object_bind_property(run->live_controls, "sensitive", breakpoint_page, "sensitive",
                               G_BINDING_SYNC_CREATE);
        gtk_box_pack_start(GTK_BOX(breakpoint_page), run->breakpoint_controls, FALSE, FALSE, 0);
        run->breakpoint_status = gtk_label_new(insight_tr(
            "Select a file and an executable HDL line. Leave both condition fields empty for an unconditional stop."));
        gtk_label_set_xalign(GTK_LABEL(run->breakpoint_status), 0);
        gtk_label_set_line_wrap(GTK_LABEL(run->breakpoint_status), TRUE);
        gtk_box_pack_start(GTK_BOX(run->breakpoint_controls), run->breakpoint_status, FALSE, FALSE, 0);
        GtkWidget *location = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->breakpoint_controls), location, FALSE, FALSE, 0);
        run->breakpoint_file = gtk_combo_box_text_new();
        for (guint i = 0; run->sources[i]; i++)
        {
            gchar *name = g_filename_display_basename(run->sources[i]);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(run->breakpoint_file), name);
            g_free(name);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(run->breakpoint_file), 0);
        gtk_box_pack_start(GTK_BOX(location), run->breakpoint_file, TRUE, TRUE, 0);
        run->breakpoint_line = gtk_spin_button_new_with_range(1, G_MAXINT, 1);
        gtk_box_pack_start(GTK_BOX(location), gtk_label_new(insight_tr("Line number")), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(location), run->breakpoint_line, FALSE, FALSE, 0);
        GtkWidget *toggle = gtk_button_new_with_label("Set Point");
        g_signal_connect(toggle, "clicked", G_CALLBACK(simulation_edit_breakpoint), run);
        gtk_box_pack_start(GTK_BOX(location), toggle, FALSE, FALSE, 0);
        GtkWidget *remove = gtk_button_new_with_label("Remove Point");
        g_object_set_data(G_OBJECT(remove), "point-action", GINT_TO_POINTER(1));
        g_signal_connect(remove, "clicked", G_CALLBACK(simulation_edit_breakpoint), run);
        gtk_box_pack_start(GTK_BOX(location), remove, FALSE, FALSE, 0);
        GtkWidget *clear = gtk_button_new_with_label("Clear Points");
        g_object_set_data(G_OBJECT(clear), "point-action", GINT_TO_POINTER(2));
        g_signal_connect(clear, "clicked", G_CALLBACK(simulation_edit_breakpoint), run);
        gtk_box_pack_start(GTK_BOX(location), clear, FALSE, FALSE, 0);
        GtkWidget *list_points = gtk_button_new_with_label("List Points");
        g_object_set_data(G_OBJECT(list_points), "vvp-command", "geany-list\n");
        g_signal_connect(list_points, "clicked", G_CALLBACK(simulation_send_command), run);
        GtkWidget *point_lists = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->breakpoint_controls), point_lists, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(point_lists), list_points, FALSE, FALSE, 0);
        GtkWidget *unresolved = gtk_button_new_with_label("Unresolved Points");
        g_signal_connect(unresolved, "clicked", G_CALLBACK(simulation_list_unresolved), run);
        gtk_box_pack_start(GTK_BOX(point_lists), unresolved, FALSE, FALSE, 0);
        GtkWidget *rule = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->breakpoint_controls), rule, FALSE, FALSE, 0);
        run->rule_signal = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->rule_signal), 512);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->rule_signal), "Full signal name");
        gtk_box_pack_start(GTK_BOX(rule), run->rule_signal, TRUE, TRUE, 0);
        run->rule_bits = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->rule_bits), 1024);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->rule_bits), "Binary value");
        gtk_box_pack_start(GTK_BOX(rule), run->rule_bits, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(rule), gtk_label_new("Hits"), FALSE, FALSE, 0);
        run->rule_hits = gtk_spin_button_new_with_range(1, 1000000, 1);
        gtk_box_pack_start(GTK_BOX(rule), run->rule_hits, FALSE, FALSE, 0);
        run->rule_log = gtk_check_button_new_with_label("Log and continue");
        gtk_box_pack_start(GTK_BOX(rule), run->rule_log, FALSE, FALSE, 0);
        run->rule_script = gtk_check_button_new_with_label("Lua decides");
        gtk_box_pack_start(GTK_BOX(rule), run->rule_script, FALSE, FALSE, 0);
        g_signal_connect(run->breakpoint_file, "changed", G_CALLBACK(simulation_rule_selected), run);
        g_signal_connect(run->breakpoint_line, "value-changed", G_CALLBACK(simulation_rule_selected),
                         run);
        simulation_rule_selected(NULL, run);
        GtkWidget *load_script = gtk_button_new_with_label("Load Lua Observer…");
        g_signal_connect(load_script, "clicked", G_CALLBACK(simulation_load_observer), run);
        GtkWidget *scripts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->breakpoint_controls), scripts, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(scripts), load_script, FALSE, FALSE, 0);
        run->reload_script = gtk_button_new_with_label("Reload Saved Lua");
        gtk_widget_set_sensitive(run->reload_script, run->observer_file != NULL);
        g_signal_connect(run->reload_script, "clicked", G_CALLBACK(simulation_load_observer), run);
        gtk_box_pack_start(GTK_BOX(scripts), run->reload_script, FALSE, FALSE, 0);
        GtkWidget *disable_script = gtk_button_new_with_label("Disable Lua");
        g_signal_connect(disable_script, "clicked", G_CALLBACK(simulation_disable_observer), run);
        gtk_box_pack_start(GTK_BOX(scripts), disable_script, FALSE, FALSE, 0);
        run->lua_watches = gtk_label_new(NULL);
        gtk_label_set_selectable(GTK_LABEL(run->lua_watches), TRUE);
        gtk_label_set_xalign(GTK_LABEL(run->lua_watches), 0);
        simulation_add_tab(inspection_tabs, run->lua_watches, "Lua watches");
        gtk_widget_set_sensitive(run->breakpoint_controls, FALSE);
        g_object_set_data(G_OBJECT(run->dialog), "insight-live-simulation", run);
        gtk_widget_set_sensitive(run->live_controls, FALSE);
        debug_commands[INSIGHT_DEBUG_PAUSE] = run->pause_button;
        insight_debug_register(run->dialog, run->breakpoint_controls, simulation_toggle_editor_point,
                               run, debug_commands);
        run->refresh_source = g_timeout_add(100, simulation_refresh_console, run);
    }

    GCancellable *cancel = g_cancellable_new();
    g_signal_connect_object(run->dialog, "response", G_CALLBACK(simulation_cancel), cancel, 0);
    g_signal_connect(run->dialog, "destroy", G_CALLBACK(simulation_dialog_destroy), run);
    g_signal_connect_object(run->dialog, "destroy", G_CALLBACK(g_cancellable_cancel), cancel,
                            G_CONNECT_SWAPPED);
    GTask *task = g_task_new(NULL, cancel, simulation_done, NULL);
    g_task_set_task_data(task, run, simulation_free);
    gtk_widget_show_all(run->dialog);
    g_task_run_in_thread(task, simulation_worker);
    g_object_unref(task);
    g_object_unref(cancel);
}
