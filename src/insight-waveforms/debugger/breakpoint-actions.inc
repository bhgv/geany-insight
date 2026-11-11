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
        g_ptr_array_set_size(run->breakpoint_hit_marker, 0);
        run->breakpoint_navigation_pending = FALSE;
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

typedef struct
{
    GWeakRef editor;
    gint handle;
} SimulationPointMarker;

/* EN: Remove the marker held in data if its editor still exists, then release it.
 * RU: Удаляет маркер из data, если редактор ещё открыт, и освобождает данные. */
static void simulation_point_marker_free(gpointer data)
{
    SimulationPointMarker *marker = data;
    GObject *editor = g_weak_ref_get(&marker->editor);
    if (editor)
    {
        SSM(SCINTILLA(editor), SCI_MARKERDELETEHANDLE, marker->handle, 0);
        g_object_unref(editor);
    }
    g_weak_ref_clear(&marker->editor);
    g_free(marker);
}

/* EN: Highlight the breakpoint line until execution resumes.
 * RU: Подсвечивает строку останова до возобновления выполнения. */
static void simulation_mark_breakpoint_hit(WaveSimulation *run)
{
    enum { SOURCE_HIT_MARKER = 19 };
    g_ptr_array_set_size(run->breakpoint_hit_marker, 0);
    gchar *utf8 = utils_get_utf8_from_locale(run->trace_file);
    GeanyDocument *doc = utf8 ? document_find_by_filename(utf8) : NULL;
    g_free(utf8);
    if (!DOC_VALID(doc) || run->trace_line > (guint)sci_get_line_count(doc->editor->sci))
        return;
    ScintillaObject *sci = doc->editor->sci;
    SSM(sci, SCI_MARKERDEFINE, SOURCE_HIT_MARKER, SC_MARK_BACKGROUND);
    SSM(sci, SCI_MARKERSETBACK, SOURCE_HIT_MARKER, 0x80dfff);
    gint handle = (gint)SSM(sci, SCI_MARKERADD, run->trace_line - 1, SOURCE_HIT_MARKER);
    if (handle >= 0)
    {
        SimulationPointMarker *marker = g_new0(SimulationPointMarker, 1);
        marker->handle = handle;
        g_weak_ref_init(&marker->editor, G_OBJECT(sci));
        g_ptr_array_add(run->breakpoint_hit_marker, marker);
    }
}

/* EN: Update run's visible list and red margin dots from its saved source rules.
 * RU: Обновляет список run и красные метки по сохранённым точкам останова. */
static void simulation_refresh_breakpoints(WaveSimulation *run)
{
    enum { SOURCE_POINT_MARKER = 18 };
    gtk_list_store_clear(run->breakpoint_rows);
    g_ptr_array_set_size(run->breakpoint_markers, 0);
    GList *keys = g_list_sort(g_hash_table_get_keys(run->breakpoints), (GCompareFunc)g_strcmp0);
    for (GList *it = keys; it; it = it->next)
    {
        const gchar *key = it->data;
        const gchar *filename = strchr(key, '\t');
        if (!filename)
            continue;
        filename++;
        guint line = (guint)g_ascii_strtoull(key, NULL, 10);
        GtkTreeIter row;
        gtk_list_store_insert_with_values(run->breakpoint_rows, &row, -1,
                                          0, filename, 1, line, -1);
        GeanyDocument *doc = document_find_by_filename(filename);
        if (!DOC_VALID(doc) || !line || line > (guint)sci_get_line_count(doc->editor->sci))
            continue;
        ScintillaObject *sci = doc->editor->sci;
        SSM(sci, SCI_MARKERDEFINE, SOURCE_POINT_MARKER, SC_MARK_CIRCLE);
        SSM(sci, SCI_MARKERSETFORE, SOURCE_POINT_MARKER, 0x2020df);
        SSM(sci, SCI_MARKERSETBACK, SOURCE_POINT_MARKER, 0x2020df);
        gint handle = (gint)SSM(sci, SCI_MARKERADD, line - 1, SOURCE_POINT_MARKER);
        if (handle >= 0)
        {
            SimulationPointMarker *marker = g_new0(SimulationPointMarker, 1);
            marker->handle = handle;
            g_weak_ref_init(&marker->editor, G_OBJECT(sci));
            g_ptr_array_add(run->breakpoint_markers, marker);
        }
    }
    g_list_free(keys);
}

/* EN: Open the source point at path in view for the simulation in data.
 * RU: Открывает исходник точки по path из view для симуляции из data. */
static void simulation_open_breakpoint(GtkTreeView *view, GtkTreePath *path,
                                       GtkTreeViewColumn *column, gpointer data)
{
    (void)column;
    WaveSimulation *run = data;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter row;
    if (run->open_location && gtk_tree_model_get_iter(model, &row, path))
    {
        gchar *filename = NULL;
        guint line = 0;
        gtk_tree_model_get(model, &row, 0, &filename, 1, &line, -1);
        run->open_location(filename, line);
        simulation_refresh_breakpoints(run);
        g_free(filename);
    }
}

/* EN: Apply action to filename and line in run. conditional enables the fields in the dialog.
 * RU: Применяет action к filename и line в run. conditional включает поля условия из окна. */
