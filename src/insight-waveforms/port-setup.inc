/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Persistent port setup dialog and recipe application.
 * RU: Окно настроек портов, сохранение и применение рецепта.
 */

/* EN: Edit the recipe saved at path in a child of parent. run supplies the compiler,
 * input sources, and include path. On cancellation, publish preprocessing errors.
 * On acceptance, optional output
 * pointers harness, model_sources and port_images receive the generated configuration.
 * RU: Редактирует рецепт из path в дочернем окне parent. run задаёт компилятор,
 * исходники и путь include. При отмене публикует ошибки препроцессора.
 * При подтверждении необязательные указатели harness,
 * model_sources и port_images получают сгенерированную конфигурацию. */
static gboolean simulation_edit_ports(GtkWindow *parent, const gchar *path, const WaveSimulation *run,
                                      gchar **harness, GPtrArray **model_sources,
                                      GHashTable **port_images)
{
    static GtkWidget *active_setup;
    if (active_setup)
    {
        gtk_window_present(GTK_WINDOW(active_setup));
        return FALSE;
    }
    gchar *include_directory = g_strdup(run->include_dir);
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Port Signals", parent, GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        harness ? "_Start Debugging" : "_Save for Next Run", GTK_RESPONSE_ACCEPT, NULL);
    simulation_dialog_tools(
        dialog,
        "Choose a module and add device rules, or leave the recipe empty to run an existing "
        "testbench. Ports shows directions, widths and assignments. Source is copyable. Use the "
        "separate HDL and diagram previews to review the recipe.",
        "Укажите модуль и добавьте правила устройств или оставьте настройки пустыми для "
        "готового тестбенча. В Ports видны направления, ширина и привязки. Из Source можно "
        "копировать исходник. Для проверки откройте просмотр HDL или схему.");
    g_object_ref_sink(dialog);
    active_setup = dialog;
    gchar *source_directory = g_path_get_dirname(run->sources[0]);
    gchar *source_snapshot = port_browser_source(run->sources[0]);
    gchar *preprocess_error = NULL;
    gchar *expanded = port_browser_preprocess(run, &preprocess_error);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 390);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hint = gtk_label_new(
        "Leave empty to run an existing testbench. Otherwise enter one rule per line (times in ns).\n"
        "module NAME   ·   duration NS\n"
        "clock PORT START HIGH LOW   ·   clock-hz PORT START HZ\n"
        "pulse PORT START LENGTH 0|1   ·   value PORT WIDTH EXPR\n"
        "bus PORT WIDTH INTERVAL EXPR   ·   output PORT WIDTH\n"
        "capture-trigger rise|fall SIGNAL   ·   capture-trigger match SIGNAL SIZED_HEX_OR_BINARY\n"
        "button PORT [IDLE [LENGTH_NS]]   ·   switch PORT [INITIAL]\n"
        "slider PORT [WIDTH=8 [INITIAL=0]]\n"
        "scope SIGNAL   ·   scope-xy X Y   ·   scope-xyz X Y Z\n"
        "led PORT [ACTIVE=1]   ·   display PORT [WIDTH=8 [hex|dec|bin]]\n"
        "hex-table PORT [WIDTH=32]\n"
        "live-interval NS   ·   live-input PORT WIDTH INITIAL   ·   monitor PORT WIDTH\n"
        "Memory presets: sram, parallel-psram, sdram, ddr1, ddr2, ddr3, ddr4, ddr5, spi-nor, spi-nand, spi-eeprom, spi-psram, i2c-eeprom.\n"
        "Use a preset alone for standard port names, or list your ports below. '-' uses the default.\n"
        "parallel-psram ADDR DQ CE_N WE_N OE_N LB_N UB_N [ADDRESS_BITS=20]\n"
        "sram ADDR DQ CE_N WE_N OE_N [ADDRESS_BITS=16 [DATA_BITS=8]]\n"
        "sdram CK CKE CS_N RAS_N CAS_N WE_N A BA DQ DQM\n"
        "memory-image ADDR_OR_SPI_CS_N_OR_SDA FILE (before the memory rule)\n"
        "i2c-eeprom SCL SDA [ADDRESS=0x50]\n"
        "spi-controller SCK CS_N MOSI MISO TX_HEX MODE PHASE_NS START_NS\n"
        "i2c-controller SCL SDA ADDRESS TX_HEX_OR_DASH READ_BYTES PHASE_NS START_NS\n"
        "spi-lua [SCK CS_N MOSI MISO] SCRIPT [RESET_N]\n"
        "i2c-lua [SCL SDA ADDRESS] SCRIPT [RESET_N]\n"
        "spi-nor / spi-nand / spi-eeprom / spi-psram SCK CS_N MOSI MISO\n"
        "ddr1 CK CKE CS_N RAS_N CAS_N WE_N A BA DQ DQS DM [RL=2 [WL=1]] (functional x8)\n"
        "ddr2 CK CKE CS_N RAS_N CAS_N WE_N A BA DQ DQS DM [RL=3 [WL=2]] (functional x8)\n"
        "ddr3 CK RESET_N CKE CS_N RAS_N CAS_N WE_N A BA DQ DQS DM [RL=3 [WL=2]] (functional x8)\n"
        "ddr4 CK RESET_N CKE CS_N ACT_N A BA BG DQ DQS DM_N [RL=3 [WL=2]] (functional x8)\n"
        "ddr5 CK RESET_N CS_N CA DQ DQS DM_N [RL=3 [WL=2]] (functional x8 BL16)\n"
        "rgb-display PCLK DE VS R G B WIDTH HEIGHT BITS VS_ACTIVE\n"
        "lvds-display CLK D0 D1 D2 D3_OR_DASH WIDTH HEIGHT BITS jeida|vesa PHASE VS_ACTIVE\n"
        "hdmi-ddc SCL SDA HPD [CONNECT_NS=0] (optional EDID image keyed by SDA)\n"
        "hdmi-display CLK D0 D1 D2 WIDTH HEIGHT BITS hdmi|dvi PHASE VS_ACTIVE\n"
        "uart RX TX BAUD [console]\n"
        "usart RX TX XCK rise|fall [console] (DUT clock)\n"
        "usart-clock RX TX XCK PERIOD_NS rise|fall [console]\n"
        "model INSTANCE MODULE SOURCE   ·   model-link INSTANCE DUT_PORT MODEL_PORT WIDTH\n"
        "model-share INSTANCE DUT_PORT MODEL_PORT   ·   model-param / model-value INSTANCE NAME EXPR\n"
        "Use step in bus expressions and $time for simulation time. Quote formulas containing spaces.");
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    GtkWidget *syntax = gtk_expander_new("Text rule reference");
    gtk_container_add(GTK_CONTAINER(syntax), hint);
    gtk_box_pack_start(GTK_BOX(box), syntax, FALSE, FALSE, 4);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    g_object_set_data_full(G_OBJECT(dialog), "verilog-port-recipe", g_object_ref(buffer), g_object_unref);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *wizard = gtk_button_new_with_label("Add Test Device…");
    g_object_set_data(G_OBJECT(wizard), "source-directory", source_directory);
    gtk_box_pack_start(GTK_BOX(actions), wizard, FALSE, FALSE, 0);
    GtkWidget *preview = gtk_button_new_with_label("Preview Testbench…");
    g_object_set_data(G_OBJECT(preview), "source-directory", source_directory);
    g_signal_connect(preview, "clicked", G_CALLBACK(simulation_preview_harness), buffer);
    gtk_box_pack_start(GTK_BOX(actions), preview, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 4);
    GFile *file = g_file_new_for_path(path);
    GFileInputStream *stream = g_file_read(file, NULL, NULL);
    if (stream)
    {
        gchar saved[16385];
        gsize length = 0;
        if (g_input_stream_read_all(G_INPUT_STREAM(stream), saved, sizeof saved, &length, NULL, NULL) &&
            length < sizeof saved && g_utf8_validate(saved, length, NULL))
            gtk_text_buffer_set_text(buffer, saved, length);
        g_object_unref(stream);
    }
    g_object_unref(file);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    PortBrowser browser = {.source = source_snapshot,
                           .expanded = expanded,
                           .preprocess_error = preprocess_error,
                           .recipe = buffer,
                           .recipe_view = GTK_TEXT_VIEW(view)};
    GtkWidget *panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(panes), scroll, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(panes), port_browser_widget(&browser), TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(panes), 320);
    GtkWidget *diagram = gtk_button_new_with_label("Preview Diagram…");
    g_object_set_data(G_OBJECT(diagram), "source-directory", source_directory);
    g_signal_connect(diagram, "clicked", G_CALLBACK(simulation_preview_diagram), &browser);
    gtk_box_pack_start(GTK_BOX(actions), diagram, FALSE, FALSE, 0);
    g_signal_connect(wizard, "clicked", G_CALLBACK(simulation_device_wizard), &browser);
    g_object_ref(buffer);
    gulong changed = g_signal_connect(buffer, "changed", G_CALLBACK(port_browser_refresh), &browser);
    gtk_box_pack_start(GTK_BOX(box), panes, TRUE, TRUE, 0);
    GtkWidget *status =
        gtk_label_new(harness ? "" : "Saved changes apply the next time debugging starts.");
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 6);
    gtk_widget_show_all(dialog);
    gboolean accepted = FALSE;
    while (insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        GtkTextIter first, last;
        gtk_text_buffer_get_bounds(buffer, &first, &last);
        gchar *recipe = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        GError *error = NULL;
        GPtrArray *sources = g_ptr_array_new_with_free_func(g_free);
        GHashTable *images =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
        gchar *generated = simulation_setup_harness(recipe, source_directory, sources, images, &error);
        gchar *directory = g_path_get_dirname(path);
        if (!error && g_mkdir_with_parents(directory, 0700) == 0 &&
            g_file_set_contents(path, recipe, -1, &error))
        {
            accepted = TRUE;
            if (harness)
                *harness = g_steal_pointer(&generated);
            if (model_sources)
                *model_sources = g_steal_pointer(&sources);
            if (port_images)
                *port_images = g_steal_pointer(&images);
        }
        if (!accepted)
            gtk_label_set_text(GTK_LABEL(status),
                               error ? error->message : "Cannot save the port recipe.");
        g_free(directory);
        g_clear_pointer(&sources, g_ptr_array_unref);
        g_clear_pointer(&images, g_hash_table_unref);
        g_free(generated);
        g_free(recipe);
        g_clear_error(&error);
        if (accepted)
            break;
    }
    g_signal_handler_disconnect(buffer, changed);
    g_object_unref(buffer);
    active_setup = NULL;
    g_free(source_snapshot);
    g_free(expanded);
    g_free(source_directory);
    g_clear_pointer(&browser.ports, g_hash_table_unref);
    g_free(browser.module);
    g_clear_pointer(&browser.query_regex, g_regex_unref);
    g_free(browser.folded_query);
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    if (!accepted && preprocess_error)
        msgwin_debugger_report(preprocess_error, include_directory, NULL);
    g_free(include_directory);
    g_free(preprocess_error);
    return accepted;
}

/* EN: Reopen saved port settings from button for the running simulation in data.
 * RU: Повторно открывает настройки портов по кнопке button для сеанса из data. */
static void simulation_port_settings(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    gchar *path = g_strdup(run->port_settings);
    simulation_edit_ports(GTK_WINDOW(run->dialog), path, run, NULL, NULL, NULL);
    g_free(path);
}
