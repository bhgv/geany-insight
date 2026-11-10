/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Lua observer loading and simulation dialog shutdown.
 * RU: Загрузка наблюдателя Lua и завершение сеанса отладки.
 */

/* EN: Choose and load a Lua observer for simulation data. button triggers the chooser.
 * RU: Выбирает и загружает наблюдателя Lua для сеанса data. button открывает выбор. */
static void simulation_load_observer(GtkButton *button, gpointer data)
{
    WaveSimulation *run = data;
    gchar *filename = NULL;
    if (GTK_WIDGET(button) == run->reload_script)
        filename = g_strdup(run->observer_file);
    else
    {
        GtkWidget *parent = g_object_ref(run->dialog);
        GtkWidget *chooser = gtk_file_chooser_dialog_new(
            "Load Verilog Observer", GTK_WINDOW(parent), GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
            GTK_RESPONSE_CANCEL, "_Load", GTK_RESPONSE_ACCEPT, NULL);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(chooser), TRUE);
        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Lua scripts");
        gtk_file_filter_add_pattern(filter, "*.[lL][uU][aA]");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
        if (run->observer_file)
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), run->observer_file);
        filename = insight_waveforms_run_setup_dialog(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT
                       ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser))
                       : NULL;
        gtk_widget_destroy(chooser);
        gboolean active = g_object_get_data(G_OBJECT(parent), "insight-live-simulation") == run;
        g_object_unref(parent);
        if (!active)
        {
            g_free(filename);
            return;
        }
    }
    if (!filename)
        return;

    gchar *source = g_malloc(64 * 1024 + 1);
    GFile *file = g_file_new_for_path(filename);
    GFileInputStream *stream = g_file_read(file, NULL, NULL);
    gsize length = 0;
    gboolean loaded =
        stream &&
        g_input_stream_read_all(G_INPUT_STREAM(stream), source, 64 * 1024 + 1, &length, NULL, NULL) &&
        length > 0 && length <= 64 * 1024 && !memchr(source, 0, length) &&
        g_utf8_validate(source, (gssize)length, NULL);
    gchar error[1024] = "";
    if (loaded && !run->lua_observer)
        run->lua_observer = insight_lua_debug_new_observer();
    gboolean applied =
        loaded && run->lua_observer &&
        insight_lua_debug_set_observer(run->lua_observer, source, length, error, sizeof error);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gchar *message = g_utf8_make_valid(
        applied  ? "\nLua observer loaded.\n"
        : *error ? error
                 : "\nCannot load observer. Choose a UTF-8 Lua script of at most 64 KiB.\n",
        -1);
    gtk_text_buffer_insert(buffer, &end, message, -1);
    if (applied)
    {
        run->observer_log_offset = 0;
        g_free(run->observer_file);
        run->observer_file = g_strdup(filename);
        gtk_widget_set_sensitive(run->reload_script, TRUE);
        simulation_report_settings(run);
        simulation_observer_output(run);
    }
    g_free(message);
    g_clear_object(&stream);
    g_object_unref(file);
    g_free(source);
    g_free(filename);
}

/* EN: Disable the Lua observer for simulation data. button is unused.
 * RU: Отключает наблюдателя Lua сеанса data. button не используется. */
static void simulation_disable_observer(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    g_clear_pointer(&run->lua_observer, insight_lua_debug_free);
    run->observer_log_offset = 0;
    simulation_observer_output(run);
}

/* EN: Offer to restart run after the user reviews diagnostic. parent owns the dialog.
 * RU: Предлагает перезапустить run после просмотра diagnostic. parent владеет диалогом. */
