/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Generated testbench source and interactive wiring diagram.
 * RU: Исходник тестбенча и интерактивная схема соединений.
 */

/* EN: Generate HDL for recipe using directory to resolve files. sources and
 * images receive staged dependencies. Return owned HDL or set error on failure.
 * A recipe for an existing testbench returns NULL without an error.
 * RU: Генерирует HDL из recipe, разрешая файлы относительно directory.
 * sources и images получают зависимости. Возвращает новый HDL или задаёт
 * error. Для готового тестбенча возвращает NULL без ошибки. */
static gchar *simulation_setup_harness(const gchar *recipe, const gchar *directory, GPtrArray *sources,
                                       GHashTable *images, GError **error)
{
    if (strlen(recipe) > 16384)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "The port recipe exceeds 16 KiB.");
        return NULL;
    }
    return simulation_port_harness(recipe, directory, sources, images, error);
}

/* EN: Add a selectable read-only text page named title to notebook.
 * RU: Добавляет в notebook вкладку title с текстом text для чтения и копирования. */
static void simulation_preview_page(GtkNotebook *notebook, const gchar *title, const gchar *text)
{
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), text, -1);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_notebook_append_page(notebook, scroll, gtk_label_new(title));
}

/* EN: Preview the unsaved recipe buffer in data using the source directory on
 * button. Validate exactly as launch does and leave the recipe unchanged.
 * RU: Показывает несохранённый рецепт из буфера data. Каталог берётся из button.
 * Проверяет рецепт так же, как запуск, и не меняет его. */
static void simulation_preview_harness(GtkButton *button, gpointer data)
{
    GtkTextBuffer *buffer = data;
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    gchar *recipe = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
    const gchar *directory = g_object_get_data(G_OBJECT(button), "source-directory");
    GPtrArray *sources = g_ptr_array_new_with_free_func(g_free);
    GHashTable *images =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
    GError *error = NULL;
    gchar *hdl = simulation_setup_harness(recipe, directory, sources, images, &error);
    g_free(recipe);

    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons("Generated Testbench", parent, GTK_DIALOG_DESTROY_WITH_PARENT,
                                    "_Close", GTK_RESPONSE_CLOSE, NULL);
    simulation_dialog_tools(
        dialog,
        "Review and copy the generated SystemVerilog. Dependencies lists model sources and "
        "staged files. A validation error appears here before launch.",
        "Просмотрите и скопируйте сгенерированный SystemVerilog. Dependencies показывает "
        "исходники моделей и подготовленные файлы. Ошибки правил видны до запуска.");
    g_object_ref_sink(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 480);
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), notebook, TRUE, TRUE, 0);
    if (!hdl)
        simulation_preview_page(
            GTK_NOTEBOOK(notebook), "Validation",
            error ? error->message : "This recipe runs the existing testbench without generating HDL.");
    else
    {
        simulation_preview_page(GTK_NOTEBOOK(notebook), "SystemVerilog", hdl);
        GString *dependencies = g_string_new(NULL);
        for (guint i = 0; i < sources->len; i++)
        {
            gchar *path = g_filename_display_name(sources->pdata[i]);
            g_string_append_printf(dependencies, "Model source: %s\n", path);
            g_free(path);
        }
        GList *names = g_list_sort(g_hash_table_get_keys(images), (GCompareFunc)g_strcmp0);
        for (GList *item = names; item; item = item->next)
        {
            gchar *name = simulation_memory_filename(item->data);
            GBytes *bytes = g_hash_table_lookup(images, item->data);
            g_string_append_printf(dependencies, "Staged file: %s (%" G_GSIZE_FORMAT " bytes)\n", name,
                                   g_bytes_get_size(bytes));
            g_free(name);
        }
        g_list_free(names);
        simulation_preview_page(GTK_NOTEBOOK(notebook), "Dependencies",
                                dependencies->len ? dependencies->str
                                                  : "No model sources or device files.");
        g_string_free(dependencies, TRUE);
    }
    g_clear_error(&error);
    g_free(hdl);
    g_hash_table_unref(images);
    g_ptr_array_unref(sources);

    gtk_widget_show_all(dialog);
    insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
}

