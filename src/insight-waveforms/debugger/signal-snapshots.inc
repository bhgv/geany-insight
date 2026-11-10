/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Paused signal snapshots, value formats and history storage.
 * RU: Снимки сигналов на паузе, форматы значений и история.
 */

/* EN: Feed length bytes of simulator output to the protocol parser for run.
 * Incomplete records are retained for the next call.
 * RU: Передаёт length байт из bytes обработчику протокола сеанса run.
 * Неполные записи сохраняются до следующего вызова. */
/* EN: Format a four-state value in the representation selected by the user.
 * RU: Форматирует четырёхсостоянийные биты bits в представлении format. */
static gchar *signal_table_format(const gchar *bits, guint format)
{
    gsize width = strlen(bits);
    if (!width)
        return g_strdup("—");
    if (format == 2 || format == 3)
    {
        if (strpbrk(bits, "xz"))
            return g_strdup_printf("%" G_GSIZE_FORMAT "'b%s", width, bits);
        GString *hex = g_string_new(NULL);
        if (format == 2)
            g_string_append(hex, "0x");
        else
            g_string_append_printf(hex, "%" G_GSIZE_FORMAT "'h", width);
        for (gsize i = 0; i < width;)
        {
            guint digit = 0;
            gsize count = i == 0 && width % 4 ? width % 4 : 4;
            while (count--)
                digit = digit * 2 + (bits[i++] == '1');
            g_string_append_c(hex, "0123456789abcdef"[digit]);
        }
        return g_string_free(hex, FALSE);
    }
    if ((format == 4 || format == 5) && width <= 64 && !strpbrk(bits, "xz"))
    {
        guint64 value = g_ascii_strtoull(bits, NULL, 2);
        if (format == 4)
            return g_strdup_printf("%" G_GUINT64_FORMAT, value);
        if (value >= 32 && value < 127 && value != '\'' && value != '\\')
            return g_strdup_printf("'%c'", (gchar)value);
    }
    if (format == 1 || format == 4 || format == 5)
        return g_strdup_printf("%" G_GSIZE_FORMAT "'b%s", width, bits);
    return g_strdup(bits);
}

/* EN: Identify a snapshot row by its table kind and fully qualified name.
 * RU: Формирует ключ строки row из типа таблицы и полного имени сигнала. */
static gchar *signal_snapshot_key(gchar **row)
{
    return g_strconcat(row[0], "\t", row[1], NULL);
}

/* EN: Free the stop snapshot in data and its owned values.
 * RU: Освобождает снимок остановки data и его значения. */
static void signal_history_stop_free(gpointer data)
{
    SignalHistoryStop *stop = data;
    g_hash_table_unref(stop->values);
    g_free(stop->location);
    g_free(stop);
}

/* EN: Keep at most ten stop snapshots for run. A register deposit updates
 * the current stop rather than adding another stop.
 * RU: Хранит до десяти снимков сеанса run. Запись регистра обновляет текущую
 * остановку вместо добавления новой. */
static void signal_history_record(WaveSimulation *run)
{
    SignalHistoryStop *stop = g_queue_peek_head(&run->signal_history);
    if (stop && run->signal_history_replace)
        g_hash_table_remove_all(stop->values);
    else
    {
        stop = g_new0(SignalHistoryStop, 1);
        stop->number = ++run->signal_stop_number;
        stop->location =
            run->trace_file ? g_strdup_printf("%s:%u", run->trace_file, run->trace_line) : g_strdup("—");
        stop->values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        g_queue_push_head(&run->signal_history, stop);
        if (g_queue_get_length(&run->signal_history) > 10)
            signal_history_stop_free(g_queue_pop_tail(&run->signal_history));
    }
    run->signal_history_replace = FALSE;
    stop->tick = run->signal_snapshot_tick;
    stop->has_tick = run->signal_snapshot_has_tick;
    for (guint i = 0; i < run->signal_rows->len; i++)
    {
        gchar **row = run->signal_rows->pdata[i];
        g_hash_table_insert(stop->values, signal_snapshot_key(row), g_strdup(row[3]));
    }
}

/* EN: Insert a signal in table for run and restore a matching selection.
 * row contains kind, name, source and raw bits. display and changed are visible.
 * selected_key identifies the old selection and context_name its source preview.
 * RU: Добавляет row в table сеанса run. display и changed видны в таблице.
 * selected_key определяет прежний выбор, context_name — контекст исходника. */
static gboolean signal_table_insert(WaveSimulation *run, guint table, gchar **row, const gchar *display,
                                    gboolean changed, const gchar *selected_key,
                                    const gchar *context_name)
{
    GtkTreeIter inserted;
    gtk_list_store_insert_with_values(run->signal_tables[table], &inserted, -1, 0, row[1], 1, display, 2,
                                      row[3], 3, changed ? "●" : "", 4, row[0], -1);
    if (selected_key && selected_key[0] == row[0][0] && !strcmp(selected_key + 2, row[1]))
    {
        gtk_tree_selection_select_iter(run->signal_selection[table], &inserted);
        return !g_strcmp0(context_name, row[1]);
    }
    return FALSE;
}