static void simulation_offer_restart(WaveSimulation *run, GtkWindow *parent,
                                     const gchar *diagnostic)
{
    gboolean missing = strstr(diagnostic, "Unknown module type:") ||
                       strstr(diagnostic, "These modules were missing:");
    const gchar *title = missing ? insight_tr("The Verilog design has missing modules.")
                                 : insight_tr("The Verilog run ended with an error.");
    GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "%s", title);
    GString *details = g_string_new(NULL);
    if (missing)
    {
        GRegex *pattern = g_regex_new("Unknown module type:[ \t]*([A-Za-z_][A-Za-z0-9_$]*)", 0, 0, NULL);
        GMatchInfo *match;
        g_regex_match(pattern, diagnostic, 0, &match);
        GHashTable *names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        while (g_match_info_matches(match))
        {
            gchar *name = g_match_info_fetch(match, 1);
            if (g_hash_table_add(names, g_strdup(name)))
                g_string_append_printf(details, "%s%s", details->len ? ", " : "", name);
            g_free(name);
            g_match_info_next(match, NULL);
        }
        g_hash_table_unref(names);
        g_match_info_free(match);
        g_regex_unref(pattern);
        if (details->len)
            g_string_append(details, "\n\n");
        if (run->port_settings)
            g_string_append(details, insight_tr("Open Port Signals to configure a PLL or HDMI buffer replacement, or add a simulation source."));
    }
    if (!details->len)
        g_string_append(details, insight_tr("See Messages for details and source locations. Save any source corrections, then restart with the same sources."));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", details->str);
    g_string_free(details, TRUE);
    const gchar *retry = !run->port_settings ? insight_tr("_Restart")
                       : missing ? insight_tr("Configure Replacements and Restart…")
                                 : insight_tr("Review Setup and Restart…");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), insight_tr("_Close"), GTK_RESPONSE_CLOSE,
                           retry, GTK_RESPONSE_ACCEPT, NULL);
    g_object_ref_sink(dialog);
    gint response = insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    if (response == GTK_RESPONSE_ACCEPT)
    {
        gchar *bin = g_path_get_dirname(run->compiler);
        insight_waveforms_simulate(parent, bin, run->include_dir,
                                    (const gchar *const *)run->sources, run->interactive,
                                    run->navigate, run->show_context, run->open_location,
                                    run->settings_directory, run->project, run->port_settings != NULL);
        g_free(bin);
    }
}

/* EN: Finish the simulation task in result, display its capture or publish diagnostics.
 * object and data are unused.
 * RU: Завершает задачу из result, открывает осциллограмму или публикует ошибки.
 * object и data не используются. */
static void simulation_done(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object;
    (void)data;
    GTask *task = G_TASK(result);
    WaveSimulation *run = g_task_get_task_data(task);
    GtkWindow *parent = g_weak_ref_get(&run->parent);
    GError *error = NULL;
    Capture *capture = g_task_propagate_pointer(task, &error);
    if (run->refresh_source)
    {
        g_source_remove(run->refresh_source);
        run->refresh_source = 0;
    }
    gtk_widget_destroy(run->dialog);
    if (parent)
    {
        simulation_rasters_show_final(run, parent);
        simulation_memory_show(run, parent);
    }
    if (capture && parent)
        show_capture(parent, capture, NULL, run->navigate);
    else if (capture)
        capture_free(capture);
    else if (parent && run->capture_error && !error)
    {
        gchar *message = g_strdup_printf("%s %s",
            insight_tr("Simulation completed. The waveform could not be opened."),
            insight_tr(run->capture_error->message));
        msgwin_msg_add_string(COLOR_BLACK, -1, NULL, message);
        GtkWidget *notice = gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE, "%s", message);
        g_signal_connect_swapped(notice, "response", G_CALLBACK(gtk_widget_destroy), notice);
        gtk_widget_show(notice);
        g_free(message);
    }
    else if (parent && error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        msgwin_debugger_report(error->message, run->include_dir, NULL);
        simulation_offer_restart(run, parent, error->message);
    }
    g_clear_error(&error);
    g_clear_object(&parent);
}

/* EN: Hide dialog and cancel the worker held in data. response is unused.
 * RU: Скрывает dialog и отменяет задачу из data. response не используется. */
static void simulation_cancel(GtkDialog *dialog, gint response, gpointer data)
{
    (void)response;
    gtk_widget_hide(GTK_WIDGET(dialog));
    g_cancellable_cancel(data);
}

/* EN: Stop polling simulation data before GTK destroys dialog and its children.
 * RU: Прекращает опрос сеанса data до уничтожения dialog и дочерних виджетов. */
static void simulation_dialog_destroy(GtkWidget *dialog, gpointer data)
{
    (void)dialog;
    WaveSimulation *run = data;
    g_object_set_data(G_OBJECT(dialog), "insight-live-simulation", NULL);
    if (run->breakpoint_markers)
        g_ptr_array_set_size(run->breakpoint_markers, 0);
    if (run->refresh_source)
    {
        g_source_remove(run->refresh_source);
        run->refresh_source = 0;
    }
}
