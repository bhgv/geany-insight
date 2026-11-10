/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Process and frame commands, register and frame value editing.
 * RU: Команды процессов и кадров, редактирование регистров и значений кадров.
 */

/* EN: Send the simulator command stored on button to simulation data.
 * RU: Отправляет команду симулятора из button в сеанс data. */
static void simulation_send_command(GtkButton *button, gpointer data)
{
    const gchar *command = g_object_get_data(G_OBJECT(button), "vvp-command");
    simulation_write_command(data, command);
}

/* EN: Read the requested process in simulation data and send its selection command. button is unused.
 * RU: Читает выбранный процесс из сеанса data и отправляет команду выбора. button не используется. */
static void simulation_select_process(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    const gchar *number = gtk_entry_get_text(GTK_ENTRY(run->process_number));
    guint64 process;
    if (!g_ascii_string_to_unsigned(number, 10, 1, G_MAXUINT64, &process, NULL))
        return;

    gchar *command = g_strdup_printf("geany-select-process %" G_GUINT64_FORMAT "\n", process);
    simulation_write_command(run, command);
    g_free(command);
}

/* EN: Return whether name is safe to pass as a single simulator identifier.
 * RU: Проверяет, допустимо ли передать name как один идентификатор симулятора. */
static gboolean simulation_simple_name(const gchar *name)
{
    return (g_ascii_isalpha(*name) || *name == '_' || *name == '$') &&
           strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_$") ==
               strlen(name);
}

/* EN: Send the selected frame depth for simulation data. button is unused.
 * RU: Отправляет глубину выбранного кадра сеанса data. button не используется. */
static void simulation_select_frame(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    gint depth = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(run->frame_depth));
    gchar *command = g_strdup_printf("geany-select-frame %d\n", depth);
    simulation_write_command(run, command);
    g_free(command);
}

/* EN: Read the named memory word in simulation data when button is clicked.
 * RU: Запрашивает указанный элемент памяти сеанса data при нажатии button. */
static void simulation_read_memory_word(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(run->memory_word));
    const gchar *opening = strchr(text, '[');
    const gchar *closing = opening ? strchr(opening + 1, ']') : NULL;
    if (!closing || closing[1])
        return;

    gchar *name = g_strndup(text, opening - text);
    gchar *index_text = g_strndup(opening + 1, closing - opening - 1);
    gint64 index;
    if (simulation_simple_name(name) &&
        g_ascii_string_to_signed(index_text, 10, G_MININT, G_MAXINT, &index, NULL))
    {
        gchar *command = g_strdup_printf("geany-read-memory %s %" G_GINT64_FORMAT "\n", name, index);
        simulation_write_command(run, command);
        g_free(command);
    }
    g_free(index_text);
    g_free(name);
}

/* EN: Validate a dotted HDL hierarchy before passing it as a command argument.
 * RU: Проверяет иерархический путь name перед передачей в команду. */
static gboolean simulation_signal_path(const gchar *name)
{
    gchar **parts = g_strsplit(name, ".", -1);
    gboolean valid = *name != 0;
    for (guint i = 0; valid && parts[i]; i++)
        valid = simulation_simple_name(parts[i]);
    g_strfreev(parts);
    return valid;
}

/* EN: Open the dependency graph for the full path entered in simulation data.
 * button is unused. No simulator command is sent.
 * RU: Открывает граф по полному пути из сеанса data. button не используется.
 * Команда симулятору не отправляется. */
static void simulation_watch_graph(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    const gchar *name = gtk_entry_get_text(GTK_ENTRY(run->signal_name));
    if (!run->navigate)
        return;
    if (!strchr(name, '.') || !simulation_signal_path(name))
    {
        ui_set_statusbar(TRUE, "%s",
                         insight_tr("Enter a full HDL signal path, for example tb.dut.count."));
        return;
    }
    run->navigate(name, TRUE, NULL);
}

