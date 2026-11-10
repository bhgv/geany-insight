/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Signal stop rules and interactive bridge record decoding.
 * RU: Условия остановки по сигналам и разбор записей интерактивного моста.
 */

/* EN: Find mode in the supported signal stop modes, or return -1.
 * RU: Возвращает индекс режима mode или -1 для неизвестного режима. */
static gint signal_stop_mode(const gchar *mode)
{
    for (guint i = 0; i < G_N_ELEMENTS(stop_modes); i++)
        if (!g_strcmp0(mode, stop_modes[i]))
            return i;
    return -1;
}

/* EN: Write the desired signal rules for run to the project settings file.
 * RU: Записывает правила остановки сеанса run в настройки проекта. */
static void signal_stop_save(WaveSimulation *run)
{
    if (!run->signal_stop_path)
        return;
    GtkTreeModel *model = GTK_TREE_MODEL(run->signal_stop_store);
    GtkTreeIter row;
    GPtrArray *rules = g_ptr_array_new_with_free_func(g_free);
    for (gboolean more = gtk_tree_model_get_iter_first(model, &row); more;
         more = gtk_tree_model_iter_next(model, &row))
    {
        gchar *path, *mode, *mask;
        gboolean enabled;
        gtk_tree_model_get(model, &row, STOP_PATH, &path, STOP_MODE, &mode, STOP_MASK, &mask,
                           STOP_ENABLED, &enabled, -1);
        g_ptr_array_add(rules, g_strdup_printf("%s\t%s\t%s\t%d", path, mode, mask, enabled));
        g_free(path);
        g_free(mode);
        g_free(mask);
    }
    GKeyFile *config = g_key_file_new();
    g_ptr_array_add(rules, NULL);
    g_key_file_set_string_list(config, "signals", "stops", (const gchar *const *)rules->pdata,
                               rules->len - 1);
    gchar *directory = g_path_get_dirname(run->signal_stop_path);
    GError *error = NULL;
    if (g_mkdir_with_parents(directory, 0700) ||
        !g_key_file_save_to_file(config, run->signal_stop_path, &error))
        ui_set_statusbar(TRUE, "%s", error ? error->message : insight_tr("Could not save signal stops."));
    g_clear_error(&error);
    g_free(directory);
    g_key_file_unref(config);
    g_ptr_array_unref(rules);
}

/* EN: Load project rules for run and mark enabled rows for restoration.
 * RU: Загружает правила проекта для run и помечает включённые для восстановления. */
static void signal_stop_load(WaveSimulation *run)
{
    GStatBuf file_info;
    if (!run->signal_stop_path || g_stat(run->signal_stop_path, &file_info) ||
        file_info.st_size > 512 * 1024)
        return;
    GKeyFile *config = g_key_file_new();
    if (g_key_file_load_from_file(config, run->signal_stop_path, G_KEY_FILE_NONE, NULL))
    {
        gsize count;
        gchar **rules = g_key_file_get_string_list(config, "signals", "stops", &count, NULL);
        for (gsize i = 0; rules && i < count && i < 64; i++)
        {
            gchar **fields = g_strsplit(rules[i], "\t", 5);
            gint mode = g_strv_length(fields) == 4 ? signal_stop_mode(fields[1]) : -1;
            if (mode >= 0 && strlen(fields[0]) <= 1024 && simulation_signal_path(fields[0]) &&
                strlen(fields[2]) <= 4096 && strspn(fields[2], "01xz?") == strlen(fields[2]) &&
                (mode != 4 || (*fields[2] && strspn(fields[2], "?") != strlen(fields[2]))) &&
                (!strcmp(fields[3], "0") || !strcmp(fields[3], "1")))
            {
                gboolean enabled = *fields[3] == '1';
                gtk_list_store_insert_with_values(
                    run->signal_stop_store, NULL, -1, STOP_PATH, fields[0], STOP_CONDITION,
                    insight_tr(stop_labels[mode]), STOP_MASK, fields[2], STOP_MODE, fields[1],
                    STOP_ENABLED, enabled, STOP_STATE,
                    insight_tr(enabled ? "Pending signal stop" : "Disabled signal stop"), -1);
                if (enabled)
                    g_queue_push_tail(&run->signal_stop_restore,
                                      g_strdup_printf("geany-watch-signal %s %s %s\n", fields[0],
                                                      fields[1], fields[2]));
            }
            g_strfreev(fields);
        }
        g_strfreev(rules);
    }
    g_key_file_unref(config);
}

/* EN: Find the active signal stop for path in run, returning its row.
 * RU: Ищет остановку path в сеансе run и возвращает её строку row. */
