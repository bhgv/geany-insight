/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Port filtering, sorting and browser widgets.
 * RU: Фильтрация, сортировка и интерфейс списка портов.
 */

/* EN: Return whether direction suits the DUT role required by field.
 * RU: Проверяет, подходит ли направление direction для роли поля field. */
static gboolean port_browser_matches(const gchar *direction, gchar field)
{
    gboolean input = !strcmp(direction, "input");
    gboolean output = !strcmp(direction, "output");
    gboolean bidirectional = !strcmp(direction, "inout");

    return ((field == 'i' || field == 'd') && (input || bidirectional)) ||
           ((field == 'o' || field == 'c') && (output || bidirectional)) ||
           (field == 'b' && (input || output || bidirectional)) ||
           (field == 'a' && strcmp(direction, "—"));
}

/* EN: Match text against browser's compiled regex or folded literal query.
 * RU: Сопоставляет text с регулярным выражением или поисковым текстом из browser. */
static gboolean port_browser_query_matches(const PortBrowser *browser, const gchar *text)
{
    if (browser->query_regex)
        return g_regex_match(browser->query_regex, text, 0, NULL);
    if (!browser->folded_query || !*browser->folded_query)
        return TRUE;
    gchar *folded = g_utf8_casefold(text, -1);
    gboolean matches = strstr(folded, browser->folded_query) != NULL;
    g_free(folded);
    return matches;
}

/* EN: Return whether row in model matches the browser filters stored in data.
 * RU: Проверяет, соответствует ли row из model фильтрам списка из data. */
static gboolean port_browser_visible(GtkTreeModel *model, GtkTreeIter *row, gpointer data)
{
    PortBrowser *browser = data;
    if (browser->regex_invalid)
        return FALSE;
    guint occupied;
    gtk_tree_model_get(model, row, PORT_OCCUPIED, &occupied, -1);
    gint occupancy = gtk_combo_box_get_active(GTK_COMBO_BOX(browser->occupancy));
    if ((occupancy == 1 && occupied) || (occupancy == 2 && !occupied))
        return FALSE;
    gchar *name, *direction, *bindings;
    guint width;
    gtk_tree_model_get(model, row, PORT_NAME, &name, PORT_DIRECTION, &direction, PORT_BINDINGS, &bindings,
                       PORT_WIDTH, &width, -1);
    gint target = gtk_combo_box_get_active(GTK_COMBO_BOX(browser->search_target));
    gboolean visible = ((target <= 0 || target == 3) && port_browser_query_matches(browser, name)) ||
                       ((target == 1 || target == 3) && port_browser_query_matches(browser, direction)) ||
                       ((target == 2 || target == 3) && port_browser_query_matches(browser, bindings));
    if (visible && browser->direction && browser->compatible &&
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(browser->compatible)))
    {
        visible =
            port_browser_matches(direction, browser->direction) && width &&
            (browser->direction == 'a' || (browser->required_width && width >= browser->required_width));
    }
    g_free(bindings);
    g_free(direction);
    g_free(name);
    return visible;
}

/* EN: Apply filter changes from widget to the port browser in data.
 * RU: Применяет изменения фильтра из widget к списку портов из data. */
static void port_browser_refilter(GtkWidget *widget, gpointer data)
{
    (void)widget;
    PortBrowser *browser = data;
    gtk_tree_model_filter_refilter(browser->filter);
    gtk_widget_queue_draw(browser->tree);
}

/* EN: Compile the current name pattern and refilter browser from data.
 * widget triggered the change. Invalid expressions are shown beside the query.
 * RU: Компилирует шаблон имени и обновляет список из data. widget вызвал
 * изменение. Ошибка выражения показывается рядом с запросом. */
static void port_browser_search_changed(GtkWidget *widget, gpointer data)
{
    PortBrowser *browser = data;
    g_clear_pointer(&browser->query_regex, g_regex_unref);
    g_clear_pointer(&browser->folded_query, g_free);
    GError *error = NULL;
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(browser->search));
    if (*query && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(browser->regex_toggle)))
        browser->query_regex = g_regex_new(query, G_REGEX_CASELESS, 0, &error);
    else
        browser->folded_query = g_utf8_casefold(query, -1);
    browser->regex_invalid = error != NULL;
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(browser->search), GTK_ENTRY_ICON_PRIMARY,
                                      error ? "dialog-error-symbolic" : "edit-find-symbolic");
    gtk_entry_set_icon_tooltip_text(GTK_ENTRY(browser->search), GTK_ENTRY_ICON_PRIMARY,
                                    error ? error->message : NULL);
    g_clear_error(&error);
    port_browser_refilter(widget, browser);
}

