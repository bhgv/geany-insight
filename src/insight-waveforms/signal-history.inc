/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Signal navigation and per-stop value history dialog.
 * RU: Переходы по сигналам и просмотр истории значений на остановках.
 */

/* EN: Resolve the selected row from selection in simulation data for context.
 * RU: Показывает контекст строки selection из сеанса data. */
static void signal_table_selected(GtkTreeSelection *selection, gpointer data)
{
    WaveSimulation *run = data;
    GtkTreeModel *model;
    GtkTreeIter row;
    if (run->signal_refilling || !run->show_context ||
        !gtk_tree_selection_get_selected(selection, &model, &row))
        return;
    gchar *name;
    gtk_tree_model_get(model, &row, 0, &name, -1);
    run->show_context(name, GTK_TEXT_VIEW(run->signal_context));
    g_free(name);
}

/* EN: Navigate from the name column at path in tree for simulation data.
 * RU: Переходит к объявлению из столбца имени по path в tree для сеанса data. */
static void signal_table_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column,
                                   gpointer data)
{
    WaveSimulation *run = data;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter row;
    if (!run->navigate || column != gtk_tree_view_get_column(tree, 0) ||
        !gtk_tree_model_get_iter(model, &row, path))
        return;
    gchar *name;
    gtk_tree_model_get(model, &row, 0, &name, -1);
    run->navigate(name, FALSE, NULL);
    g_free(name);
}

/* EN: Apply the representation attached to menu item for simulation data.
 * RU: Применяет формат из пункта item к таблицам сеанса data. */
static void signal_table_format_selected(GtkMenuItem *item, gpointer data)
{
    WaveSimulation *run = data;
    run->signal_format = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "signal-format"));
    signal_tables_refresh(run);
}

/* EN: Open the dependency graph for the signal attached to item in session data.
 * RU: Открывает граф для сигнала из пункта item в сеансе data. */
static void signal_table_open_graph(GtkMenuItem *item, gpointer data)
{
    WaveSimulation *run = data;
    const gchar *name = g_object_get_data(G_OBJECT(item), "signal-name");
    if (run->navigate && name)
        run->navigate(name, TRUE, NULL);
}

/* EN: Convert known binary bits to unsigned decimal without a machine-word cap.
 * RU: Переводит известные биты bits в десятичную строку без ограничения разрядностью машины. */
static gchar *signal_history_decimal(const gchar *bits)
{
    if (!*bits || strpbrk(bits, "xz"))
        return g_strdup("—");

    guint8 *digits = g_new0(guint8, strlen(bits) * 31 / 100 + 2);
    gsize used = 1;
    for (const gchar *bit = bits; *bit; bit++)
    {
        guint carry = *bit == '1';
        for (gsize i = 0; i < used; i++)
        {
            guint digit = 2 * digits[i] + carry;
            digits[i] = digit % 10;
            carry = digit / 10;
        }
        if (carry)
            digits[used++] = carry;
    }
    gchar *decimal = g_malloc(used + 1);
    for (gsize i = 0; i < used; i++)
        decimal[i] = '0' + digits[used - i - 1];
    decimal[used] = 0;
    g_free(digits);
    return decimal;
}

/* EN: Render bits in hexadecimal, retaining all-X and all-Z groups.
 * RU: Показывает bits в шестнадцатеричном виде, сохраняя группы из X или Z. */
static gchar *signal_history_hex(const gchar *bits)
{
    gsize width = strlen(bits);
    if (!width)
        return g_strdup("—");

    GString *hex = g_string_sized_new((width + 3) / 4);
    for (gsize offset = 0; offset < width;)
    {
        gsize count = offset == 0 && width % 4 ? width % 4 : 4;
        guint value = 0, x = 0, z = 0;
        for (gsize i = 0; i < count; i++)
        {
            gchar bit = bits[offset++];
            value = value * 2 + (bit == '1');
            x += bit == 'x';
            z += bit == 'z';
        }
        g_string_append_c(hex, x == count   ? 'x'
                               : z == count ? 'z'
                               : x || z     ? '?'
                                            : "0123456789abcdef"[value]);
    }
    return g_string_free(hex, FALSE);
}

