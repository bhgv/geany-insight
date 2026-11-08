/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Interactive console updates, source navigation and Lua observer events.
 * RU: Обновление консоли, переходы по исходникам и события наблюдателя Lua.
 */

/* EN: Open the source location of simulation data when button is pressed.
 * RU: Открывает позицию в исходнике сеанса data по нажатию button. */
static void simulation_open_location(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    if (run->trace_file && run->open_location)
        run->open_location(run->trace_file, run->trace_line);
}

/* EN: Return a locked snapshot of run's live directory as an owned path.
 * RU: Возвращает копию каталога run, прочитанную под блокировкой. */
static gchar *simulation_live_directory(WaveSimulation *run)
{
    g_mutex_lock(&run->lock);
    gchar *directory = run->live_process ? g_strdup(run->live_directory) : NULL;
    g_mutex_unlock(&run->lock);
    return directory;
}

/* EN: Request a simulator pause for WaveSimulation data from button.
 * RU: Запрашивает паузу WaveSimulation из data по нажатию button. */
static void simulation_request_pause(GtkButton *button, gpointer data)
{
    WaveSimulation *run = data;
    if (!run->bridge_ready || run->pause_requested)
        return;
    gchar *directory = simulation_live_directory(run);
    if (!directory)
        return;
    gchar *path = g_build_filename(directory, ".geany-vvp-pause", NULL);
    run->pause_requested = g_file_set_contents(path, "pause\n", -1, NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(button), !run->pause_requested);
    g_free(path);
    g_free(directory);
}

static void simulation_write_command(WaveSimulation *run, const gchar *command);

/* EN: Flush the Lua observer output of run to its debugger widgets.
 * RU: Переносит вывод наблюдателя Lua сеанса run в виджеты отладчика. */
static void simulation_observer_output(WaveSimulation *run)
{
    const gchar *log = insight_lua_debug_observer_log(run->lua_observer);
    gsize length = strlen(log);
    if (length > run->observer_log_offset)
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gchar *message = g_utf8_make_valid(log + run->observer_log_offset, -1);
        gtk_text_buffer_insert(buffer, &end, message, -1);
        g_free(message);
    }
    run->observer_log_offset = length;

    InsightLuaWatchGraph *graph = g_new0(InsightLuaWatchGraph, 1);
    insight_lua_debug_observer_watch(run->lua_observer, graph);
    GString *text = g_string_new(NULL);
    for (unsigned i = 1; i < graph->node_count; i++)
    {
        const InsightLuaWatchNode *node = &graph->nodes[i];
        if (text->len)
            g_string_append_c(text, '\n');
        g_string_append_printf(text, "%s = %s", node->label, node->value);
    }
    gchar *valid = g_utf8_make_valid(text->str, -1);
    gtk_label_set_text(GTK_LABEL(run->lua_watches), valid);
    g_free(valid);
    g_string_free(text, TRUE);
    g_free(graph);
}

/* EN: Dispatch the pending simulator event in run to its Lua observer.
 * RU: Передаёт ожидающее событие симуляции run наблюдателю Lua. */
static void simulation_handle_observer_event(WaveSimulation *run)
{
    gchar **fields = g_steal_pointer(&run->pending_event);
    if (!fields)
        return;
    guint64 line, tick, hits;
    if (insight_lua_debug_observer_active(run->lua_observer) && g_strv_length(fields) == 7 &&
        g_ascii_string_to_unsigned(fields[1], 10, 1, G_MAXINT, &line, NULL) &&
        g_ascii_string_to_unsigned(fields[3], 10, 0, G_MAXUINT64, &tick, NULL) &&
        g_ascii_string_to_unsigned(fields[4], 10, 1, G_MAXUINT, &hits, NULL))
    {
        InsightObserverValue values[] = {
            {.scope = 0, .type = 's', .name = "tick", .text = fields[3], .length = strlen(fields[3])},
            {.scope = 0, .type = 'i', .name = "hits", .integer = (long long)hits},
            {.scope = 0, .type = 's', .name = "signal", .text = fields[5], .length = strlen(fields[5])},
            {.scope = 0, .type = 's', .name = "bits", .text = fields[6], .length = strlen(fields[6])},
            {.scope = 0,
             .type = 'i',
             .name = "ticks",
             .integer = tick <= G_MAXINT64 ? (long long)tick : 0}};
        InsightObserverEvent event = {.kind = "hdl-point",
                                      .file = fields[2],
                                      .name = fields[5],
                                      .line = (unsigned long)line,
                                      .values = values,
                                      .count = tick <= G_MAXINT64 ? G_N_ELEMENTS(values)
                                                                  : G_N_ELEMENTS(values) - 1};
        gint decision = insight_lua_debug_observe(run->lua_observer, &event);
        simulation_observer_output(run);
        if (decision == 0 && !run->pause_requested)
            simulation_write_command(run, "cont\n");
    }
    g_strfreev(fields);
}