static gboolean signal_stop_find(WaveSimulation *run, const gchar *path, GtkTreeIter *row)
{
    GtkTreeModel *model = GTK_TREE_MODEL(run->signal_stop_store);
    for (gboolean more = gtk_tree_model_get_iter_first(model, row); more;
         more = gtk_tree_model_iter_next(model, row))
    {
        gchar *name;
        gtk_tree_model_get(model, row, 0, &name, -1);
        gboolean match = !strcmp(name, path);
        g_free(name);
        if (match)
            return TRUE;
    }
    return FALSE;
}

/* EN: Apply a complete simulator protocol line to the stop list for run.
 * RU: Применяет строку протокола text к списку остановок сеанса run. */
static void signal_stop_record(WaveSimulation *run, const gchar *text)
{
    const gchar *prefix = "GEANY-SIGNAL-STOP-";
    if (!run->signal_stop_store || !g_str_has_prefix(text, prefix))
        return;
    gchar **fields = g_strsplit(text + strlen(prefix), "\t", 4);
    guint count = g_strv_length(fields);
    if (count < 2 || !*fields[1] || !g_utf8_validate(fields[1], -1, NULL))
    {
        g_strfreev(fields);
        return;
    }

    GtkTreeIter row;
    gboolean found = signal_stop_find(run, fields[1], &row);
    if (count == 4 && !strcmp(fields[0], "ON"))
    {
        gint mode = signal_stop_mode(fields[2]);
        if (mode >= 0)
        {
            if (!found)
                gtk_list_store_append(run->signal_stop_store, &row);
            gtk_list_store_set(run->signal_stop_store, &row, STOP_PATH, fields[1], STOP_CONDITION,
                               insight_tr(stop_labels[mode]), STOP_MASK, fields[3], STOP_HITS, 0u,
                               STOP_STATE, insight_tr("Armed signal stop"), STOP_MODE, fields[2],
                               STOP_ENABLED, TRUE, -1);
            if (!run->signal_stop_restoring)
                signal_stop_save(run);
        }
    }
    else if (found && count == 2 && !strcmp(fields[0], "OFF"))
    {
        if (run->signal_stop_disabling)
            gtk_list_store_set(run->signal_stop_store, &row, STOP_ENABLED, FALSE, STOP_STATE,
                               insight_tr("Disabled signal stop"), -1);
        else
            gtk_list_store_remove(run->signal_stop_store, &row);
        signal_stop_save(run);
    }
    else if (found && count == 2 && !strcmp(fields[0], "MISSING"))
        gtk_list_store_set(run->signal_stop_store, &row, STOP_STATE, insight_tr("Signal not found"), -1);
    else if (found && count == 2 && !strcmp(fields[0], "REJECTED"))
    {
        gchar *state;
        gtk_tree_model_get(GTK_TREE_MODEL(run->signal_stop_store), &row, STOP_STATE, &state, -1);
        if (g_strcmp0(state, insight_tr("Armed signal stop")) &&
            g_strcmp0(state, insight_tr("Signal stop hit")))
            gtk_list_store_set(run->signal_stop_store, &row, STOP_STATE,
                               insight_tr("Signal rule unavailable"), -1);
        g_free(state);
    }
    else if (found && count == 2 && !strcmp(fields[0], "HIT"))
    {
        GtkTreeIter previous;
        if (run->signal_stop_hit && strcmp(run->signal_stop_hit, fields[1]) &&
            signal_stop_find(run, run->signal_stop_hit, &previous))
            gtk_list_store_set(run->signal_stop_store, &previous, STOP_STATE,
                               insight_tr("Armed signal stop"), -1);
        guint hits;
        gtk_tree_model_get(GTK_TREE_MODEL(run->signal_stop_store), &row, STOP_HITS, &hits, -1);
        gtk_list_store_set(run->signal_stop_store, &row, STOP_HITS, hits < G_MAXUINT ? hits + 1 : hits,
                           STOP_STATE, insight_tr("Signal stop hit"), -1);
        g_free(run->signal_stop_hit);
        run->signal_stop_hit = g_strdup(fields[1]);
    }
    g_strfreev(fields);
}

enum
{
    FRAME_LABEL,
    FRAME_VALUE,
    FRAME_NAME,
    FRAME_INDEX,
    FRAME_EDITABLE,
    FRAME_OFFSET,
    FRAME_COLUMNS
};

/* EN: Add a packed field record from text below the last value in run.
 * RU: Добавляет поле из записи text под последним значением сеанса run. */
