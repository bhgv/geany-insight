/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Drive the real debugger through breakpoints, trees and live I/O.
 * RU: Проверяет реальный отладчик, точки останова, деревья и ввод-вывод. */

/* EN: Drive GTK actions for the debugger fixture in data and check live results.
 * Return G_SOURCE_CONTINUE while awaiting the next UI state.
 * RU: Выполняет действия GTK для сценария из data и проверяет показания.
 * Возвращает G_SOURCE_CONTINUE в ожидании следующего состояния интерфейса. */
static gboolean drive_debugger(gpointer data)
{
    (void)data;
    GeanyDocument *document = document_get_current();
    if (!DOC_VALID(document))
        return G_SOURCE_CONTINUE;

    if (phase == OPEN_DEBUGGER)
    {
        GtkWidget *item = find_control(gtk_widget_get_toplevel(GTK_WIDGET(document->editor->sci)),
                                      "insight-tools-debug-current-hdl");
        if (!GTK_IS_MENU_ITEM(item))
            return G_SOURCE_CONTINUE;
        phase = live_devices ? RUN_LIVE_DEVICES : DISABLE_SIGNAL_RULE;
        g_timeout_add(100, accept_ports, NULL);
        gtk_menu_item_activate(GTK_MENU_ITEM(item));
        return G_SOURCE_CONTINUE;
    }

    GtkWidget *simulation = find_window("Verilog Simulation");
    if (phase == WAIT_FOR_CAPTURE)
    {
        GtkWidget *capture = find_window("VCD Waveforms");
        if (!simulation && capture && select_track(capture, "counter_tb.dut.count"))
        {
            phase = INSPECT_CONNECTIONS;
            GtkWidget *editor = gtk_widget_get_toplevel(GTK_WIDGET(document->editor->sci));
            GtkWidget *status = find_control(editor, "statusbar");
            if (status)
                g_signal_connect(status, "text-pushed", G_CALLBACK(log_graph_status), NULL);
            g_timeout_add(100, open_hdl_uses, NULL);
            if (!click(capture, "Signal Map…"))
                return fail_timeout(NULL);
        }
        return G_SOURCE_CONTINUE;
    }
    if (phase == INSPECT_CONNECTIONS)
    {
        GtkWidget *uses = find_window("HDL Uses");
        if (!uses)
            return G_SOURCE_CONTINUE;
        GtkWidget *notebook = find_control(uses, "notebook");
        if (!notebook)
            return fail_timeout(NULL);
        GtkWidget *slice = find_control(uses, "insight-hdl-use-slice");
        if (!GTK_IS_ENTRY(slice))
            return fail_timeout(NULL);
        if (!*gtk_entry_get_text(GTK_ENTRY(slice)))
        {
            gtk_entry_set_text(GTK_ENTRY(slice), "[0]");
            if (!click(uses, "Apply slice"))
                return fail_timeout(NULL);
            return G_SOURCE_CONTINUE;
        }
        gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 1);
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), 1);
        GtkWidget *view = page ? gtk_bin_get_child(GTK_BIN(page)) : NULL;
        GraphChecks checks = {0};
        if (GTK_IS_TREE_VIEW(view))
            gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),
                                   check_connection, &checks);
        gboolean data_bit = checks.bit, mapped = checks.mapped;
        gboolean mixed_clock = checks.clock;
        checks.bit = FALSE;
        page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), 3);
        view = page ? gtk_bin_get_child(GTK_BIN(page)) : NULL;
        if (GTK_IS_TREE_VIEW(view))
            gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),
                                   check_connection, &checks);
        if (!data_bit || !mapped || !checks.clock || mixed_clock || checks.bit)
        {
            g_printerr("Verilog GTK data/events separation failed: data bit %d, mapped bit %d, event clock %d, mixed %d.\n",
                       data_bit, mapped, checks.clock, mixed_clock || checks.bit);
            return fail_timeout(NULL);
        }
        gtk_dialog_response(GTK_DIALOG(uses), GTK_RESPONSE_CLOSE);
        GtkWidget *capture = find_window("VCD Waveforms");
        if (!capture || !click(capture, "Signal Instruments…"))
            return fail_timeout(NULL);
        phase = INSPECT_INSTRUMENTS;
        return G_SOURCE_CONTINUE;
    }
    if (phase == INSPECT_INSTRUMENTS)
    {
        GtkWidget *window = find_window("Signal Instruments");
        if (!window)
            return G_SOURCE_CONTINUE;
        GtkWidget *y = find_control(window, "insight-instrument-Y");
        GtkWidget *z = find_control(window, "insight-instrument-Z");
        GtkWidget *range = find_control(window, "insight-instrument-range");
        if (!y || !z || !range)
            return fail_timeout(NULL);
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(y)) == 0)
        {
            gtk_combo_box_set_active(GTK_COMBO_BOX(y), 1);
            return G_SOURCE_CONTINUE;
        }
        if (g_str_has_prefix(gtk_label_get_text(GTK_LABEL(range)), "XY plot"))
        {
            gtk_combo_box_set_active(GTK_COMBO_BOX(z), 2);
            return G_SOURCE_CONTINUE;
        }
        if (!g_str_has_prefix(gtk_label_get_text(GTK_LABEL(range)), "XYZ projection"))
            return G_SOURCE_CONTINUE;
        g_print("Verilog GTK: Lua stop, source navigation, VCD, HDL Connections and instruments passed.\n");
        passed = TRUE;
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    if (!simulation)
        return G_SOURCE_CONTINUE;

    if (phase == RUN_LIVE_DEVICES && console_contains(simulation, "GEANY-VVP-READY"))
    {
        if (click(simulation, "UART Console…") && click(simulation, "Continue"))
            phase = SEND_LIVE_INPUT;
    }
    else if (phase == SEND_LIVE_INPUT)
    {
        GtkWidget *uart = find_window("UART Console");
        GtkWidget *entry = uart ? find_control(uart, "Text to send; press Enter") : NULL;
        GtkWidget *input = find_control(simulation, "Binary value (0, 1, x, z)");
        if (entry && input)
        {
            gtk_entry_set_text(GTK_ENTRY(input), "1");
            GtkWidget *toggle = find_control(simulation, "switch_on");
            GtkWidget *slider = find_control(simulation, "insight-slider-level[7:0]");
            if (GTK_IS_RANGE(slider) && gtk_widget_is_sensitive(slider) &&
                GTK_IS_TOGGLE_BUTTON(toggle) && gtk_widget_is_sensitive(toggle) &&
                click(simulation, "Apply Input") && click(simulation, "push"))
            {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), TRUE);
                gtk_range_set_value(GTK_RANGE(slider), 173);
                gtk_entry_set_text(GTK_ENTRY(entry), "OK");
                g_signal_emit_by_name(entry, "activate");
                phase = CHECK_LIVE_DEVICES;
            }
        }
    }
    else if (phase == CHECK_LIVE_DEVICES)
    {
        GtkWidget *uart = find_window("UART Console");
        GtkWidget *readings = find_control(simulation, "insight-live-port-readings");
        const gchar *values = readings ? gtk_label_get_text(GTK_LABEL(readings)) : "";
        GtkWidget *lamp = find_control(simulation, "insight-indicator-ready");
        GtkWidget *hex = find_control(simulation, "insight-indicator-result");
        GtkWidget *decimal = find_control(simulation, "insight-indicator-level_out");
        GtkWidget *table = find_control(simulation, "insight-indicator-hex_out");
        GtkWidget *time_plot = find_control(simulation, "insight-live-scope-1");
        GtkWidget *xy_plot = find_control(simulation, "insight-live-scope-2");
        GtkWidget *xyz_plot = find_control(simulation, "insight-live-scope-3");
        if (GTK_IS_LABEL(time_plot) && strstr(gtk_label_get_text(GTK_LABEL(time_plot)), " · Y ") &&
            GTK_IS_LABEL(xy_plot) && strstr(gtk_label_get_text(GTK_LABEL(xy_plot)), " · X ") &&
            GTK_IS_LABEL(xyz_plot) && strstr(gtk_label_get_text(GTK_LABEL(xyz_plot)), " · Z ") &&
            !console_contains(simulation, "GEANY-SCOPE") &&
            GTK_IS_LABEL(lamp) && g_str_has_suffix(gtk_label_get_text(GTK_LABEL(lamp)), "ON") &&
            GTK_IS_LABEL(hex) && !strcmp(gtk_label_get_text(GTK_LABEL(hex)), "0xA5") &&
            GTK_IS_LABEL(decimal) && !strcmp(gtk_label_get_text(GTK_LABEL(decimal)), "173") &&
            GTK_IS_LABEL(table) && !strcmp(gtk_label_get_text(GTK_LABEL(table)), "01  BE\n00  AD") &&
            uart && console_contains(uart, "OK") &&
            console_contains(simulation, "Waveform capture started at") && strstr(values, "pushed = 1") &&
            strstr(values, "ready = 1") && strstr(values, "result = 10100101  (165)") &&
            strstr(values, "model_result = 00111001  (57)") &&
            strstr(values, "spi_result = 10111010  (186)") &&
            strstr(values, "i2c_result = 10111110  (190)"))
        {
            g_print("Verilog GTK: live inputs, capture trigger, SRAM, UART, SPI NOR and I2C EEPROM images passed.\n");
            passed = TRUE;
            gtk_widget_destroy(simulation);
            gtk_main_quit();
            return G_SOURCE_REMOVE;
        }
    }
    else if (phase == DISABLE_SIGNAL_RULE && restored_signal_rules(simulation, TRUE))
    {
        if (click(simulation, "Enable / disable rule"))
            phase = ENABLE_SIGNAL_RULE;
    }
    else if (phase == ENABLE_SIGNAL_RULE && restored_signal_rules(simulation, FALSE))
    {
        if (click(simulation, "Enable / disable rule"))
            phase = LOAD_OBSERVER;
    }
    else if (phase == LOAD_OBSERVER && console_contains(simulation, "GEANY-VVP-READY") &&
             restored_signal_rules(simulation, TRUE))
    {
        if (!find_control(simulation, "Port Signals…"))
        {
            g_printerr("Missing Port Signals button in the Verilog debugger.\n");
            return fail_timeout(NULL);
        }
        GtkWidget *load = find_control(simulation, "Load Lua Observer…");
        if (load && gtk_widget_is_sensitive(load))
        {
            phase = SET_POINT;
            g_timeout_add(100, accept_fixture, observer_source);
            gtk_button_clicked(GTK_BUTTON(load));
        }
    }
    else if (phase == SET_POINT && console_contains(simulation, "Lua observer loaded."))
    {
        GtkWidget *line = find_control(simulation, "source-line");
        GtkWidget *script = find_control(simulation, "Lua decides");
        if (line && script)
        {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(line), point_line);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(script), TRUE);
            if (click(simulation, "Set Point"))
                phase = RUN_TO_POINT;
        }
    }
    else if (phase == RUN_TO_POINT && console_contains(simulation, "1 source breakpoint(s)."))
    {
        if (click(simulation, "Continue"))
            phase = INSPECT_STOP;
    }
    else if (phase == INSPECT_STOP && console_contains(simulation, "HDL hit 2 at tick 15") &&
             find_control(simulation, "HDL hits = 2"))
    {
        if (click(simulation, "Open HDL Line"))
        {
            GeanyDocument *current = document_get_current();
            if (!DOC_VALID(current) || sci_get_current_line(current->editor->sci) + 1 != (gint)point_line)
            {
                g_printerr("Verilog GTK opened the wrong HDL line.\n");
                exit(1);
            }
            GtkWidget *resume = find_control(simulation, "Continue");
            if (!resume || !gtk_widget_is_sensitive(resume))
                return G_SOURCE_CONTINUE;
            GtkWidget *layout = find_control(simulation, "insight-verilog-signal-layout");
            GtkWidget *hierarchy = find_control(simulation, "insight-verilog-instance-tree");
            if (!GTK_IS_COMBO_BOX(layout) || !GTK_IS_TREE_VIEW(hierarchy) ||
                g_strcmp0(gtk_combo_box_get_active_id(GTK_COMBO_BOX(layout)), "tree") ||
                !gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(hierarchy)), NULL))
                return fail_timeout(NULL);
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(layout), "flat");
            GtkWidget *widget = find_control(simulation, "insight-verilog-registers");
            if (!GTK_IS_TREE_VIEW(widget))
                return fail_timeout(NULL);
            GtkTreeView *tree = GTK_TREE_VIEW(widget);
            GtkWidget *page = gtk_widget_get_parent(widget);
            GtkNotebook *tabs = GTK_NOTEBOOK(gtk_widget_get_parent(page));
            GtkWidget *outer = find_control(simulation, "insight-verilog-inspection-tabs");
            if (!GTK_IS_NOTEBOOK(outer))
                return fail_timeout(NULL);
            GtkWidget *signal_page = widget;
            while (gtk_widget_get_parent(signal_page) != outer)
            {
                signal_page = gtk_widget_get_parent(signal_page);
                if (!signal_page)
                    return fail_timeout(NULL);
            }
            gint signal_index = gtk_notebook_page_num(GTK_NOTEBOOK(outer), signal_page);
            if (signal_index < 0)
                return fail_timeout(NULL);
            gtk_notebook_set_current_page(GTK_NOTEBOOK(outer), signal_index);
            gtk_notebook_set_current_page(tabs, gtk_notebook_page_num(tabs, page));
            GtkTreePath *row = register_row(tree, "counter_tb.dut.count", "0001");
            if (!row)
                return G_SOURCE_CONTINUE;
            gboolean edited = edit_register(tree, row, "10xz");
            gtk_tree_path_free(row);
            if (!edited)
                return fail_timeout(NULL);
            phase = VERIFY_REGISTER_WRITE;
        }
    }
    else if (phase == VERIFY_REGISTER_WRITE || phase == VERIFY_REGISTER_RESTORE)
    {
        GtkWidget *resume = find_control(simulation, "Continue");
        GtkWidget *widget = find_control(simulation, "insight-verilog-registers");
        if (!resume || !gtk_widget_is_sensitive(resume) || !GTK_IS_TREE_VIEW(widget))
            return G_SOURCE_CONTINUE;
        GtkTreeView *tree = GTK_TREE_VIEW(widget);
        gboolean restore = phase == VERIFY_REGISTER_WRITE;
        GtkTreePath *row = register_row(tree, "counter_tb.dut.count", restore ? "10xz" : "0001");
        if (!row)
            return G_SOURCE_CONTINUE;
        phase = restore ? VERIFY_REGISTER_RESTORE : INSPECT_PREVIEW;
        gboolean acted = restore ? edit_register(tree, row, "0001") : open_register_history(tree, row);
        gtk_tree_path_free(row);
        if (!acted)
            return fail_timeout(NULL);
    }
    else if (phase == INSPECT_PREVIEW)
    {
        GtkWidget *preview = find_window("VCD Waveforms");
        GtkWidget *resume = find_control(simulation, "Continue");
        if (preview && resume && gtk_widget_is_sensitive(resume))
        {
            GtkWidget *value = find_control(preview, "insight-wave-cursor-value");
            if (!GTK_IS_LABEL(value) || !g_str_has_prefix(gtk_label_get_text(GTK_LABEL(value)), "Tick 0 "))
                return fail_timeout(NULL);
            gtk_widget_destroy(preview);
            if (click(simulation, "Continue"))
                phase = WAIT_FOR_CAPTURE;
        }
    }
    return G_SOURCE_CONTINUE;
}