/* EN: Hide internal records in bytes from the console for run, including
 * partial prefixes across chunks of length count.
 * RU: Скрывает записи протокола сеанса run из bytes длиной count, включая
 * префиксы, разделённые между чтениями. */
static GString *simulation_scope_console(WaveSimulation *run, const gchar *bytes, gsize count)
{
    const gchar *prefixes[] = {"GEANY-SCOPE\t", "GEANY-PORT\t",   "GEANY-SIGNAL\t",
                               "GEANY-FRAME-",  "GEANY-SIGNALS-", "GEANY-SIGNAL-STOP-"};
    GString *visible = g_string_sized_new(count);
    for (gsize i = 0; i < count; i++)
    {
        gchar ch = bytes[i];
        if (run->scope_console_prefix < 0)
        {
            if (run->scope_console_prefix == -1)
                g_string_append_c(visible, ch);
            if (ch == '\n')
                run->scope_console_prefix = 0;
            continue;
        }
        gint length = run->scope_console_prefix++;
        run->console_prefix[length] = ch;
        gboolean candidate = FALSE;
        for (guint j = 0; j < G_N_ELEMENTS(prefixes); j++)
        {
            if (strncmp(prefixes[j], run->console_prefix, length + 1))
                continue;
            candidate = TRUE;
            if ((gsize)length + 1 == strlen(prefixes[j]))
                run->scope_console_prefix = -2;
        }
        if (!candidate)
        {
            g_string_append_len(visible, run->console_prefix, length + 1);
            run->scope_console_prefix = ch == '\n' ? 0 : -1;
        }
    }
    return visible;
}

/* EN: Compute bounds low and high from scope samples starting at ring index start. Return false if no sample is known.
 * RU: Считает границы low и high по отсчётам scope с индекса start. Возвращает false, если все отсчёты неизвестны. */
static gboolean simulation_scope_bounds(const LiveScope *scope, guint start, guint64 *low, guint64 *high)
{
    gboolean known = FALSE;
    for (guint axis = 0; axis < scope->axes; axis++)
    {
        low[axis] = G_MAXUINT64;
        high[axis] = 0;
    }
    for (guint i = 0; i < scope->count; i++)
    {
        const InstrumentPoint *point = &scope->samples[(start + i) % LIVE_SCOPE_POINTS].point;
        if (!point->valid)
            continue;
        known = TRUE;
        for (guint axis = 0; axis < scope->axes; axis++)
        {
            low[axis] = MIN(low[axis], point->values[axis]);
            high[axis] = MAX(high[axis], point->values[axis]);
        }
    }
    return known;
}

/* EN: Refresh the labels and drawings of dirty live scopes in run.
 * RU: Обновляет подписи и графики изменившихся осциллографов run. */