static void simulation_frame_field_record(WaveSimulation *run, const gchar *text)
{
    gchar **fields = g_strsplit(text, "\t", 3);
    if (g_strv_length(fields) == 3)
    {
        guint depth = 0;
        const gchar *label = fields[0];
        for (const gchar *c = fields[0]; *c; c++)
            if (*c == '.')
            {
                depth++;
                label = c + 1;
            }
        guint64 offset;
        if (depth < run->frame_field_parent_count &&
            depth + 1 < G_N_ELEMENTS(run->frame_field_parents) && *label &&
            *fields[2] && strspn(fields[2], "01xz") == strlen(fields[2]) &&
            g_ascii_string_to_unsigned(fields[1], 10, 0, 4095, &offset, NULL))
        {
            gtk_tree_store_insert_with_values(run->frame_values, &run->frame_field_parents[depth + 1],
                &run->frame_field_parents[depth], -1, FRAME_LABEL, label, FRAME_VALUE, fields[2],
                FRAME_EDITABLE, run->frame_field_editable, FRAME_OFFSET, (gint)offset, -1);
            run->frame_field_parent_count = depth + 2;
        }
    }
    g_strfreev(fields);
}

/* EN: Populate the frame tree of run from one bridge record text.
 * RU: Заполняет дерево кадров сеанса run по одной записи text от симулятора. */
static void simulation_frame_record(WaveSimulation *run, const gchar *text)
{
    if (!run->frame_values)
        return;
    if (!strcmp(text, "GEANY-VVP-FRAME-TREE"))
    {
        run->frame_ready = TRUE;
        run->frame_refresh = TRUE;
    }
    else if (g_str_has_prefix(text, "GEANY-FRAME-BEGIN\t"))
    {
        gtk_tree_store_clear(run->frame_values);
        run->frame_receiving = TRUE;
        run->frame_has_group = FALSE;
        run->frame_field_parent_count = 0;
        gtk_label_set_text(GTK_LABEL(run->frame_status), text + strlen("GEANY-FRAME-BEGIN\t"));
    }
    else if (!strcmp(text, "GEANY-FRAME-UNAVAILABLE"))
    {
        gtk_tree_store_clear(run->frame_values);
        run->frame_receiving = FALSE;
        g_clear_pointer(&run->frame_reopen_path, gtk_tree_path_free);
        gtk_label_set_text(GTK_LABEL(run->frame_status),
                           insight_tr("Select a paused HDL process and frame."));
    }
    else if (g_str_has_prefix(text, "GEANY-FRAME-END\t"))
    {
        run->frame_receiving = FALSE;
        if (run->frame_reopen_path)
        {
            gtk_tree_view_expand_to_path(GTK_TREE_VIEW(run->frame_view), run->frame_reopen_path);
            gtk_tree_view_expand_row(GTK_TREE_VIEW(run->frame_view), run->frame_reopen_path, FALSE);
            g_clear_pointer(&run->frame_reopen_path, gtk_tree_path_free);
        }
        if (g_str_has_suffix(text, "\tlimited"))
            gtk_label_set_text(GTK_LABEL(run->frame_status),
                               insight_tr("Frame snapshot reached the display limit."));
    }
    else if (run->frame_receiving && g_str_has_prefix(text, "GEANY-FRAME-FIELD\t"))
        simulation_frame_field_record(run, text + strlen("GEANY-FRAME-FIELD\t"));
    else if (run->frame_receiving && g_str_has_prefix(text, "GEANY-FRAME-ROW\t"))
    {
        gchar **fields = g_strsplit(text + strlen("GEANY-FRAME-ROW\t"), "\t", 4);
        if (g_strv_length(fields) == 4)
        {
            gboolean root = !strcmp(fields[2], "-");
            gboolean group = !strcmp(fields[0], "G");
            GtkTreeIter row;
            gchar *label = root ? g_strdup(fields[1]) : g_strdup_printf("[%s]", fields[2]);
            gtk_tree_store_insert_with_values(
                run->frame_values, &row, !root && run->frame_has_group ? &run->frame_group : NULL, -1,
                FRAME_LABEL, label, FRAME_VALUE, fields[3], FRAME_NAME, fields[1], FRAME_INDEX, fields[2],
                FRAME_EDITABLE, !strcmp(fields[0], "W"), FRAME_OFFSET, -1, -1);
            run->frame_field_parents[0] = row;
            run->frame_field_parent_count = group ? 0 : 1;
            run->frame_field_editable = !strcmp(fields[0], "W");
            if (root)
            {
                run->frame_has_group = group;
                if (group)
                    run->frame_group = row;
            }
            g_free(label);
        }
        g_strfreev(fields);
    }
}

/* EN: Consume length bytes of simulator output, retaining partial lines in run.
 * RU: Принимает length байт вывода bytes и сохраняет неполные строки в run. */