/* EN: Convert text in the selected format to an exact-width four-state value.
 * RU: Преобразует text в формате format в значение ширины width. */
static gchar *signal_edit_bits(const gchar *text, gsize width, guint format)
{
    gchar *value = g_ascii_strdown(text, -1);
    g_strstrip(value);
    GString *bits = g_string_new(NULL);
    const gchar *digits = value;
    guint base = 2;
    const gchar *quote = strchr(value, '\'');
    if (quote && quote != value)
    {
        gchar *end;
        guint64 stated = g_ascii_strtoull(value, &end, 10);
        if (end != quote || stated != width || !quote[1])
            goto invalid;
        base = quote[1] == 'b' ? 2 : quote[1] == 'h' ? 16 : quote[1] == 'd' ? 10 : 0;
        digits = quote + 2;
    }
    else if (value[0] == '\'' && strlen(value) == 3 && value[2] == '\'')
    {
        guint byte = (guchar)text[strchr(text, '\'') - text + 1];
        for (gint i = 7; i >= 0; i--)
            g_string_append_c(bits, byte & (1u << i) ? '1' : '0');
        goto finish;
    }
    else if (g_str_has_prefix(value, "0x"))
    {
        base = 16;
        digits += 2;
    }
    else if (g_str_has_prefix(value, "0b"))
        digits += 2;
    else if (format == 4 || strspn(value, "01xz") != strlen(value))
        base = 10;

    if (!*digits || !base)
        goto invalid;
    if (base == 10)
    {
        guint64 number;
        if (!g_ascii_string_to_unsigned(digits, 10, 0, G_MAXUINT64, &number, NULL))
            goto invalid;
        for (gint bit = 63; bit >= 0; bit--)
            g_string_append_c(bits, number & (G_GUINT64_CONSTANT(1) << bit) ? '1' : '0');
    }
    else
    {
        for (const gchar *ch = digits; *ch; ch++)
        {
            if (base == 2)
            {
                if (!strchr("01xz", *ch))
                    goto invalid;
                g_string_append_c(bits, *ch);
            }
            else if (*ch == 'x' || *ch == 'z')
                for (guint j = 0; j < 4; j++)
                    g_string_append_c(bits, *ch);
            else
            {
                gint digit = g_ascii_xdigit_value(*ch);
                if (digit < 0)
                    goto invalid;
                for (gint j = 3; j >= 0; j--)
                    g_string_append_c(bits, digit & (1 << j) ? '1' : '0');
            }
        }
    }

finish:
    while (bits->len > width && bits->str[0] == '0')
        g_string_erase(bits, 0, 1);
    if (bits->len > width)
        goto invalid;
    if (bits->len < width)
    {
        gchar *zeros = g_strnfill(width - bits->len, '0');
        g_string_prepend(bits, zeros);
        g_free(zeros);
    }
    g_free(value);
    return g_string_free(bits, FALSE);

invalid:
    g_free(value);
    g_string_free(bits, TRUE);
    return NULL;
}

/* EN: Commit text in the register row identified by path for simulation data.
 * renderer provides the display format but is otherwise unused.
 * RU: Записывает text в регистр строки path сеанса data. renderer не используется. */
static void signal_register_edited(GtkCellRendererText *renderer, gchar *path, gchar *text, gpointer data)
{
    (void)renderer;
    WaveSimulation *run = data;
    GtkTreeModel *model = GTK_TREE_MODEL(run->signal_tables[2]);
    GtkTreeIter row;
    if (!run->register_write_ready || !gtk_tree_model_get_iter_from_string(model, &row, path))
        return;

    gchar *name = NULL, *old = NULL;
    gtk_tree_model_get(model, &row, 0, &name, 2, &old, -1);
    gchar *bits = *old ? signal_edit_bits(text, strlen(old), run->signal_format) : NULL;
    if (!bits || !simulation_signal_path(name))
        gtk_label_set_text(GTK_LABEL(run->signal_status),
                           insight_tr("Invalid register value or unsupported register path."));
    else if (strcmp(bits, old))
    {
        gchar *command = g_strdup_printf("geany-write-register %s %s\n", name, bits);
        simulation_write_command(run, command);
        g_free(command);
    }
    g_free(name);
    g_free(old);
    g_free(bits);
}