static void simulation_scope_refresh(WaveSimulation *run)
{
    for (guint i = 0; run->live_scopes && i < run->live_scopes->len; i++)
    {
        LiveScope *scope = run->live_scopes->pdata[i];
        if (!scope->dirty)
            continue;
        scope->dirty = FALSE;
        guint first = (scope->next + LIVE_SCOPE_POINTS - scope->count) % LIVE_SCOPE_POINTS;
        guint last = (scope->next + LIVE_SCOPE_POINTS - 1) % LIVE_SCOPE_POINTS;
        GString *status = g_string_new(NULL);
        g_string_append_printf(status, "%u samples · %" G_GUINT64_FORMAT "–%" G_GUINT64_FORMAT " ns",
                               scope->count, scope->samples[first].time, scope->samples[last].time);
        guint64 low[3], high[3];
        if (simulation_scope_bounds(scope, first, low, high))
            for (guint axis = 0; axis < scope->axes; axis++)
                g_string_append_printf(status, " · %c %" G_GUINT64_FORMAT "–%" G_GUINT64_FORMAT,
                                       scope->axes == 1 ? 'Y' : "XYZ"[axis], low[axis], high[axis]);
        else
            g_string_append(status, " · no known samples");
        gtk_label_set_text(GTK_LABEL(scope->status), status->str);
        g_string_free(status, TRUE);
        gtk_widget_queue_draw(scope->drawing);
    }
}

/* EN: Drain queued bytes for WaveSimulation data and refresh controls on the GTK thread.
 * RU: Забирает вывод WaveSimulation из data и обновляет интерфейс в потоке GTK. */