/* EN: Compare rows a and b in model by the numeric column in data and then by declaration order.
 * RU: Сравнивает строки a и b модели model по числовому столбцу data, затем по порядку объявления. */
static gint port_browser_compare_group(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
    gint column = GPOINTER_TO_INT(data);
    guint first, second, order_a, order_b;
    gtk_tree_model_get(model, a, column, &first, PORT_ORDER, &order_a, -1);
    gtk_tree_model_get(model, b, column, &second, PORT_ORDER, &order_b, -1);
    if (first != second)
        return first < second ? -1 : 1;
    return order_a == order_b ? 0 : order_a < order_b ? -1 : 1;
}

/* EN: Apply the sort selection and reverse flag to browser in data. widget is unused.
 * RU: Применяет порядок сортировки и направление к списку из data. widget не используется. */
static void port_browser_sort_changed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    PortBrowser *browser = data;
    const gint columns[] = {PORT_NAME, PORT_DIRECTION_RANK, PORT_OCCUPIED, PORT_ORDER};
    gint selected = gtk_combo_box_get_active(GTK_COMBO_BOX(browser->sort_choice));
    if (selected < 0 || (guint)selected >= G_N_ELEMENTS(columns))
        return;
    gboolean reverse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(browser->reverse));
    gtk_tree_sortable_set_sort_column_id(browser->sorted, columns[selected],
                                         reverse ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}

/* EN: Color cell for row in model when its direction matches the focused browser
 * field in data. column is unused.
 * RU: Подсвечивает cell строки row из model при совпадении направления с полем
 * в data. column не используется. */
static void port_browser_color(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model,
                               GtkTreeIter *row, gpointer data)
{
    (void)column;
    PortBrowser *browser = data;
    gchar *direction;
    gtk_tree_model_get(model, row, PORT_DIRECTION, &direction, -1);
    gboolean matches = browser->direction && port_browser_matches(direction, browser->direction);
    if (matches)
        g_object_set(cell, "cell-background", "#cde8ff", "foreground", "#102a43", NULL);
    g_object_set(cell, "cell-background-set", matches, "foreground-set", matches, NULL);
    g_free(direction);
}

/* EN: Create the Ports and Source notebook for browser. The caller keeps browser alive
 * until the dialog and its recipe change handler are destroyed.
 * RU: Создаёт вкладки портов и исходника для browser. Вызывающий хранит browser до
 * уничтожения диалога и отключения обработчика изменений текста. */
