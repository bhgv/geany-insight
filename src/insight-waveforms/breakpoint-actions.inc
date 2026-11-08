/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Commands and editing actions for source breakpoints.
 * RU: Команды и редактирование точек останова в исходниках.
 */

/* EN: Send command to run at a ready prompt and update the pending debugger state.
 * RU: Отправляет command в run при готовом приглашении и обновляет состояние отладчика. */
static void simulation_write_command(WaveSimulation *run, const gchar *command)
{
    if (run->preview_pending || run->prompt_tail[0] != '>' || run->prompt_tail[1] != ' ')
        return;
    g_mutex_lock(&run->lock);
    GSubprocess *process = run->live_process ? g_object_ref(run->live_process) : NULL;
    g_mutex_unlock(&run->lock);
    if (!process)
        return;

    run->prompt_tail[0] = run->prompt_tail[1] = 0;
    if (!strcmp(command, "cont\n") || !strcmp(command, "step\n") ||
        g_str_has_prefix(command, "geany-step-") || g_str_has_prefix(command, "geany-select-process ") ||
        g_str_has_prefix(command, "geany-select-frame "))
    {
        run->frame_refresh = TRUE;
        g_clear_pointer(&run->frame_reopen_path, gtk_tree_path_free);
        if (run->frame_values)
            gtk_tree_store_clear(run->frame_values);
    }
    if (g_str_has_prefix(command, "geany-write-register "))
    {
        run->signal_refresh_needed = TRUE;
        run->signal_history_replace = TRUE;
        run->frame_refresh = TRUE;
    }
    if (!strcmp(command, "cont\n") || !strcmp(command, "step\n") ||
        g_str_has_prefix(command, "geany-step-"))
    {
        run->signal_refresh_needed = TRUE;
        run->signal_history_replace = FALSE;
        run->signal_manual_stop = FALSE;
        if (run->signal_stop_hit)
        {
            GtkTreeIter row;
            if (signal_stop_find(run, run->signal_stop_hit, &row))
                gtk_list_store_set(run->signal_stop_store, &row, STOP_STATE,
                                   insight_tr("Armed signal stop"), -1);
            g_clear_pointer(&run->signal_stop_hit, g_free);
        }
        g_clear_pointer(&run->trace_file, g_free);
        gtk_widget_set_sensitive(run->source_button, FALSE);
    }
    gtk_widget_set_sensitive(run->live_controls, FALSE);
    GOutputStream *input = g_subprocess_get_stdin_pipe(process);
    g_output_stream_write_all(input, command, strlen(command), NULL, NULL, NULL);
    g_object_unref(process);
}

/* EN: Show a configuration save failure in run's console.
 * RU: Показывает ошибку сохранения настроек в консоли run. */
static void simulation_report_settings(WaveSimulation *run)
{
    if (!simulation_save_settings(run))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, "\nCould not save Verilog debug settings.\n", -1);
    }
}

/* EN: Return the list position of breakpoint key in run, or -1.
 * RU: Возвращает позицию точки key в списке run либо -1. */
static gint simulation_unresolved_index(const WaveSimulation *run, const gchar *key)
{
    gsize length = strlen(key);
    for (guint i = 0; i < run->unresolved_records->len; i++)
    {
        const gchar *record = run->unresolved_records->pdata[i];
        if (!strncmp(record, key, length) && record[length] == '\t')
            return (gint)i;
    }
    return -1;
}

/* EN: Show unresolved breakpoint rules from simulation data when button is activated.
 * RU: Показывает непривязанные точки останова из data по нажатию button. */
static void simulation_list_unresolved(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    GString *text = g_string_new("\n");
    g_string_append_printf(text, "Unresolved source points: %u\n", run->unresolved_records->len);
    for (guint i = 0; i < run->unresolved_records->len; i++)
    {
        gchar **parts = g_strsplit(run->unresolved_records->pdata[i], "\t", 7);
        g_string_append_printf(text, "  %s:%s\n", parts[1], parts[0]);
        g_strfreev(parts);
    }
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gchar *valid = g_utf8_make_valid(text->str, -1);
    gtk_text_buffer_insert(buffer, &end, valid, -1);
    g_free(valid);
    g_string_free(text, TRUE);
}

/* EN: Apply the breakpoint action stored on button to simulation data.
 * RU: Применяет действие с точкой останова из button к сеансу data. */