static void simulation_read_records(WaveSimulation *run, const gchar *bytes, gsize length)
{
    for (gsize i = 0; i < length; i++)
    {
        if (bytes[i] != '\n')
        {
            if (run->protocol_line->len < 8192)
                g_string_append_c(run->protocol_line, bytes[i]);
            continue;
        }
        if (run->protocol_line->len && run->protocol_line->str[run->protocol_line->len - 1] == '\r')
            g_string_truncate(run->protocol_line, run->protocol_line->len - 1);
        const gchar *text = run->protocol_line->str;
        if (g_str_has_prefix(text, "> "))
            text += 2;
        if (g_str_has_prefix(text, "Waveform capture started at "))
            run->capture_started = TRUE;
        simulation_frame_record(run, text);
        signal_tables_record(run, text);
        signal_stop_record(run, text);
        if (!strcmp(text, "GEANY-VVP-SIGNAL-STOPS"))
            run->signal_stops_ready = TRUE;
        if (!strcmp(text, "GEANY-VVP-WRITE-REGISTER"))
            run->register_write_ready = TRUE;
        if (strstr(text, "GEANY-VVP-FRAMES"))
            run->frames_ready = TRUE;
        if (strstr(text, "GEANY-VVP-PROCESSES"))
            run->processes_ready = TRUE;
        if (strstr(text, "GEANY-VVP-SOURCE-STEP"))
            run->source_step_ready = TRUE;
        if (strstr(text, "GEANY-VVP-READY"))
            run->bridge_ready = TRUE;
        if (run->live_scopes && g_str_has_prefix(text, "GEANY-SCOPE\t"))
            simulation_scope_record(run, text + strlen("GEANY-SCOPE\t"));
        const gchar *port = strstr(text, "GEANY-PORT\t");
        if (run->live_port_samples && port)
        {
            gchar **fields = g_strsplit(port + strlen("GEANY-PORT\t"), "\t", 3);
            if (g_strv_length(fields) == 2 && simulation_port_name(fields[0]) && *fields[1] &&
                strlen(fields[1]) <= 64 && strspn(fields[1], "01xz") == strlen(fields[1]) &&
                (g_hash_table_size(run->live_port_samples) < 128 ||
                 g_hash_table_contains(run->live_port_samples, fields[0])))
            {
                const gchar *previous = g_hash_table_lookup(run->live_port_samples, fields[0]);
                if (g_strcmp0(previous, fields[1]))
                {
                    g_hash_table_replace(run->live_port_samples, g_strdup(fields[0]),
                                         g_strdup(fields[1]));
                    simulation_update_indicator(run, fields[0], fields[1]);
                }
            }
            g_strfreev(fields);
        }
        const gchar *uart = strstr(text, "GEANY-UART\t");
        if (run->uart_log && uart && strlen(uart) == strlen("GEANY-UART\t") + 2 &&
            g_ascii_isxdigit(uart[strlen("GEANY-UART\t")]) &&
            g_ascii_isxdigit(uart[strlen("GEANY-UART\t") + 1]))
        {
            guint byte = (guint)g_ascii_strtoull(uart + strlen("GEANY-UART\t"), NULL, 16);
            if (byte == '\n' || byte == '\r' || byte == '\t' || (byte >= 32 && byte < 127))
                g_string_append_c(run->uart_log, byte);
            else
                g_string_append_printf(run->uart_log, "\\x%02x", byte);
            if (run->uart_log->len > 65536)
                g_string_erase(run->uart_log, 0, 4096);
            if (run->uart_view)
            {
                GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->uart_view));
                gtk_text_buffer_set_text(buffer, run->uart_log->str, run->uart_log->len);
                GtkTextIter end;
                gtk_text_buffer_get_end_iter(buffer, &end);
                gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(run->uart_view), &end, 0, FALSE, 0, 0);
            }
        }
        const gchar *sent = strstr(text, "GEANY-UART-SENT\t");
        if (run->uart_log && sent &&
            g_ascii_strtoull(sent + strlen("GEANY-UART-SENT\t"), NULL, 10) == run->uart_send_sequence)
        {
            run->uart_send_sequence = 0;
            if (run->uart_entry)
                gtk_widget_set_sensitive(run->uart_entry, TRUE);
        }
        const gchar *event = strstr(text, "GEANY-VVP-EVENT\t");
        if (event)
        {
            g_strfreev(run->pending_event);
            run->pending_event = g_strsplit(event, "\t", 7);
        }
        gchar *filename = NULL;
        guint line;
        if (!run->breakpoint_navigation_pending && simulation_trace_location(text, &filename, &line))
        {
            g_free(run->trace_file);
            run->trace_file = insight_canonicalize_filename(filename, run->include_dir);
            g_free(filename);
            run->breakpoint_stop = strstr(text, "GEANY-VVP-BREAKPOINT\t") != NULL;
            run->breakpoint_navigation_pending = run->breakpoint_stop;
            run->trace_line = line;
        }
        g_string_truncate(run->protocol_line, 0);
    }
}