static GtkWidget *port_browser_widget(PortBrowser *browser)
{
    GtkWidget *notebook = gtk_notebook_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    if (browser->preprocess_error)
    {
        gchar *message = g_strdup_printf("Port preprocessing failed.\n%s", browser->preprocess_error);
        GtkWidget *diagnostic = gtk_label_new(message);
        gtk_label_set_line_wrap(GTK_LABEL(diagnostic), TRUE);
        gtk_label_set_selectable(GTK_LABEL(diagnostic), TRUE);
        gtk_box_pack_start(GTK_BOX(box), diagnostic, FALSE, FALSE, 0);
        g_free(message);
    }
    browser->store =
        gtk_list_store_new(PORT_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                           G_TYPE_UINT, G_TYPE_INT, G_TYPE_INT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
    GtkWidget *filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    browser->search = gtk_search_entry_new();
    gtk_widget_set_name(browser->search, "insight-port-search");
    gtk_entry_set_placeholder_text(GTK_ENTRY(browser->search), "Find ports");
    browser->search_target = gtk_combo_box_text_new();
    gtk_widget_set_name(browser->search_target, "insight-port-search-target");
    const gchar *targets[] = {"Name", "Direction", "Devices / rules", "All"};
    for (guint i = 0; i < G_N_ELEMENTS(targets); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(browser->search_target), targets[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(browser->search_target), 0);
    gtk_box_pack_start(GTK_BOX(filters), browser->search_target, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(filters), browser->search, TRUE, TRUE, 0);
    browser->regex_toggle = gtk_check_button_new_with_label("Regex");
    gtk_box_pack_start(GTK_BOX(filters), browser->regex_toggle, FALSE, FALSE, 0);
    if (browser->choosing)
    {
        browser->compatible = gtk_check_button_new_with_label("Compatible only");
        gtk_widget_set_sensitive(browser->compatible, FALSE);
        gtk_box_pack_start(GTK_BOX(filters), browser->compatible, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), filters, FALSE, FALSE, 0);
    GtkWidget *sorting = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(sorting), gtk_label_new("Sort by"), FALSE, FALSE, 0);
    browser->sort_choice = gtk_combo_box_text_new();
    gtk_widget_set_name(browser->sort_choice, "insight-port-sort");
    const gchar *choices[] = {"Name", "Direction", "Occupancy", "Declaration order"};
    for (guint i = 0; i < G_N_ELEMENTS(choices); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(browser->sort_choice), choices[i]);
    gtk_box_pack_start(GTK_BOX(sorting), browser->sort_choice, TRUE, TRUE, 0);
    browser->reverse = gtk_check_button_new_with_label("Reverse");
    gtk_box_pack_start(GTK_BOX(sorting), browser->reverse, FALSE, FALSE, 0);
    browser->occupancy = gtk_combo_box_text_new();
    gtk_widget_set_name(browser->occupancy, "insight-port-occupancy");
    const gchar *states[] = {"All ports", "Unassigned only", "Assigned only"};
    for (guint i = 0; i < G_N_ELEMENTS(states); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(browser->occupancy), states[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(browser->occupancy), 0);
    gtk_box_pack_start(GTK_BOX(sorting), browser->occupancy, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), sorting, FALSE, FALSE, 0);

    GtkTreeModel *filtered = gtk_tree_model_filter_new(GTK_TREE_MODEL(browser->store), NULL);
    browser->filter = GTK_TREE_MODEL_FILTER(filtered);
    gtk_tree_model_filter_set_visible_func(browser->filter, port_browser_visible, browser, NULL);
    GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(filtered);
    browser->sorted = GTK_TREE_SORTABLE(sorted);
    gtk_tree_sortable_set_sort_func(browser->sorted, PORT_DIRECTION_RANK, port_browser_compare_group,
                                    GINT_TO_POINTER(PORT_DIRECTION_RANK), NULL);
    gtk_tree_sortable_set_sort_func(browser->sorted, PORT_OCCUPIED, port_browser_compare_group,
                                    GINT_TO_POINTER(PORT_OCCUPIED), NULL);
    GtkWidget *tree = gtk_tree_view_new_with_model(sorted);
    browser->tree = tree;
    gtk_widget_set_name(tree, "insight-test-device-ports");
    g_object_unref(sorted);
    g_object_unref(filtered);
    g_object_unref(browser->store);
    g_signal_connect(browser->search, "changed", G_CALLBACK(port_browser_search_changed), browser);
    g_signal_connect(browser->search_target, "changed", G_CALLBACK(port_browser_refilter), browser);
    g_signal_connect(browser->occupancy, "changed", G_CALLBACK(port_browser_refilter), browser);
    g_signal_connect(browser->regex_toggle, "toggled", G_CALLBACK(port_browser_search_changed), browser);
    g_signal_connect(browser->sort_choice, "changed", G_CALLBACK(port_browser_sort_changed), browser);
    g_signal_connect(browser->reverse, "toggled", G_CALLBACK(port_browser_sort_changed), browser);
    gtk_combo_box_set_active(GTK_COMBO_BOX(browser->sort_choice), 0);
    if (browser->compatible)
        g_signal_connect(browser->compatible, "toggled", G_CALLBACK(port_browser_refilter), browser);
    const gchar *titles[] = {"Port", "Direction", "Bits", "Devices / rules"};
    const gint columns[] = {PORT_NAME, PORT_DIRECTION, PORT_WIDTH_TEXT, PORT_BINDINGS};
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
    {
        GtkCellRenderer *cell = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column =
            gtk_tree_view_column_new_with_attributes(titles[i], cell, "text", columns[i], NULL);
        gtk_tree_view_column_set_cell_data_func(column, cell, port_browser_color, browser, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    }
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *copy = gtk_button_new_with_label("Copy Port Name");
    g_signal_connect(copy, "clicked", G_CALLBACK(port_browser_copy), tree);
    gtk_box_pack_start(GTK_BOX(actions), copy, FALSE, FALSE, 0);
    if (browser->recipe_view)
    {
        GtkWidget *go = gtk_button_new_with_label("Go to Rule…");
        g_signal_connect(go, "clicked", G_CALLBACK(port_browser_rule_clicked), browser);
        g_signal_connect(tree, "row-activated", G_CALLBACK(port_browser_rule_activated), browser);
        gtk_box_pack_start(GTK_BOX(actions), go, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, gtk_label_new("Ports"));
    GtkWidget *source = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(source), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(source), TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(source)),
                             browser->source ? browser->source : "Source unavailable.", -1);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), source);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll, gtk_label_new("Source"));
    port_browser_refresh(browser->recipe, browser);
    return notebook;
}