/* EN: Draw the connection line in area with theme colors on cr. data encodes
 * a second arrowhead for bidirectional ports. Return FALSE for normal GTK drawing.
 * RU: Рисует линию связи в area на cr цветом темы. data задаёт вторую стрелку
 * для двунаправленного порта. Возвращает FALSE для обычной отрисовки GTK. */
static gboolean simulation_diagram_arrow(GtkWidget *area, cairo_t *cr, gpointer data)
{
    GdkRGBA color;
    gtk_style_context_get_color(gtk_widget_get_style_context(area), gtk_widget_get_state_flags(area),
                                &color);
    gdk_cairo_set_source_rgba(cr, &color);
    gdouble left = 3;
    gdouble right = gtk_widget_get_allocated_width(area) - 3;
    gdouble center = gtk_widget_get_allocated_height(area) / 2.0;
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, left, center);
    cairo_line_to(cr, right, center);
    cairo_move_to(cr, right - 6, center - 4);
    cairo_line_to(cr, right, center);
    cairo_line_to(cr, right - 6, center + 4);
    if (GPOINTER_TO_INT(data))
    {
        cairo_move_to(cr, left + 6, center - 4);
        cairo_line_to(cr, left, center);
        cairo_line_to(cr, left + 6, center + 4);
    }
    cairo_stroke(cr);
    return FALSE;
}

/* EN: Add a selectable labeled block at column and row in grid. title is the
 * optional frame caption and text is the block content.
 * RU: Добавляет выделяемый блок в column и row сетки grid. title — подпись
 * рамки, text — содержимое блока. */
static void simulation_diagram_block(GtkGrid *grid, gint column, gint row, const gchar *title,
                                     const gchar *text)
{
    GtkWidget *frame = gtk_frame_new(title);
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 42);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_widget_set_margin_start(label, 8);
    gtk_widget_set_margin_end(label, 8);
    gtk_widget_set_margin_top(label, 4);
    gtk_widget_set_margin_bottom(label, 4);
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_container_add(GTK_CONTAINER(frame), label);
    gtk_grid_attach(grid, frame, column, row, 1, 1);
}

/* EN: Add an arrow between blocks at column and row of grid. both draws arrows
 * at both ends for a bidirectional connection.
 * RU: Добавляет стрелку между блоками в column и row сетки grid. both рисует
 * стрелки с обеих сторон для двунаправленного соединения. */
static void simulation_diagram_link(GtkGrid *grid, gint column, gint row, gboolean both)
{
    GtkWidget *arrow = gtk_drawing_area_new();
    gtk_widget_set_size_request(arrow, 36, 20);
    g_signal_connect(arrow, "draw", G_CALLBACK(simulation_diagram_arrow), GINT_TO_POINTER(both));
    gtk_grid_attach(grid, arrow, column, row, 1, 1);
}

/* EN: Append binding to text as its own line. The caller supplies an allocated
 * GString and owns the original binding.
 * RU: Добавляет binding отдельной строкой в text. GString и исходная строка
 * принадлежат вызывающему коду. */
static void simulation_diagram_append(GString *text, const gchar *binding)
{
    if (text->len)
        g_string_append_c(text, '\n');
    g_string_append(text, binding);
}

/* EN: Collect the rendered slice expressions by original port from generated hdl.
 * Return an owned lookup table of multiline captions.
 * RU: Собирает выражения срезов из hdl по исходному порту. Возвращает таблицу
 * многострочных подписей, принадлежащую вызывающему коду. */
static GHashTable *simulation_diagram_slices(const gchar *hdl)
{
    GHashTable *slices = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const gchar *marker = "// GEANY-PORT-SLICE ";
    const gchar *cursor = hdl;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '\n');
        if (!end)
            break;
        const gchar *alias_end = strchr(cursor, ' ');
        if (alias_end && alias_end < end)
        {
            const gchar *value = alias_end + 1;
            const gchar *bracket = strchr(value, '[');
            if (bracket && bracket < end)
            {
                gchar *port = g_strndup(value, bracket - value);
                gchar *slice = g_strndup(value, end - value);
                const gchar *previous = g_hash_table_lookup(slices, port);
                gchar *caption = previous ? g_strconcat(previous, "\n", slice, NULL) : g_strdup(slice);
                g_hash_table_replace(slices, port, caption);
                g_free(slice);
            }
        }
        cursor = end;
    }
    return slices;
}

/* EN: Return from the diagram dialog in data with the port selected by button.
 * RU: Завершает диалог схемы из data, сохраняя порт, выбранный кнопкой button. */