static void simulation_edit_breakpoint(GtkButton *button, gpointer data)
{
    WaveSimulation *run = data;
    if (!run->bridge_ready || run->prompt_tail[0] != '>' || run->prompt_tail[1] != ' ')
        return;

    gint action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "point-action"));
    gint index = gtk_combo_box_get_active(GTK_COMBO_BOX(run->breakpoint_file));
    gint line = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(run->breakpoint_line));
    const gchar *filename = index >= 0 ? run->sources[index] : NULL;
    if (action != 2 && (!filename || strpbrk(filename, "\r\n\t")))
        return;

    gchar *key = action == 2 ? NULL : g_strdup_printf("%d\t%s", line, filename);
    gchar *rule = NULL;
    if (action == 0)
    {
        const gchar *signal = gtk_entry_get_text(GTK_ENTRY(run->rule_signal));
        gchar *bits = g_ascii_strdown(gtk_entry_get_text(GTK_ENTRY(run->rule_bits)), -1);
        if (((*signal == '\0') != (*bits == '\0')) || strpbrk(signal, "\r\n\t ") ||
            strspn(bits, "01xz") != strlen(bits) ||
            (g_hash_table_size(run->breakpoints) + run->unresolved_records->len >= 128 &&
             !g_hash_table_contains(run->breakpoints, key) && simulation_unresolved_index(run, key) < 0))
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(
                buffer, &end,
                "\nEnter a full signal name and all bits, or leave both empty. Limit: 128 points.\n", -1);
            g_free(bits);
            g_free(key);
            return;
        }
        gint hits = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(run->rule_hits));
        gboolean log_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(run->rule_log));
        gboolean scripted = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(run->rule_script));
        if (log_only && scripted)
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(buffer, &end, "\nChoose either Log and continue or Lua decides.\n",
                                   -1);
            g_free(bits);
            g_free(key);
            return;
        }
        rule = g_strdup_printf("%s\t%s\t%d\t%d\t%d", signal, bits, hits, log_only, scripted);
        g_free(bits);
    }

    gchar *directory = simulation_live_directory(run);
    if (directory)
    {
        GString *contents = g_string_new(NULL);
        if (action != 2)
        {
            GHashTableIter iter;
            gpointer old_key, old_rule;
            g_hash_table_iter_init(&iter, run->breakpoints);
            while (g_hash_table_iter_next(&iter, &old_key, &old_rule))
            {
                if (strcmp(old_key, key))
                    g_string_append_printf(contents, "%s\t%s\n", (gchar *)old_key, (gchar *)old_rule);
            }
            if (rule)
                g_string_append_printf(contents, "%s\t%s\n", key, rule);
        }
        gchar *path = g_build_filename(directory, ".geany-vvp-breakpoints", NULL);
        if (g_file_set_contents(path, contents->str, (gssize)contents->len, NULL))
        {
            if (action == 2)
            {
                g_hash_table_remove_all(run->breakpoints);
                g_hash_table_remove_all(run->point_anchors);
                g_ptr_array_set_size(run->unresolved_records, 0);
                g_ptr_array_set_size(run->unresolved_anchors, 0);
            }
            else
            {
                gint unresolved;
                while ((unresolved = simulation_unresolved_index(run, key)) >= 0)
                {
                    g_ptr_array_remove_index(run->unresolved_records, (guint)unresolved);
                    g_ptr_array_remove_index(run->unresolved_anchors, (guint)unresolved);
                }
                if (action == 1)
                {
                    g_hash_table_remove(run->breakpoints, key);
                    g_hash_table_remove(run->point_anchors, key);
                }
                else
                {
                    WavePointSource *source = simulation_point_source(filename);
                    gchar *anchor = simulation_point_anchor(source, (guint)line);
                    if (source)
                        simulation_point_source_free(source);
                    g_hash_table_replace(run->point_anchors, g_strdup(key), anchor);
                    g_hash_table_replace(run->breakpoints, g_steal_pointer(&key), g_steal_pointer(&rule));
                }
            }
            simulation_report_settings(run);
            simulation_write_command(run, "geany-load\n");
        }
        g_free(path);
        g_string_free(contents, TRUE);
        g_free(directory);
    }
    g_free(key);
    g_free(rule);
}
