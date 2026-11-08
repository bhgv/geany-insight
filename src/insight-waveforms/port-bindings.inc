/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Port assignments, occupancy and navigation to recipe rules.
 * RU: Назначение портов, занятость и переход к правилам.
 */

/* EN: Return the argument indexes that designate DUT ports for rule args, with argc tokens.
 * RU: Возвращает индексы аргументов, задающих порты схемы для правила args из argc токенов. */
static guint port_browser_pin_mask(gchar **args, gint argc)
{
    const gchar *kind = args[0];
    if (!strcmp(kind, "spi-lua") && argc == 2)
        return 0;
    if (!strcmp(kind, "i2c-lua") && argc == 2)
        return 0;
    if (g_str_has_prefix(kind, "spi-"))
        return 0x1e | (!strcmp(kind, "spi-lua") && argc == 7 ? 1u << 6 : 0);
    if (g_str_has_prefix(kind, "i2c-"))
        return 6 | (!strcmp(kind, "i2c-lua") && argc == 6 ? 1u << 5 : 0);
    if (!strcmp(kind, "model-link") || !strcmp(kind, "model-share") || !strcmp(kind, "capture-trigger"))
        return 1u << 2;
    if (!strcmp(kind, "scope-xyz"))
        return 14;
    if (!strcmp(kind, "usart") || !strcmp(kind, "usart-clock"))
        return 14;
    if (!strcmp(kind, "scope-xy") || !strcmp(kind, "uart"))
        return 6;
    const gchar *single[] = {"clock",      "clock-hz", "pulse",   "button",    "switch",
                             "slider",     "led",      "display", "hex-table", "scope",
                             "live-input", "monitor",  "output",  "value",     "bus"};
    for (guint i = 0; i < G_N_ELEMENTS(single); i++)
        if (!strcmp(kind, single[i]))
            return 2;
    const gchar *memories[] = {"sram", "sdram", "ddr1", "ddr2", "ddr3", "ddr4"};
    const guint pins[] = {5, 10, 11, 12, 11};
    for (guint i = 0; i < G_N_ELEMENTS(memories); i++)
        if (!strcmp(kind, memories[i]))
            return (1u << (pins[i] + 1)) - 2;
    return 0;
}

/* EN: Give direction a stable sort rank for input, output, inout, ref, and unknown.
 * RU: Возвращает порядок сортировки direction для input, output, inout, ref и неизвестного. */
static guint port_browser_direction_rank(const gchar *direction)
{
    const gchar *roles[] = {"input", "output", "inout", "ref"};
    for (guint i = 0; i < G_N_ELEMENTS(roles); i++)
        if (!strcmp(direction, roles[i]))
            return i;
    return G_N_ELEMENTS(roles);
}

/* EN: Refresh the PortBrowser in data from the edited recipe buffer.
 * RU: Обновляет список PortBrowser из data по тексту рецепта в buffer. */