static void simulation_change_breakpoint(WaveSimulation *run, const gchar *filename, guint line,
                                         gint action, gboolean conditional)
{
    if (action != 2 && (!filename || strpbrk(filename, "\r\n\t")))
        return;

    gchar *key = action == 2 ? NULL : g_strdup_printf("%u\t%s", line, filename);
    gchar *rule = NULL;
    if (action == 0)
    {
        const gchar *signal = conditional ? gtk_entry_get_text(GTK_ENTRY(run->rule_signal)) : "";
        gchar *bits = g_ascii_strdown(conditional ? gtk_entry_get_text(GTK_ENTRY(run->rule_bits)) : "", -1);
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
            gtk_label_set_text(GTK_LABEL(run->breakpoint_status), insight_tr(
                "Enter a full signal name and binary value, or leave both empty. Limit: 128 points."));
            g_free(bits);
            g_free(key);
            return;
        }
        gint hits = conditional ? gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(run->rule_hits)) : 1;
        gboolean log_only = conditional && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(run->rule_log));
        gboolean scripted = conditional && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(run->rule_script));
        if (log_only && scripted)
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(buffer, &end, "\nChoose either Log and continue or Lua decides.\n",
                                   -1);
            gtk_label_set_text(GTK_LABEL(run->breakpoint_status),
                               insight_tr("Choose either Log and continue or Lua decides."));
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
            simulation_refresh_breakpoints(run);
            run->breakpoint_reload = TRUE;
            if (run->bridge_ready && !run->preview_pending &&
                run->prompt_tail[0] == '>' && run->prompt_tail[1] == ' ')
            {
                run->breakpoint_reload = FALSE;
                run->restored_rules = TRUE;
                simulation_write_command(run, "geany-load\n");
            }
            else if (run->bridge_ready)
            {
                gchar *reload = g_build_filename(directory, ".geany-vvp-reload", NULL);
                g_file_set_contents(reload, "reload\n", -1, NULL);
                g_free(reload);
            }
            const gchar *message = action == 2 ? "Source breakpoint list cleared."
                                   : action == 1 ? "Source breakpoint removed."
                                                 : "Source breakpoint added.";
            gtk_label_set_text(GTK_LABEL(run->breakpoint_status), insight_tr(message));
        }
        else
            gtk_label_set_text(GTK_LABEL(run->breakpoint_status),
                               insight_tr("Could not save the source breakpoint list."));
        g_free(path);
        g_string_free(contents, TRUE);
        g_free(directory);
    }
    else
        gtk_label_set_text(GTK_LABEL(run->breakpoint_status),
                           insight_tr("Wait for the simulator to start before changing breakpoints."));
    g_free(key);
    g_free(rule);
}

/* EN: Read the source selection and action from the breakpoint button and session data.
 * RU: Читает выбранный исходник из data и действие из кнопки button. */
static void simulation_edit_breakpoint(GtkButton *button, gpointer data)
{
    WaveSimulation *run = data;
    gtk_spin_button_update(GTK_SPIN_BUTTON(run->breakpoint_line));
    const gchar *filename = gtk_combo_box_get_active_id(GTK_COMBO_BOX(run->breakpoint_file));
    guint line = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(run->breakpoint_line));
    gint action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "point-action"));
    simulation_change_breakpoint(run, filename, line, action, TRUE);
}

/* EN: Toggle an unconditional HDL point at filename and one-based line in session data.
 * RU: Переключает безусловную точку на строке line файла filename в сеансе data. */
static void simulation_toggle_editor_point(const gchar *filename, guint line, gpointer data)
{
    WaveSimulation *run = data;
    gchar *selected = insight_canonicalize_filename(filename, NULL);
    const gchar *source = selected;
    for (guint i = 0; run->sources[i]; i++)
    {
        gchar *candidate = insight_canonicalize_filename(run->sources[i], NULL);
#ifdef G_OS_WIN32
        gboolean matches = !g_ascii_strcasecmp(candidate, selected);
#else
        gboolean matches = !strcmp(candidate, selected);
#endif
        g_free(candidate);
        if (matches)
        {
            source = run->sources[i];
            break;
        }
    }
    if (simulation_point_file_allowed(run, source))
    {
        GtkComboBox *files = GTK_COMBO_BOX(run->breakpoint_file);
        if (!gtk_combo_box_set_active_id(files, source))
        {
            gchar *name = g_filename_display_basename(source);
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(files), source, name);
            gtk_combo_box_set_active_id(files, source);
            g_free(name);
        }
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(run->breakpoint_line), line);
        gchar *key = g_strdup_printf("%u\t%s", line, source);
        gint action = g_hash_table_contains(run->breakpoints, key) ? 1 : 0;
        g_free(key);
        simulation_change_breakpoint(run, source, line, action, FALSE);
        ui_set_statusbar(FALSE, "%s", gtk_label_get_text(GTK_LABEL(run->breakpoint_status)));
    }
    else
        ui_set_statusbar(FALSE, "%s", insight_tr("This file is not in the active Verilog simulation."));
    g_free(selected);
}