static void simulation_diagram_choose_port(GtkButton *button, gpointer data)
{
    GtkDialog *dialog = data;
    const gchar *port = g_object_get_data(G_OBJECT(button), "diagram-port");
    g_object_set_data_full(G_OBJECT(dialog), "selected-diagram-port", g_strdup(port), g_free);
    gtk_dialog_response(dialog, GTK_RESPONSE_APPLY);
}

/* EN: Place a navigable port with name, direction and caption in grid at row.
 * Clicking the block returns the name to dialog.
 * RU: Добавляет в grid на строку row порт name с direction и caption.
 * При нажатии блок возвращает имя в dialog. */
static void simulation_diagram_port(GtkGrid *grid, gint row, const gchar *name, const gchar *direction,
                                    const gchar *caption, GtkDialog *dialog)
{
    GtkWidget *frame = gtk_frame_new(direction);
    GtkWidget *button = gtk_button_new_with_label(caption);
    gtk_widget_set_hexpand(frame, TRUE);
    gtk_widget_set_tooltip_text(button, "Go to the rules for this port");
    g_object_set_data_full(G_OBJECT(button), "diagram-port", g_strdup(name), g_free);
    g_signal_connect(button, "clicked", G_CALLBACK(simulation_diagram_choose_port), dialog);
    gtk_container_add(GTK_CONTAINER(frame), button);
    gtk_grid_attach(grid, frame, 2, row, 1, 1);
}

/* EN: Find port in the unfiltered browser store and follow its rule bindings.
 * RU: Находит port в списке browser без фильтров и переходит к его правилам. */
static void simulation_diagram_follow_port(PortBrowser *browser, const gchar *port)
{
    GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
    GtkTreeIter row;
    gboolean next = gtk_tree_model_get_iter_first(model, &row);
    while (next)
    {
        gchar *name;
        gtk_tree_model_get(model, &row, PORT_NAME, &name, -1);
        gboolean matches = !strcmp(name, port);
        g_free(name);
        if (matches)
        {
            port_browser_follow_rule(browser, model, &row);
            return;
        }
        next = gtk_tree_model_iter_next(model, &row);
    }
}

/* EN: Filter the diagram in data after widget changes. Match the query and
 * direction together, keeping each visible connection row intact.
 * RU: Фильтрует схему из data после изменения widget. Совмещает запрос
 * и направление, сохраняя видимые соединения целиком. */
static void simulation_diagram_filter(GtkWidget *widget, gpointer data)
{
    (void)widget;
    GtkGrid *grid = data;
    guint count = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(grid), "diagram-port-count"));
    GtkLabel *status = g_object_get_data(G_OBJECT(grid), "diagram-status");
    GtkEntry *search = g_object_get_data(G_OBJECT(grid), "diagram-search");
    GtkToggleButton *regex = g_object_get_data(G_OBJECT(grid), "diagram-regex");
    GtkComboBox *direction = g_object_get_data(G_OBJECT(grid), "diagram-direction");
    const gchar *role = gtk_combo_box_get_active_id(direction);
    const gchar *text = gtk_entry_get_text(search);
    gchar *query = g_utf8_casefold(text, -1);
    GError *error = NULL;
    GRegex *pattern = *text && gtk_toggle_button_get_active(regex)
                          ? g_regex_new(text, G_REGEX_CASELESS, 0, &error)
                          : NULL;
    gtk_entry_set_icon_from_icon_name(search, GTK_ENTRY_ICON_PRIMARY,
                                      error ? "dialog-error-symbolic" : "edit-find-symbolic");
    gtk_entry_set_icon_tooltip_text(search, GTK_ENTRY_ICON_PRIMARY, error ? error->message : NULL);
    guint visible = 0;
    for (guint row = 1; row <= count; row++)
    {
        GtkWidget *port = gtk_grid_get_child_at(grid, 2, row);
        const gchar *port_role = g_object_get_data(G_OBJECT(port), "diagram-port-role");
        const gchar *raw = g_object_get_data(G_OBJECT(port), "diagram-raw-text");
        const gchar *folded = g_object_get_data(G_OBJECT(port), "diagram-search-text");
        gboolean matches =
            !error && (!role || !*role || !strcmp(role, port_role)) &&
            (pattern ? g_regex_match(pattern, raw, 0, NULL) : strstr(folded, query) != NULL);
        for (gint column = 0; column < 5; column++)
        {
            GtkWidget *child = gtk_grid_get_child_at(grid, column, row);
            if (child)
                gtk_widget_set_visible(child, matches);
        }
        visible += matches;
    }
    gchar *summary = error ? g_strdup("Invalid regular expression")
                           : g_strdup_printf("%u of %u connected ports", visible, count);
    gtk_label_set_text(status, summary);
    g_free(summary);
    g_clear_pointer(&pattern, g_regex_unref);
    g_clear_error(&error);
    g_free(query);
}