static void port_browser_refresh(GtkTextBuffer *buffer, gpointer data)
{
    PortBrowser *browser = data;
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    gchar *recipe = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
    gchar **lines = g_strsplit(recipe, "\n", -1);
    gchar *module = NULL;
    GHashTable *aliases = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (guint line = 0; lines[line]; line++)
    {
        gchar **args = NULL;
        gint count;
        if (g_shell_parse_argv(lines[line], &count, &args, NULL))
        {
            if (count == 8 && !strcmp(args[0], "port-slice"))
                g_hash_table_replace(aliases, g_strdup(args[1]), g_strdup(args[2]));
            g_strfreev(args);
        }
    }
    GHashTable *bindings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (guint line = 0; lines[line]; line++)
    {
        gchar **args = NULL;
        gint argc;
        if (!g_shell_parse_argv(lines[line], &argc, &args, NULL))
            continue;
        if (argc == 2 && !strcmp(args[0], "module") && !module)
            module = g_strdup(args[1]);
        simulation_memory_defaults(&args, &argc);
        guint mask = port_browser_pin_mask(args, argc);
        gchar *fallback[] = {args[0], "sck", "cs_n", "mosi", "miso"};
        gchar *i2c[] = {args[0], "scl", "sda"};
        gchar **pins = args;
        gint count = argc;
        if (argc == 2 && !strcmp(args[0], "spi-lua"))
        {
            pins = fallback;
            count = G_N_ELEMENTS(fallback);
            mask = 0x1e;
        }
        else if (argc == 2 && !strcmp(args[0], "i2c-lua"))
        {
            pins = i2c;
            count = G_N_ELEMENTS(i2c);
            mask = 6;
        }
        for (gint i = 1; i < MIN(count, 16); i++)
        {
            if (!(mask & (1u << i)))
                continue;
            const gchar *actual = g_hash_table_lookup(aliases, pins[i]);
            gchar *port = actual ? g_strdup(actual) : g_strndup(pins[i], strcspn(pins[i], "["));
            const gchar *previous = g_hash_table_lookup(bindings, port);
            const gchar *device = g_str_has_prefix(args[0], "model-") ? args[1] : args[0];
            gchar *entry = g_strdup_printf("%s (line %u)", device, line + 1);
            gchar *combined = previous ? g_strconcat(previous, ", ", entry, NULL) : g_strdup(entry);
            g_hash_table_replace(bindings, port, combined);
            g_free(entry);
        }
        g_strfreev(args);
    }
    if (!browser->ports || g_strcmp0(browser->module, module))
    {
        g_clear_pointer(&browser->ports, g_hash_table_unref);
        g_free(browser->module);
        browser->module = g_strdup(module);
        browser->ports = port_browser_ports(browser->expanded, module);
    }
    GList *names = g_list_sort(g_hash_table_get_keys(browser->ports), (GCompareFunc)g_strcmp0);
    gtk_list_store_clear(browser->store);
    for (GList *item = names; item; item = item->next)
    {
        const gchar *name = item->data;
        const gchar *attached = g_hash_table_lookup(bindings, name);
        PortInfo *info = g_hash_table_lookup(browser->ports, name);
        gchar *width = info->width ? g_strdup_printf("%u", info->width) : g_strdup("?");
        GtkTreeIter row;
        gtk_list_store_insert_with_values(
            browser->store, &row, -1, PORT_NAME, name, PORT_DIRECTION, info->direction, PORT_BINDINGS,
            attached ? attached : "Unassigned", PORT_WIDTH_TEXT, width, PORT_WIDTH, info->width,
            PORT_LEFT, info->left, PORT_RIGHT, info->right, PORT_ORDER, info->order, PORT_OCCUPIED,
            (guint)(attached != NULL), PORT_DIRECTION_RANK, port_browser_direction_rank(info->direction),
            -1);
        g_free(width);
    }
    g_hash_table_unref(aliases);
    g_list_free(names);
    g_hash_table_unref(bindings);
    g_strfreev(lines);
    g_free(module);
    g_free(recipe);
}

/* EN: Copy the selected port from tree (callback data) when button is clicked.
 * RU: По нажатию button копирует выбранный порт из tree, переданного через data. */
static void port_browser_copy(GtkButton *button, gpointer data)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
    GtkTreeModel *model;
    GtkTreeIter row;
    if (gtk_tree_selection_get_selected(selection, &model, &row))
    {
        gchar *port;
        gtk_tree_model_get(model, &row, 0, &port, -1);
        GtkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(button), GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(clipboard, port, -1);
        g_free(port);
    }
}

/* EN: Read one-based rule line numbers from a bindings caption. The caller owns
 * the returned array.
 * RU: Извлекает номера строк правил от единицы из подписи bindings. Массив
 * принадлежит вызывающей стороне. */
static GArray *port_browser_binding_lines(const gchar *bindings)
{
    GArray *lines = g_array_new(FALSE, FALSE, sizeof(guint));
    GRegex *pattern = g_regex_new("\\(line ([0-9]+)\\)", 0, 0, NULL);
    GMatchInfo *match;
    g_regex_match(pattern, bindings, 0, &match);
    while (g_match_info_matches(match))
    {
        gchar *number = g_match_info_fetch(match, 1);
        guint64 value;
        if (g_ascii_string_to_unsigned(number, 10, 1, G_MAXINT, &value, NULL))
        {
            guint line = (guint)value;
            if (!lines->len || g_array_index(lines, guint, lines->len - 1) != line)
                g_array_append_val(lines, line);
        }
        g_free(number);
        g_match_info_next(match, NULL);
    }
    g_match_info_free(match);
    g_regex_unref(pattern);
    return lines;
}