/* EN: Insert a signal below the scopes in its VPI name.
 * RU: Добавляет сигнал в дерево областей по его имени VPI. */
static void signal_instance_insert(GtkTreeStore *store, GHashTable *scopes, const gchar *name,
                                   const gchar *value, gboolean changed)
{
    GtkTreeIter *parent = NULL;
    const gchar *part = name;
    const gchar *dot;
    while ((dot = strchr(part, '.')))
    {
        gchar *path = g_strndup(name, dot - name);
        GtkTreeIter *scope = g_hash_table_lookup(scopes, path);
        if (!scope)
        {
            gchar *label = g_strndup(part, dot - part);
            scope = g_new(GtkTreeIter, 1);
            gtk_tree_store_insert_with_values(store, scope, parent, -1, 0, label, 3, path, -1);
            g_hash_table_insert(scopes, g_steal_pointer(&path), scope);
            g_free(label);
        }
        g_free(path);
        parent = scope;
        part = dot + 1;
    }
    GtkTreeIter leaf;
    gtk_tree_store_insert_with_values(store, &leaf, parent, -1, 0, part, 1, value,
                                      2, changed ? insight_tr("Yes") : "", -1);
}

/* EN: Save an expanded scope path from view in the set passed as data.
 * RU: Сохраняет путь раскрытой области view во множестве data. */
static void signal_instance_save_expansion(GtkTreeView *view, GtkTreePath *path, gpointer data)
{
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter iter;
    gchar *scope = NULL;
    if (gtk_tree_model_get_iter(model, &iter, path))
        gtk_tree_model_get(model, &iter, 3, &scope, -1);
    if (scope)
        g_hash_table_add(data, scope);
}

/* EN: Refill the visible models across all scopes for the regex query in run.
 * RU: Заполняет видимые таблицы сеанса run по всем областям с учётом регулярного выражения. */
static void signal_tables_refresh(WaveSimulation *run)
{
    if (!run->signal_tables[0])
        return;
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(run->signal_search));
    GError *error = NULL;
    GRegex *regex = *query ? g_regex_new(query, G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, &error) : NULL;
    if (error)
    {
        gtk_label_set_text(GTK_LABEL(run->signal_status), error->message);
        g_error_free(error);
        return;
    }
    gchar *selected[G_N_ELEMENTS(run->signal_tables)] = {NULL};
    const gchar *context_name = g_object_get_data(G_OBJECT(run->signal_context), "hdl-context-name");
    gboolean keep_context = FALSE;
    run->signal_refilling = TRUE;
    for (guint i = 0; i < G_N_ELEMENTS(run->signal_tables); i++)
    {
        GtkTreeModel *model;
        GtkTreeIter row;
        if (gtk_tree_selection_get_selected(run->signal_selection[i], &model, &row))
        {
            gchar *name, *kind;
            gtk_tree_model_get(model, &row, 0, &name, 4, &kind, -1);
            selected[i] = g_strconcat(kind, "\t", name, NULL);
            g_free(name);
            g_free(kind);
        }
        gtk_list_store_clear(run->signal_tables[i]);
    }
    GHashTable *expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gtk_tree_view_map_expanded_rows(run->signal_instance_view, signal_instance_save_expansion, expanded);
    gtk_tree_store_clear(run->signal_instances);
    GHashTable *scopes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gboolean changed_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(run->signal_changed_only));
    GHashTable *present = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; i < run->signal_rows->len; i++)
    {
        gchar **row = run->signal_rows->pdata[i];
        gchar *key = signal_snapshot_key(row);
        gboolean pinned = g_hash_table_contains(run->signal_pins, key);
        if (pinned)
            g_hash_table_add(present, g_strdup(key));
        const gchar *previous = g_hash_table_lookup(run->signal_previous_values, key);
        gboolean changed = previous && *previous && *row[3] && strcmp(previous, row[3]);
        g_free(key);
        if (changed_only && !changed)
            continue;
        gchar *display = signal_table_format(row[3], run->signal_format);
        if (!regex || g_regex_match(regex, row[1], 0, NULL) || g_regex_match(regex, display, 0, NULL) ||
            g_regex_match(regex, row[3], 0, NULL))
        {
            guint table = *row[0] == 'P' ? 0 : *row[0] == 'N' ? 1 : 2;
            keep_context |=
                signal_table_insert(run, table, row, display, changed, selected[table], context_name);
            signal_instance_insert(run->signal_instances, scopes, row[1], display, changed);
            if (pinned)
                keep_context |=
                    signal_table_insert(run, 3, row, display, changed, selected[3], context_name);
        }
        g_free(display);
    }
    if (!changed_only)
    {
        GHashTableIter pins;
        gpointer key;
        g_hash_table_iter_init(&pins, run->signal_pins);
        while (g_hash_table_iter_next(&pins, &key, NULL))
        {
            gchar *entry = key;
            if (g_hash_table_contains(present, entry) ||
                (regex && !g_regex_match(regex, entry + 2, 0, NULL)))
                continue;
            gchar kind[] = {entry[0], 0};
            gchar *row[] = {kind, entry + 2, NULL, ""};
            keep_context |= signal_table_insert(run, 3, row, "—", FALSE, selected[3], context_name);
        }
    }
    GHashTableIter expansions;
    gpointer scope_name;
    g_hash_table_iter_init(&expansions, expanded);
    while (g_hash_table_iter_next(&expansions, &scope_name, NULL))
    {
        GtkTreeIter *scope = g_hash_table_lookup(scopes, scope_name);
        if (scope)
        {
            GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(run->signal_instances), scope);
            gtk_tree_view_expand_to_path(run->signal_instance_view, path);
            gtk_tree_path_free(path);
        }
    }
    g_hash_table_unref(expanded);
    g_hash_table_unref(scopes);
    g_hash_table_unref(present);
    run->signal_refilling = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(selected); i++)
        g_free(selected[i]);
    if (!keep_context)
    {
        g_object_set_data(G_OBJECT(run->signal_context), "hdl-context-name", NULL);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->signal_context)), "", -1);
    }
    g_clear_pointer(&regex, g_regex_unref);
    gtk_label_set_text(GTK_LABEL(run->signal_status),
                       run->signal_limited ? insight_tr("Signal snapshot limit reached.") : "");
}