/* EN: Build a scrollable diagram from browser's assigned ports and hdl's slices.
 * Use module as the DUT caption, add local search and return clicks to dialog.
 * RU: Строит прокручиваемую схему по портам browser и срезам из hdl.
 * module задаёт заголовок DUT. Добавляет поиск и передаёт нажатия в dialog. */
static GtkWidget *simulation_diagram_widget(const PortBrowser *browser, const gchar *module,
                                            const gchar *hdl, GtkDialog *dialog)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *search = gtk_search_entry_new();
    gtk_widget_set_name(search, "insight-diagram-search");
    gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Find ports, slices or devices");
    gtk_box_pack_start(GTK_BOX(toolbar), search, TRUE, TRUE, 0);
    GtkWidget *regex = gtk_check_button_new_with_label("Regex");
    gtk_box_pack_start(GTK_BOX(toolbar), regex, FALSE, FALSE, 0);
    GtkWidget *roles = gtk_combo_box_text_new();
    gtk_widget_set_name(roles, "insight-diagram-direction");
    const gchar *ids[] = {"", "input", "output", "inout", "ref"};
    const gchar *labels[] = {"All directions", "Input", "Output", "Inout", "Ref"};
    for (guint i = 0; i < G_N_ELEMENTS(ids); i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(roles), ids[i], labels[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(roles), 0);
    gtk_box_pack_start(GTK_BOX(toolbar), roles, FALSE, FALSE, 0);
    GtkWidget *status = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 6);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_widget_set_name(GTK_WIDGET(grid), "insight-testbench-diagram");
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_grid_set_column_spacing(grid, 6);
    gtk_grid_set_row_spacing(grid, 6);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(grid));
    gchar *title = g_strdup_printf("DUT · %s", module ? module : "design");
    simulation_diagram_block(grid, 0, 0, NULL, "Stimulus");
    simulation_diagram_block(grid, 2, 0, NULL, title);
    simulation_diagram_block(grid, 4, 0, NULL, "Devices and probes");
    g_free(title);

    GHashTable *slices = simulation_diagram_slices(hdl);
    GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
    GtkTreeIter item;
    gint row = 1;
    gboolean next = gtk_tree_model_get_iter_first(model, &item);
    while (next)
    {
        guint occupied;
        gtk_tree_model_get(model, &item, PORT_OCCUPIED, &occupied, -1);
        if (occupied)
        {
            gchar *name, *direction, *width, *bindings;
            gtk_tree_model_get(model, &item, PORT_NAME, &name, PORT_DIRECTION, &direction,
                               PORT_WIDTH_TEXT, &width, PORT_BINDINGS, &bindings, -1);
            const gchar *used = g_hash_table_lookup(slices, name);
            gchar *port =
                g_strdup_printf("%s · %s bits%s%s", name, width, used ? "\n" : "", used ? used : "");
            simulation_diagram_port(grid, row, name, direction, port, dialog);
            GtkWidget *block = gtk_grid_get_child_at(grid, 2, row);
            gchar *search_text = g_strjoin(" ", port, direction, bindings, NULL);
            g_object_set_data_full(G_OBJECT(block), "diagram-search-text",
                                   g_utf8_casefold(search_text, -1), g_free);
            g_object_set_data_full(G_OBJECT(block), "diagram-raw-text", search_text, g_free);
            g_object_set_data_full(G_OBJECT(block), "diagram-port-role", g_strdup(direction), g_free);
            GString *left = g_string_new(NULL), *right = g_string_new(NULL);
            gchar **rules = g_strsplit(bindings, ", ", -1);
            gboolean input = !strcmp(direction, "input");
            gboolean both = !strcmp(direction, "inout") || !strcmp(direction, "ref");
            for (guint i = 0; rules[i]; i++)
            {
                const gchar *rule = rules[i];
                gboolean probe = g_str_has_prefix(rule, "scope") ||
                                 g_str_has_prefix(rule, "capture-trigger ") ||
                                 g_str_has_prefix(rule, "monitor ");
                simulation_diagram_append(input && !probe ? left : right, rule);
            }
            if (left->len)
            {
                simulation_diagram_block(grid, 0, row, NULL, left->str);
                simulation_diagram_link(grid, 1, row, FALSE);
            }
            if (right->len)
            {
                simulation_diagram_link(grid, 3, row, both);
                simulation_diagram_block(grid, 4, row, NULL, right->str);
            }
            row++;
            g_strfreev(rules);
            g_string_free(left, TRUE);
            g_string_free(right, TRUE);
            g_free(port);
            g_free(bindings);
            g_free(width);
            g_free(direction);
            g_free(name);
        }
        next = gtk_tree_model_iter_next(model, &item);
    }
    if (row == 1)
        simulation_diagram_block(grid, 2, row, NULL, "No port bindings found.");
    g_hash_table_unref(slices);
    g_object_set_data(G_OBJECT(grid), "diagram-port-count", GUINT_TO_POINTER(row - 1));
    g_object_set_data(G_OBJECT(grid), "diagram-status", status);
    g_object_set_data(G_OBJECT(grid), "diagram-search", search);
    g_object_set_data(G_OBJECT(grid), "diagram-regex", regex);
    g_object_set_data(G_OBJECT(grid), "diagram-direction", roles);
    g_signal_connect(search, "changed", G_CALLBACK(simulation_diagram_filter), grid);
    g_signal_connect(regex, "toggled", G_CALLBACK(simulation_diagram_filter), grid);
    g_signal_connect(roles, "changed", G_CALLBACK(simulation_diagram_filter), grid);
    simulation_diagram_filter(NULL, grid);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    return box;
}