/* EN: Select and scroll to line in browser's recipe editor. line is one-based.
 * RU: Выделяет строку line от единицы и прокручивает редактор правил browser к ней. */
static void port_browser_select_rule(PortBrowser *browser, guint line)
{
    if (line > (guint)gtk_text_buffer_get_line_count(browser->recipe))
        return;
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_line(browser->recipe, &start, (gint)line - 1);
    end = start;
    gtk_text_iter_forward_to_line_end(&end);
    gtk_text_buffer_select_range(browser->recipe, &start, &end);
    gtk_text_view_scroll_to_iter(browser->recipe_view, &start, 0.15, FALSE, 0, 0);
    gtk_widget_grab_focus(GTK_WIDGET(browser->recipe_view));
}

/* EN: Follow the bindings of row in model for browser. If several rules use the
 * port, let the user pick one in a modal child. Cancellation leaves focus alone.
 * RU: Переходит к правилам порта из row модели model для browser. Если правил
 * несколько, предлагает выбрать одно в модальном окне. Отмена сохраняет фокус. */
static void port_browser_follow_rule(PortBrowser *browser, GtkTreeModel *model, GtkTreeIter *row)
{
    gchar *bindings;
    gtk_tree_model_get(model, row, PORT_BINDINGS, &bindings, -1);
    GArray *lines = port_browser_binding_lines(bindings);
    g_free(bindings);
    guint target = lines->len == 1 ? g_array_index(lines, guint, 0) : 0;
    if (lines->len > 1)
    {
        GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(browser->tree));
        GtkWidget *dialog = gtk_dialog_new_with_buttons(
            "Choose Port Rule", parent, GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
            "_Go to Rule", GTK_RESPONSE_ACCEPT, NULL);
        simulation_dialog_tools(
            dialog,
            "Choose the connection rule for this port by line number and text. Go to Rule closes "
            "this picker and selects the rule in Port Signals.",
            "Выберите привязку порта по номеру строки и тексту. Go to Rule закрывает выбор и "
            "выделяет правило в Port Signals.");
        g_object_ref_sink(dialog);
        GtkWidget *combo = gtk_combo_box_text_new();
        for (guint i = 0; i < lines->len; i++)
        {
            guint line = g_array_index(lines, guint, i);
            GtkTextIter start, end;
            gtk_text_buffer_get_iter_at_line(browser->recipe, &start, (gint)line - 1);
            end = start;
            gtk_text_iter_forward_to_line_end(&end);
            gchar *rule = gtk_text_buffer_get_text(browser->recipe, &start, &end, FALSE);
            gchar *label = g_strdup_printf("%u · %s", line, rule);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), label);
            g_free(label);
            g_free(rule);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), combo, FALSE, FALSE,
                           8);
        gtk_widget_show_all(dialog);
        if (insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        {
            gint selected = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
            if (selected >= 0 && (guint)selected < lines->len)
                target = g_array_index(lines, guint, selected);
        }
        gtk_widget_destroy(dialog);
        g_object_unref(dialog);
    }
    if (target)
        port_browser_select_rule(browser, target);
    g_array_unref(lines);
}

/* EN: Follow the selected port rule when button is clicked; data is the browser.
 * RU: По нажатию button переходит к выбранному правилу порта из browser в data. */
static void port_browser_rule_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    PortBrowser *browser = data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree));
    GtkTreeModel *model;
    GtkTreeIter row;
    if (gtk_tree_selection_get_selected(selection, &model, &row))
        port_browser_follow_rule(browser, model, &row);
}

/* EN: Follow the rule for path in tree using browser in data. column is unused.
 * RU: Переходит к правилу строки path из tree через browser в data. column не используется. */
static void port_browser_rule_activated(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column,
                                        gpointer data)
{
    (void)column;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter row;
    if (gtk_tree_model_get_iter(model, &row, path))
        port_browser_follow_rule(data, model, &row);
}