/* EN: Restore the original text in entry when event is Ctrl+Z. data is unused.
 * RU: Восстанавливает исходный текст entry, если event соответствует Ctrl+Z. data не используется. */
static gboolean signal_edit_undo(GtkWidget *entry, GdkEventKey *event, gpointer data)
{
    (void)data;
    if (!(event->state & GDK_CONTROL_MASK) || gdk_keyval_to_lower(event->keyval) != GDK_KEY_z)
        return FALSE;
    gtk_entry_set_text(GTK_ENTRY(entry), g_object_get_data(G_OBJECT(entry), "signal-initial-text"));
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    return TRUE;
}

/* EN: Mark editable complete on Enter, Escape or focus exit. data is unused.
 * RU: Отмечает завершение editable по Enter, Escape или выходу. data не используется. */
static void signal_edit_completed(GtkCellEditable *editable, gpointer data)
{
    (void)data;
    g_object_set_data(G_OBJECT(editable), "signal-edit-complete", GINT_TO_POINTER(1));
}

/* EN: Commit entry when it loses focus. event and data are unused.
 * RU: Записывает значение entry при потере фокуса. event и data не используются. */
static gboolean signal_edit_focus_out(GtkWidget *entry, GdkEventFocus *event, gpointer data)
{
    (void)event;
    (void)data;
    if (!g_object_get_data(G_OBJECT(entry), "signal-edit-complete"))
    {
        g_object_ref(entry);
        g_object_set_data(G_OBJECT(entry), "signal-edit-complete", GINT_TO_POINTER(1));
        gtk_cell_editable_editing_done(GTK_CELL_EDITABLE(entry));
        gtk_cell_editable_remove_widget(GTK_CELL_EDITABLE(entry));
        g_object_unref(entry);
    }
    return FALSE;
}

/* EN: Save the original text and replace renderer's default focus-out cancel
 * with a commit in editable. path and data are unused.
 * RU: Сохраняет исходный текст editable и заменяет отмену renderer при потере
 * фокуса на запись. path и data не используются. */
static void signal_edit_started(GtkCellRenderer *renderer, GtkCellEditable *editable, const gchar *path,
                                gpointer data)
{
    (void)path;
    (void)data;
    if (!GTK_IS_ENTRY(editable))
        return;
    GtkEntry *entry = GTK_ENTRY(editable);
    gtk_widget_set_name(GTK_WIDGET(entry), "insight-verilog-register-edit");
    gtk_entry_set_max_length(entry, 4200);
    g_object_set_data_full(G_OBJECT(entry), "signal-initial-text", g_strdup(gtk_entry_get_text(entry)),
                           g_free);
    g_signal_connect(entry, "key-press-event", G_CALLBACK(signal_edit_undo), NULL);
    guint focus_out = g_signal_lookup("focus-out-event", GTK_TYPE_WIDGET);
    g_signal_handlers_block_matched(entry, G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_DATA, focus_out, 0, NULL,
                                    NULL, renderer);
    g_signal_connect(entry, "focus-out-event", G_CALLBACK(signal_edit_focus_out), NULL);
    g_signal_connect(entry, "editing-done", G_CALLBACK(signal_edit_completed), NULL);
}

/* EN: Write text to the value at path in WaveSimulation data. renderer is unused.
 * RU: Записывает text в значение по path сеанса WaveSimulation из data. renderer не используется. */