/* EN: Mark digits whose underlying bits differ from previous. group is one
 * for binary and four for hexadecimal. Store the changed bit count in changes.
 * RU: Выделяет разряды digits, биты которых отличаются от previous.
 * group равен 1 для двоичного и 4 для шестнадцатеричного вида.
 * Записывает число изменённых битов в changes. */
static gchar *signal_history_markup(const gchar *digits, const gchar *bits, const gchar *previous,
                                    guint group, guint *changes)
{
    *changes = 0;
    gsize width = strlen(bits);
    if (!width || !previous || strlen(previous) != width)
        return g_markup_escape_text(digits, -1);

    GString *markup = g_string_new(NULL);
    gsize offset = 0;
    for (gsize digit = 0; digits[digit]; digit++)
    {
        guint count = !digit && width % group ? width % group : group;
        guint difference = 0;
        for (guint bit = 0; bit < count; bit++, offset++)
            difference += bits[offset] != previous[offset];
        *changes += difference;
        if (difference)
            g_string_append_printf(markup, "<u>%c</u>", digits[digit]);
        else
            g_string_append_c(markup, digits[digit]);
    }
    return g_string_free(markup, FALSE);
}

/* EN: Stripe each model row in the cell of view column. data is the tree.
 * RU: Задаёт фон строки row модели model для ячейки cell. data — дерево,
 * column не используется. */
static void signal_history_stripe(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model,
                                  GtkTreeIter *row, gpointer data)
{
    (void)column;
    GtkTreePath *path = gtk_tree_model_get_path(model, row);
    gboolean alternate = gtk_tree_path_get_indices(path)[0] % 2;
    gtk_tree_path_free(path);

    GtkStyleContext *style = gtk_widget_get_style_context(GTK_WIDGET(data));
    GdkRGBA base, foreground;
    if (alternate)
    {
        gtk_style_context_get_color(style, GTK_STATE_FLAG_NORMAL, &foreground);
        if (gtk_style_context_lookup_color(style, "theme_base_color", &base))
        {
            base.red += (foreground.red - base.red) * 0.08;
            base.green += (foreground.green - base.green) * 0.08;
            base.blue += (foreground.blue - base.blue) * 0.08;
        }
        else
        {
            base = foreground;
            base.alpha = 0.08;
        }
        g_object_set(cell, "cell-background-rgba", &base, NULL);
    }
    else
        g_object_set(cell, "cell-background-set", FALSE, NULL);
}

/* EN: Preview the capture for run at tick on the given signal name.
 * RU: Открывает запись сеанса run на такте tick для сигнала name. */
static void simulation_preview_at(WaveSimulation *run, const gchar *name, guint64 tick);

/* EN: Request the waveform jump from the activated history row in tree.
 * path and column are unused. data is the history dialog.
 * RU: Запрашивает переход к выбранной строке истории tree. path и column
 * не используются. data — окно истории. */
static void signal_history_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column,
                                     gpointer data)
{
    (void)tree;
    (void)path;
    (void)column;
    gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_APPLY);
}

/* EN: Show history for the name and kind attached to item in simulation data.
 * RU: Показывает историю имени и типа из пункта item для сеанса data. */
