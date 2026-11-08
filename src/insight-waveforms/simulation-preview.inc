/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Waveform snapshots and preview requests during simulation.
 * RU: Снимки диаграмм и запросы предпросмотра во время симуляции.
 */

/* EN: Read the named signal or enter a scope in simulation data, depending on button's action.
 * RU: Читает сигнал или входит в область сеанса data по действию button. */
static void simulation_inspect_name(GtkButton *button, gpointer data)
{
    WaveSimulation *run = data;
    const gchar *name = gtk_entry_get_text(GTK_ENTRY(run->signal_name));
    if (!simulation_simple_name(name))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, "\nEnter one signal or scope name from List.\n", -1);
        return;
    }
    gboolean scope = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "scope"));
    gchar *command = g_strdup_printf(scope ? "push %s\n" : "$display \"%%b\" %s\n", name);
    simulation_write_command(run, command);
    g_free(command);
}

typedef struct
{
    gchar *directory, *signal;
    guint64 tick;
} WavePreviewRequest;

/* EN: Free the owned preview request in data.
 * RU: Освобождает запрос предпросмотра из data. */
static void wave_preview_request_free(gpointer data)
{
    WavePreviewRequest *request = data;
    g_free(request->directory);
    g_free(request->signal);
    g_free(request);
}

/* EN: Center the viewer window on tick and select the track named name when present.
 * RU: Центрирует окно window на такте tick и выбирает дорожку name, если она есть. */
static void wave_preview_position(GtkWidget *window, const gchar *name, guint64 tick)
{
    Viewer *viewer = g_object_get_data(G_OBJECT(window), "insight-wave-viewer");
    gboolean track = insight_waveforms_select_track(GTK_WINDOW(window), name);
    if (tick <= viewer->capture->end)
    {
        viewer->cursor = tick;
        guint64 total = MAX(G_GUINT64_CONSTANT(1), viewer->capture->end);
        guint64 half = viewer->span / 2;
        viewer->start = MIN(total - viewer->span, tick > half ? tick - half : 0);
        update(viewer);
    }
    else
        ui_set_statusbar(TRUE, "%s", insight_tr("The stop is beyond the recorded waveform."));
    if (!track && tick <= viewer->capture->end)
        ui_set_statusbar(TRUE, insight_tr("Signal has no track in this recording: %s"), name);
}

/* EN: Read the capture requested in data. task owns the result, cancel stops
 * the read, and object is unused.
 * RU: Читает запись из запроса data. task получает результат, cancel отменяет
 * чтение, object не используется. */
static void simulation_preview_worker(GTask *task, gpointer object, gpointer data, GCancellable *cancel)
{
    (void)object;
    GError *error = NULL;
    WavePreviewRequest *request = data;
    gchar *path = simulation_find_vcd(request->directory, &error);
    Capture *capture = path ? read_capture_file(path, cancel, &error) : NULL;
    g_free(path);
    if (capture)
        g_task_return_pointer(task, capture, capture_free);
    else
        g_task_return_error(task, error);
}

/* EN: Show result under the live simulation dialog object. data is unused.
 * RU: Показывает result над окном активного сеанса object. data не используется. */
static void simulation_preview_done(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)data;
    GtkWidget *dialog = GTK_WIDGET(object);
    WaveSimulation *run = g_object_get_data(object, "insight-live-simulation");
    GError *error = NULL;
    Capture *capture = g_task_propagate_pointer(G_TASK(result), &error);
    if (run)
    {
        run->preview_pending = FALSE;
        if (capture)
        {
            GtkWidget *preview = show_capture(GTK_WINDOW(dialog), capture, NULL, run->navigate);
            gtk_window_set_modal(GTK_WINDOW(preview), TRUE);
            WavePreviewRequest *request = g_task_get_task_data(G_TASK(result));
            if (request->signal)
                wave_preview_position(preview, request->signal, request->tick);
        }
        else
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            gchar *message = g_strdup_printf("\nWaveform preview: %s\n",
                                             error ? error->message : "No capture available.");
            gchar *valid = g_utf8_make_valid(message, -1);
            gtk_text_buffer_insert(buffer, &end, valid, -1);
            g_free(valid);
            g_free(message);
        }
    }
    else if (capture)
        capture_free(capture);
    g_clear_error(&error);
}

/* EN: Open run's paused capture, optionally targeting name at tick.
 * RU: Открывает запись остановленного сеанса run, при необходимости для name на tick. */
static void simulation_preview_at(WaveSimulation *run, const gchar *name, guint64 tick)
{
    if (run->preview_pending || run->prompt_tail[0] != '>' || run->prompt_tail[1] != ' ')
    {
        if (name)
            ui_set_statusbar(TRUE, "%s", insight_tr("Pause the simulation before opening the waveform."));
        return;
    }
    g_mutex_lock(&run->lock);
    gchar *directory = run->live_process ? g_strdup(run->live_directory) : NULL;
    g_mutex_unlock(&run->lock);
    if (!directory)
        return;
    run->preview_pending = TRUE;
    gtk_widget_set_sensitive(run->live_controls, FALSE);
    WavePreviewRequest *request = g_new0(WavePreviewRequest, 1);
    request->directory = directory;
    request->signal = g_strdup(name);
    request->tick = tick;
    GTask *task = g_task_new(run->dialog, NULL, simulation_preview_done, NULL);
    g_task_set_task_data(task, request, wave_preview_request_free);
    g_task_run_in_thread(task, simulation_preview_worker);
    g_object_unref(task);
}

/* EN: Open the paused capture for simulation data. button is unused.
 * RU: Открывает запись остановленного сеанса data. button не используется. */
static void simulation_preview(GtkButton *button, gpointer data)
{
    (void)button;
    simulation_preview_at(data, NULL, 0);
}

/* EN: Select a breakpoint rule in simulation data from widget.
 * RU: Выбирает точку останова сеанса data из widget. */
static void simulation_rule_selected(GtkWidget *widget, gpointer data)
{
    (void)widget;
    WaveSimulation *run = data;
    gint index = gtk_combo_box_get_active(GTK_COMBO_BOX(run->breakpoint_file));
    if (index < 0)
        return;
    gint line = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(run->breakpoint_line));
    gchar *key = g_strdup_printf("%d\t%s", line, run->sources[index]);
    const gchar *rule = g_hash_table_lookup(run->breakpoints, key);
    gint unresolved = rule ? -1 : simulation_unresolved_index(run, key);
    if (unresolved >= 0)
    {
        const gchar *record = run->unresolved_records->pdata[unresolved];
        rule = record + strlen(key) + 1;
    }
    gchar **fields = rule ? g_strsplit(rule, "\t", 5) : NULL;
    gboolean valid = fields && g_strv_length(fields) == 5;
    gtk_entry_set_text(GTK_ENTRY(run->rule_signal), valid ? fields[0] : "");
    gtk_entry_set_text(GTK_ENTRY(run->rule_bits), valid ? fields[1] : "");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(run->rule_hits),
                              valid ? g_ascii_strtoll(fields[2], NULL, 10) : 1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(run->rule_log), valid && !strcmp(fields[3], "1"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(run->rule_script), valid && !strcmp(fields[4], "1"));
    g_strfreev(fields);
    g_free(key);
}