static gboolean simulation_refresh_console(gpointer data)
{
    WaveSimulation *run = data;
    g_mutex_lock(&run->lock);
    gboolean running = run->live_process != NULL;
    GByteArray *output = run->live_output;
    run->live_output = g_byte_array_new();
    g_cond_signal(&run->live_output_space);
    g_mutex_unlock(&run->lock);
    simulation_rasters_refresh(run);

    const gchar *bytes = (const gchar *)output->data;
    gssize count = output->len;
    if (count > 0)
    {
        simulation_read_records(run, bytes, (gsize)count);
        simulation_scope_refresh(run);
        run->prompt_tail[0] = count > 1 ? bytes[count - 2] : run->prompt_tail[1];
        run->prompt_tail[1] = bytes[count - 1];
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
        gint excess = gtk_text_buffer_get_char_count(buffer) - 65536;
        GtkTextIter end;
        if (excess > 0)
        {
            GtkTextIter first, limit;
            gtk_text_buffer_get_start_iter(buffer, &first);
            gtk_text_buffer_get_iter_at_offset(buffer, &limit, excess);
            gtk_text_buffer_delete(buffer, &first, &limit);
        }
        GString *visible = simulation_scope_console(run, bytes, (gsize)count);
        gchar *valid =
            visible ? g_utf8_make_valid(visible->str, visible->len) : g_utf8_make_valid(bytes, count);
        if (visible)
            g_string_free(visible, TRUE);
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, valid, -1);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(run->console), &end, 0, FALSE, 0, 0);
        g_free(valid);
    }
    g_byte_array_unref(output);
    gboolean paused = running && run->prompt_tail[0] == '>' && run->prompt_tail[1] == ' ';
    if (paused && run->bridge_ready && !run->restored_rules)
    {
        run->restored_rules = TRUE;
        if (g_hash_table_size(run->breakpoints))
        {
            simulation_write_command(run, "geany-load\n");
            paused = FALSE;
        }
    }
    if (paused && run->signal_stops_ready && !run->preview_pending)
    {
        run->signal_stop_restoring = FALSE;
        run->signal_stop_disabling = FALSE;
        gchar *restore = g_queue_pop_head(&run->signal_stop_restore);
        if (restore)
        {
            run->signal_stop_restoring = TRUE;
            simulation_write_command(run, restore);
            g_free(restore);
            paused = FALSE;
        }
    }
    if (paused && run->pending_event)
    {
        simulation_handle_observer_event(run);
        paused = run->prompt_tail[0] == '>' && run->prompt_tail[1] == ' ';
    }
    signal_tables_follow_file(run);
    if (paused && !run->preview_pending && run->signal_tables_ready && run->signal_refresh_needed)
    {
        run->signal_refresh_needed = FALSE;
        simulation_write_command(run, "geany-signals\n");
        paused = FALSE;
    }
    if (paused && !run->preview_pending && run->frame_ready && run->frame_refresh)
    {
        run->frame_refresh = FALSE;
        simulation_write_command(run, "geany-frame-tree\n");
        paused = FALSE;
    }
    if (run->frame_view)
        gtk_widget_set_sensitive(run->frame_view, paused && !run->frame_receiving);
    const gchar *signal_state = !run->signal_snapshot_received ? "Waiting for a paused signal snapshot."
                                : paused                       ? "Values at the current stop"
                                                               : "Values from the last stop";
    const gchar *state_text = insight_tr(signal_state);
    if (strcmp(gtk_label_get_text(GTK_LABEL(run->signal_state)), state_text))
        gtk_label_set_text(GTK_LABEL(run->signal_state), state_text);
    g_object_set(run->signal_register_renderer, "editable",
                 paused && !run->preview_pending && run->register_write_ready, NULL);
    gboolean rule_selected = paused && !run->preview_pending && run->signal_stops_ready &&
                             gtk_tree_selection_count_selected_rows(run->signal_stop_selection) != 0;
    gtk_widget_set_sensitive(run->signal_stop_remove, rule_selected);
    gtk_widget_set_sensitive(run->signal_stop_edit, rule_selected);
    gtk_widget_set_sensitive(run->signal_stop_toggle, rule_selected);
    gchar *reason = run->signal_stop_hit
                        ? g_strdup_printf("%s: %s", insight_tr("Signal stop hit"), run->signal_stop_hit)
                        : g_strdup(insight_tr(run->signal_manual_stop ? "Paused by user"
                                              : run->trace_file       ? "Paused at HDL source"
                                              : paused                ? "Simulator paused"
                                                                      : ""));
    if (strcmp(gtk_label_get_text(GTK_LABEL(run->pause_reason)), reason))
        gtk_label_set_text(GTK_LABEL(run->pause_reason), reason);
    g_free(reason);
    gtk_widget_set_sensitive(run->live_controls, paused && !run->preview_pending);
    gtk_widget_set_sensitive(run->breakpoint_controls, run->bridge_ready);
    gtk_widget_set_sensitive(run->source_step_controls, run->source_step_ready);
    gtk_widget_set_sensitive(run->process_controls, run->processes_ready);
    gtk_widget_set_sensitive(run->frame_controls, run->frames_ready);
    if (paused && run->pause_requested)
    {
        run->signal_manual_stop = TRUE;
        gchar *directory = simulation_live_directory(run);
        if (directory)
        {
            gchar *marker = g_build_filename(directory, ".geany-vvp-pause", NULL);
            g_unlink(marker);
            g_free(marker);
            g_free(directory);
        }
        run->pause_requested = FALSE;
    }
    gtk_widget_set_sensitive(run->pause_button,
                             running && run->bridge_ready && !paused && !run->pause_requested);
    if (run->live_port_send)
        gtk_widget_set_sensitive(run->live_port_send, running);
    if (run->live_devices)
        gtk_widget_set_sensitive(run->live_devices, running);
    if (run->live_port_samples && count > 0 && g_hash_table_size(run->live_port_samples))
    {
        GList *keys = g_list_sort(g_hash_table_get_keys(run->live_port_samples), (GCompareFunc)g_strcmp0);
        GString *readings = g_string_new(NULL);
        for (GList *key = keys; key; key = key->next)
        {
            const gchar *bits = g_hash_table_lookup(run->live_port_samples, key->data);
            const gchar *lamp = bits[1] ? "" : bits[0] == '1' ? "●  " : bits[0] == '0' ? "○  " : "◉  ";
            guint64 number;
            g_string_append_printf(readings, "%s%s = %s", lamp, (const gchar *)key->data, bits);
            if (bits[1] && instrument_number(bits, &number))
                g_string_append_printf(readings, "  (%" G_GUINT64_FORMAT ")", number);
            g_string_append_c(readings, '\n');
        }
        gtk_label_set_text(GTK_LABEL(run->live_port_display), readings->str);
        g_string_free(readings, TRUE);
        g_list_free(keys);
    }
    gtk_widget_set_sensitive(run->source_button, paused && run->trace_file != NULL);
    return G_SOURCE_CONTINUE;
}