/* EN: Collect protocol line text for run. Return true for signal-table records.
 * RU: Принимает строку text для сеанса run. Возвращает TRUE для записей таблиц. */
static gboolean signal_tables_record(WaveSimulation *run, const gchar *text)
{
    if (!run->signal_rows)
        return FALSE;
    if (!strcmp(text, "GEANY-VVP-SIGNAL-TABLES"))
    {
        run->signal_tables_ready = TRUE;
        return TRUE;
    }
    const gchar *start = "GEANY-SIGNALS-BEGIN";
    if (!strcmp(text, start) || g_str_has_prefix(text, "GEANY-SIGNALS-BEGIN\t"))
    {
        const gchar *tick = text + strlen(start);
        run->signal_snapshot_has_tick =
            *tick == '\t' &&
            g_ascii_string_to_unsigned(tick + 1, 10, 0, G_MAXUINT64, &run->signal_snapshot_tick, NULL);
        run->signal_collecting = TRUE;
        g_hash_table_remove_all(run->signal_previous_values);
        for (guint i = 0; i < run->signal_rows->len; i++)
        {
            gchar **row = run->signal_rows->pdata[i];
            g_hash_table_insert(run->signal_previous_values, signal_snapshot_key(row), g_strdup(row[3]));
        }
        g_ptr_array_set_size(run->signal_rows, 0);
        return TRUE;
    }
    if (run->signal_collecting && g_str_has_prefix(text, "GEANY-SIGNAL\t"))
    {
        gchar **row = g_strsplit(text + strlen("GEANY-SIGNAL\t"), "\t", 4);
        if (g_strv_length(row) == 4 && strlen(row[0]) == 1 && strchr("PNR", *row[0]) &&
            run->signal_rows->len < 65536 && g_utf8_validate(row[1], -1, NULL) &&
            !strpbrk(row[3], "\t\r\n") && strspn(row[3], "01xz") == strlen(row[3]))
        {
            gchar *file = insight_canonicalize_filename(row[2], run->include_dir);
            g_free(row[2]);
            row[2] = file;
            g_ptr_array_add(run->signal_rows, row);
        }
        else
            g_strfreev(row);
        return TRUE;
    }
    if (run->signal_collecting && g_str_has_prefix(text, "GEANY-SIGNALS-END\t"))
    {
        run->signal_collecting = FALSE;
        run->signal_snapshot_received = TRUE;
        run->signal_limited = g_str_has_suffix(text, "\tlimited");
        signal_history_record(run);
        signal_tables_refresh(run);
        return TRUE;
    }
    return FALSE;
}

enum
{
    STOP_PATH,
    STOP_CONDITION,
    STOP_MASK,
    STOP_HITS,
    STOP_STATE,
    STOP_MODE,
    STOP_ENABLED,
    STOP_COLUMNS
};

static const gchar *const stop_modes[] = {"change", "rise", "fall", "unknown", "mask"};
static const gchar *const stop_labels[] = {"Any value change", "Rising edge (0 to 1)",
                                           "Falling edge (1 to 0)", "A bit changes to X or Z",
                                           "Value starts matching a mask"};

static gboolean simulation_signal_path(const gchar *name);