static void signal_table_open_history(GtkMenuItem *item, gpointer data)
{
    WaveSimulation *run = data;
    const gchar *name = g_object_get_data(G_OBJECT(item), "signal-name");
    const gchar *kind = g_object_get_data(G_OBJECT(item), "signal-kind");
    gchar *key = g_strconcat(kind, "\t", name, NULL);
    GtkListStore *store = gtk_list_store_new(7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                             G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    for (GList *entry = run->signal_history.head; entry; entry = entry->next)
    {
        const SignalHistoryStop *stop = entry->data;
        const gchar *bits = g_hash_table_lookup(stop->values, key);
        if (!bits)
            continue;
        const gchar *previous = NULL;
        for (GList *older = entry->next; older && !previous; older = older->next)
        {
            const SignalHistoryStop *prior = older->data;
            previous = g_hash_table_lookup(prior->values, key);
        }
        gchar *number = g_strdup_printf("%" G_GUINT64_FORMAT, stop->number);
        gchar *decimal = signal_history_decimal(bits);
        gchar *hex = signal_history_hex(bits);
        guint changes;
        gchar *hex_markup = signal_history_markup(hex, bits, previous, 4, &changes);
        gchar *binary_markup = signal_history_markup(*bits ? bits : "—", bits, previous, 1, &changes);
        gchar *changed_bits = *bits && previous && strlen(bits) == strlen(previous)
                                  ? g_strdup_printf("%u", changes)
                                  : g_strdup("—");
        gchar *location = g_utf8_make_valid(stop->location, -1);
        gchar *tick = stop->has_tick ? g_strdup_printf("%" G_GUINT64_FORMAT, stop->tick) : g_strdup("—");
        gtk_list_store_insert_with_values(store, NULL, -1, 0, number, 1, decimal, 2, hex_markup, 3,
                                          binary_markup, 4, changed_bits, 5, location, 6, tick, -1);
        g_free(tick);
        g_free(hex_markup);
        g_free(binary_markup);
        g_free(changed_bits);
        g_free(location);
        g_free(number);
        g_free(decimal);
        g_free(hex);
    }
    g_free(key);

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        insight_tr("Signal changes"), GTK_WINDOW(run->dialog),
        GTK_DIALOG_DESTROY_WITH_PARENT, insight_tr("_Close"), GTK_RESPONSE_CLOSE,
        insight_tr("Open waveform at stop"), GTK_RESPONSE_APPLY, NULL);
    g_object_ref_sink(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 780, 370);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    GtkWidget *heading = gtk_label_new(name);
    gtk_label_set_selectable(GTK_LABEL(heading), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(heading), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 4);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_widget_set_name(tree, "insight-verilog-signal-history");
    g_object_unref(store);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), GTK_SELECTION_SINGLE);
    const gchar *titles[] = {"Stop number", "Simulation tick", "Decimal", "Hexadecimal",
                             "Binary",      "Changed bits",    "Source"};
    const gint columns[] = {0, 6, 1, 2, 3, 4, 5};
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
    {
        GtkCellRenderer *cell = gtk_cell_renderer_text_new();
        if (i < 6)
            g_object_set(cell, "family", "monospace", "xalign", 1.0f, NULL);
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            insight_tr(titles[i]), cell, columns[i] == 2 || columns[i] == 3 ? "markup" : "text",
            columns[i], NULL);
        gtk_tree_view_column_set_cell_data_func(column, cell, signal_history_stripe, tree, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    }
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    GtkWidget *hint = gtk_label_new(insight_tr("Double-click a stop to open its waveform while paused."));
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 4);
    g_signal_connect(tree, "row-activated", G_CALLBACK(signal_history_activated), dialog);
    GtkWidget *owner = g_object_ref(run->dialog);
    gchar *signal = g_strdup(name);
    guint64 tick = 0;
    gboolean jump = FALSE;
    gtk_widget_show_all(dialog);
    while (insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog)) == GTK_RESPONSE_APPLY)
    {
        GtkTreeModel *model;
        GtkTreeIter row;
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &model,
                                            &row))
        {
            gchar *text;
            gtk_tree_model_get(model, &row, 6, &text, -1);
            jump = g_ascii_string_to_unsigned(text, 10, 0, G_MAXUINT64, &tick, NULL);
            g_free(text);
        }
        if (jump)
            break;
        gtk_label_set_text(GTK_LABEL(hint), insight_tr("Select a stop with a recorded simulation tick."));
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    if (jump && g_object_get_data(G_OBJECT(owner), "insight-live-simulation") == run)
        simulation_preview_at(run, signal, tick);
    g_object_unref(owner);
    g_free(signal);
}