static void simulation_frame_edited(GtkCellRendererText *renderer, gchar *path, gchar *text,
                                    gpointer data)
{
    (void)renderer;
    WaveSimulation *run = data;
    GtkTreeModel *model = GTK_TREE_MODEL(run->frame_values);
    GtkTreeIter row;
    if (!run->frame_ready || run->frame_receiving ||
        !gtk_tree_model_get_iter_from_string(model, &row, path))
        return;

    gchar *old;
    gint offset;
    gboolean editable;
    gtk_tree_model_get(model, &row, FRAME_VALUE, &old, FRAME_EDITABLE, &editable,
                       FRAME_OFFSET, &offset, -1);
    gchar *bits = editable && old ? signal_edit_bits(text, strlen(old), 0) : NULL;
    if (!bits)
        gtk_label_set_text(GTK_LABEL(run->frame_status), insight_tr("Invalid frame value."));
    else if (strcmp(bits, old))
    {
        GtkTreeIter storage = row;
        gint storage_offset = offset;
        while (storage_offset >= 0)
        {
            GtkTreeIter parent;
            if (!gtk_tree_model_iter_parent(model, &parent, &storage))
                break;
            storage = parent;
            gtk_tree_model_get(model, &storage, FRAME_OFFSET, &storage_offset, -1);
        }

        gchar *name, *index, *full;
        gtk_tree_model_get(model, &storage, FRAME_NAME, &name, FRAME_INDEX, &index,
                           FRAME_VALUE, &full, -1);
        gsize width = strlen(bits), total = full ? strlen(full) : 0;
        gsize start = offset < 0 ? 0 : (gsize)offset;
        if (storage_offset < 0 && name && index && !strpbrk(name, " \t\r\n") &&
            start <= total && width <= total - start)
        {
            memcpy(full + total - start - width, bits, width);
            GtkTreePath *edited = gtk_tree_model_get_path(model, &row);
            g_clear_pointer(&run->frame_reopen_path, gtk_tree_path_free);
            if (gtk_tree_path_get_depth(edited) > 1 && gtk_tree_path_up(edited))
                run->frame_reopen_path = g_steal_pointer(&edited);
            g_clear_pointer(&edited, gtk_tree_path_free);
            run->signal_refresh_needed = TRUE;
            run->signal_history_replace = TRUE;
            gchar *command = g_strdup_printf("geany-frame-write %s %s %s\n", name, index, full);
            simulation_write_command(run, command);
            g_free(command);
        }
        g_free(full);
        g_free(index);
        g_free(name);
    }
    g_free(bits);
    g_free(old);
}

/* EN: Build the expandable frame inspection page for run.
 * RU: Создаёт вкладку раскрываемых значений кадра сеанса run. */
static GtkWidget *simulation_frame_page(WaveSimulation *run)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    run->frame_status = gtk_label_new(insight_tr("Waiting for a paused HDL frame."));
    gtk_label_set_xalign(GTK_LABEL(run->frame_status), 0);
    gtk_box_pack_start(GTK_BOX(page), run->frame_status, FALSE, FALSE, 0);

    run->frame_values = gtk_tree_store_new(FRAME_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                           G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INT);
    run->frame_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(run->frame_values));
    g_object_unref(run->frame_values);
    gtk_widget_set_name(run->frame_view, "insight-verilog-frame-tree");
    GtkTreeView *view = GTK_TREE_VIEW(run->frame_view);
    gtk_tree_view_insert_column_with_attributes(view, -1, insight_tr("Name"),
                                                gtk_cell_renderer_text_new(), "text", FRAME_LABEL, NULL);
    GtkCellRenderer *value = gtk_cell_renderer_text_new();
    g_signal_connect(value, "editing-started", G_CALLBACK(signal_edit_started), run);
    g_signal_connect(value, "edited", G_CALLBACK(simulation_frame_edited), run);
    gtk_tree_view_insert_column_with_attributes(view, -1, insight_tr("Value"), value, "text", FRAME_VALUE,
                                                "editable", FRAME_EDITABLE, NULL);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), run->frame_view);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    return page;
}