/* EN: Validate the recipe in browser (data) and open a separate modal diagram
 * from button. Leave the recipe and simulator state untouched.
 * RU: Проверяет правила browser из data и открывает отдельную модальную схему
 * по button. Не меняет настройки и состояние симулятора. */
static void simulation_preview_diagram(GtkButton *button, gpointer data)
{
    PortBrowser *browser = data;
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(browser->recipe, &first, &last);
    gchar *recipe = gtk_text_buffer_get_text(browser->recipe, &first, &last, FALSE);
    const gchar *directory = g_object_get_data(G_OBJECT(button), "source-directory");
    GPtrArray *sources = g_ptr_array_new_with_free_func(g_free);
    GHashTable *images =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
    GError *error = NULL;
    gchar *hdl = simulation_setup_harness(recipe, directory, sources, images, &error);
    g_free(recipe);
    g_ptr_array_unref(sources);
    g_hash_table_unref(images);

    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Testbench Diagram", parent, GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    simulation_dialog_tools(
        dialog,
        "The DUT ports are in the center, stimuli on the left and devices or probes on the "
        "right. Search by port, slice or device and filter by direction. Click a port to jump "
        "to a rule.",
        "Порты DUT в центре, источники слева, устройства и наблюдатели справа. Ищите по порту, "
        "срезу или устройству, фильтруйте по направлению. Нажмите порт для перехода к правилу.");
    g_object_ref_sink(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 880, 540);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    if (hdl)
        gtk_box_pack_start(GTK_BOX(box),
                           simulation_diagram_widget(browser, browser->module, hdl, GTK_DIALOG(dialog)),
                           TRUE, TRUE, 0);
    else
    {
        GtkWidget *notebook = gtk_notebook_new();
        simulation_preview_page(
            GTK_NOTEBOOK(notebook), "Validation",
            error ? error->message
                  : "This recipe runs an existing testbench without a generated connection diagram.");
        gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE, 0);
    }
    g_clear_error(&error);
    g_free(hdl);
    gtk_widget_show_all(dialog);
    gint response = insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog));
    gchar *selected = response == GTK_RESPONSE_APPLY
                          ? g_strdup(g_object_get_data(G_OBJECT(dialog), "selected-diagram-port"))
                          : NULL;
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    if (selected)
    {
        simulation_diagram_follow_port(browser, selected);
        g_free(selected);
    }
}
