/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Bus devices, recipe setup and fixture selection.
 * RU: Устройства шин, настройка рецепта и выбор тестового проекта. */

/* EN: Select a device, bind its first port and load the image from data.
 * RU: Выбирает устройство, назначает первый порт и загружает образ из data. */
static gboolean add_bus_memory(gpointer data)
{
    const MemoryWizard *memory = data;
    GtkWidget *wizard = find_window("Add Test Device");
    if (!wizard)
        return G_SOURCE_CONTINUE;
    GtkWidget *kind = find_control(wizard, "insight-test-device-type");
    GtkWidget *ports = find_control(wizard, "insight-test-device-ports");
    if (!GTK_IS_COMBO_BOX(kind) || !GTK_IS_TREE_VIEW(ports))
        return G_SOURCE_CONTINUE;
    if (g_strcmp0(gtk_combo_box_get_active_id(GTK_COMBO_BOX(kind)), memory->kind) &&
        !gtk_combo_box_set_active_id(GTK_COMBO_BOX(kind), memory->kind))
        return G_SOURCE_CONTINUE;
    GtkWidget *first = find_control(wizard, "insight-device-first-field");
    GtkWidget *file = find_control(wizard, "insight-device-file");
    if (!GTK_IS_ENTRY(first) || !GTK_IS_FILE_CHOOSER(file))
        return G_SOURCE_CONTINUE;
    gchar *selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file));
    gboolean selected_image = !g_strcmp0(selected, memory->image);
    g_free(selected);
    if (!selected_image)
    {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(file), memory->image);
        return G_SOURCE_CONTINUE;
    }

    GtkTreeView *view = GTK_TREE_VIEW(ports);
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter row;
    for (gboolean more = gtk_tree_model_get_iter_first(model, &row); more;
         more = gtk_tree_model_iter_next(model, &row))
    {
        gchar *name;
        gtk_tree_model_get(model, &row, 0, &name, -1);
        gboolean match = !strcmp(name, memory->first_port);
        g_free(name);
        if (!match)
            continue;
        gtk_entry_set_text(GTK_ENTRY(first), "replace_me");
        gtk_widget_grab_focus(first);
        GtkTreePath *path = gtk_tree_model_get_path(model, &row);
        gtk_tree_view_row_activated(view, path, gtk_tree_view_get_column(view, 0));
        gtk_tree_path_free(path);
        if (strcmp(gtk_entry_get_text(GTK_ENTRY(first)), memory->first_port))
            return G_SOURCE_CONTINUE;
        gtk_dialog_response(GTK_DIALOG(wizard), GTK_RESPONSE_ACCEPT);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* EN: Verify the generated testbench in the preview, then close it. data points
 * to the caller's result flag. Return G_SOURCE_CONTINUE until the window exists.
 * RU: Проверяет сгенерированный тестбенч в просмотре и закрывает окно.
 * data указывает на результат. Возвращает G_SOURCE_CONTINUE до появления окна. */
static gboolean check_testbench_preview(gpointer data)
{
    GtkWidget *dialog = find_window("Generated Testbench");
    if (!dialog)
        return G_SOURCE_CONTINUE;
    GtkWidget *view = find_control(dialog, "console");
    if (GTK_IS_TEXT_VIEW(view))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        GtkTextIter first, last;
        gtk_text_buffer_get_bounds(buffer, &first, &last);
        gchar *hdl = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        *(gboolean *)data = !gtk_text_view_get_editable(GTK_TEXT_VIEW(view)) &&
                           strstr(hdl, "module geany_port_testbench;") &&
                           strstr(hdl, "$geany_sram(") && strstr(hdl, "geany_slice_net") &&
                           strstr(hdl, "$geany_spi_nor(") && strstr(hdl, "$geany_i2c_eeprom(");
        g_free(hdl);
    }
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    return G_SOURCE_REMOVE;
}

/* EN: Confirm the diagram help mentions DUT. data holds the result flag.
 * RU: Проверяет упоминание DUT в справке схемы. data хранит результат. */
static gboolean check_diagram_help(gpointer data)
{
    GtkWidget *dialog = find_window("Verilog Setup Help");
    if (!dialog)
        return G_SOURCE_CONTINUE;
    GtkWidget *view = find_control(dialog, "insight-verilog-help-text");
    if (GTK_IS_TEXT_VIEW(view))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        GtkTextIter first, last;
        gtk_text_buffer_get_bounds(buffer, &first, &last);
        gchar *text = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        *(gboolean *)data = strstr(text, "DUT") != NULL;
        g_free(text);
    }
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    return G_SOURCE_REMOVE;
}

/* EN: Verify port rules are absent until explicitly included in the assistant.
 * data holds the result flag. Close without sending any request.
 * RU: Проверяет, что правила появляются только после явного добавления.
 * data хранит результат. Закрывает окно без отправки запроса. */
static gboolean check_diagram_ai_context(gpointer data)
{
    GtkWidget *dialog = find_window("Verilog AI Assistant");
    if (!dialog)
        return G_SOURCE_CONTINUE;
    GtkWidget *view = find_control(dialog, "insight-ai-context");
    if (GTK_IS_TEXT_VIEW(view))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        GtkTextIter first, last;
        gtk_text_buffer_get_bounds(buffer, &first, &last);
        gchar *before = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        gboolean included = click(dialog, "insight-verilog-ai-include-rules");
        gtk_text_buffer_get_bounds(buffer, &first, &last);
        gchar *after = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        *(gboolean *)data = included && !strstr(before, "module live_devices") &&
                           strstr(after, "module live_devices") && strstr(after, "hex-table hex_out 16");
        g_free(before);
        g_free(after);
    }
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    return G_SOURCE_REMOVE;
}

/* EN: Check diagram help, AI context and filters, then activate the hex-table port. data is the
 * caller's result flag. Return G_SOURCE_CONTINUE until the window appears.
 * RU: Проверяет справку, контекст ИИ и фильтры, затем нажимает порт hex-таблицы. data — флаг
 * результата. Возвращает G_SOURCE_CONTINUE до появления окна. */
static gboolean check_testbench_diagram(gpointer data)
{
    GtkWidget *dialog = find_window("Testbench Diagram");
    if (!dialog)
        return G_SOURCE_CONTINUE;
    gboolean help_ok = FALSE;
    guint help = g_timeout_add(10, check_diagram_help, &help_ok);
    if (!click(dialog, "insight-verilog-dialog-help"))
        g_source_remove(help);
    gboolean assistant_ok = FALSE;
    guint assistant = g_timeout_add(10, check_diagram_ai_context, &assistant_ok);
    if (!click(dialog, "insight-verilog-dialog-ai"))
        g_source_remove(assistant);
    GtkWidget *slice = find_control(dialog, "level · 16 bits\nlevel[7:0]");
    GtkWidget *table = find_control(dialog, "hex_out · 16 bits");
    GtkWidget *search = find_control(dialog, "insight-diagram-search");
    GtkWidget *direction = find_control(dialog, "insight-diagram-direction");
    GtkWidget *regex = find_control(dialog, "Regex");
    gboolean labels = find_control(dialog, "insight-testbench-diagram") &&
                      find_control(dialog, "DUT · live_devices") && slice && table &&
                      GTK_IS_ENTRY(search) && GTK_IS_COMBO_BOX(direction) &&
                      GTK_IS_TOGGLE_BUTTON(regex);
    gboolean filters = FALSE;
    if (labels)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(regex), TRUE);
        gtk_entry_set_text(GTK_ENTRY(search), "hex[-_]table");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(direction), "input");
        filters = !gtk_widget_is_visible(table);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(direction), "output");
        filters &= gtk_widget_is_visible(table) && !gtk_widget_is_visible(slice);
    }
    *(gboolean *)data = help_ok && assistant_ok && labels && filters &&
                       click(dialog, "hex_out · 16 bits");
    if (!*(gboolean *)data)
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    return G_SOURCE_REMOVE;
}

/* EN: Populate modeless setup and verify sorting, rule navigation and both previews.
 * data is unused. Return G_SOURCE_CONTINUE until the setup can be accepted.
 * RU: Заполняет настройки и проверяет сортировку, переход и оба вида просмотра.
 * data не используется. Возвращает G_SOURCE_CONTINUE, пока настройки недоступны. */
static gboolean accept_ports(gpointer data)
{
    (void)data;
    GtkWidget *dialog = find_window("Port Signals");
    if (!dialog)
        return G_SOURCE_CONTINUE;
    if (live_devices)
    {
        GtkWidget *recipe = find_control(dialog, "console");
        if (!GTK_IS_TEXT_VIEW(recipe))
            return G_SOURCE_CONTINUE;
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(recipe)),
            "module live_devices\n"
            "duration 10000000\n"
            "live-interval 250\n"
            "live-input enable 1 0\n"
            "switch switch_on\n"
            "button push\n"
            "capture-trigger fall push\n"
            "monitor pushed 1\n"
            "led ready\n"
            "display result\n"
            "display level_out 8 dec\n"
            "hex-table hex_out 16\n"
            "scope level_out\n"
            "scope-xy level_out result\n"
            "scope-xyz level_out result ready\n"
            "monitor model_result 8\n"
            "monitor spi_result 8\n"
            "monitor i2c_result 8\n"
            "model ext external_memory \"external memory.sv\"\n"
            "model-param ext DATA \"8'h3c\"\n"
            "model-value ext mask \"8'h05\"\n"
            "model-share ext ready enable\n"
            "model-link ext external_data data 8\n"
            "sram - - - - - 4\n"
            "uart rx tx 1000000 console\n", -1);
        guint wizard = g_timeout_add(10, add_test_slider, NULL);
        if (!click(dialog, "Add Test Device…"))
        {
            g_source_remove(wizard);
            return G_SOURCE_CONTINUE;
        }
        GtkWidget *sort = find_control(dialog, "insight-port-sort");
        if (!GTK_IS_COMBO_BOX(sort) || gtk_combo_box_get_active(GTK_COMBO_BOX(sort)) != 2)
            return G_SOURCE_CONTINUE;
        GtkWidget *tree = find_control(dialog, "insight-test-device-ports");
        if (!GTK_IS_TREE_VIEW(tree))
            return G_SOURCE_CONTINUE;
        GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
        GtkTreeIter row;
        gboolean found = FALSE;
        gboolean next = gtk_tree_model_get_iter_first(model, &row);
        while (next && !found)
        {
            gchar *name;
            gtk_tree_model_get(model, &row, 0, &name, -1);
            found = !strcmp(name, "hex_out");
            g_free(name);
            if (!found)
                next = gtk_tree_model_iter_next(model, &row);
        }
        if (!found)
            return G_SOURCE_CONTINUE;
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &row);
        if (!click(dialog, "Go to Rule…"))
            return G_SOURCE_CONTINUE;
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(recipe));
        GtkTextIter first, last;
        if (!gtk_text_buffer_get_selection_bounds(buffer, &first, &last))
            return G_SOURCE_CONTINUE;
        gchar *selection = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        gboolean correct = !strcmp(selection, "hex-table hex_out 16");
        g_free(selection);
        if (!correct)
            return G_SOURCE_CONTINUE;
        const MemoryWizard memories[] = {{"spi-nor", "sck", spi_image},
            {"i2c-eeprom", "scl", i2c_image}};
        for (guint i = 0; i < G_N_ELEMENTS(memories); i++)
        {
            guint device = g_timeout_add(10, add_bus_memory, (gpointer)&memories[i]);
            if (!click(dialog, "Add Test Device…"))
            {
                g_source_remove(device);
                return G_SOURCE_CONTINUE;
            }
        }
        gboolean preview_ok = FALSE;
        guint preview = g_timeout_add(10, check_testbench_preview, &preview_ok);
        if (!click(dialog, "Preview Testbench…"))
        {
            g_source_remove(preview);
            return G_SOURCE_CONTINUE;
        }
        if (!preview_ok)
            return G_SOURCE_CONTINUE;
        gtk_text_buffer_get_start_iter(buffer, &first);
        gtk_text_buffer_place_cursor(buffer, &first);
        gboolean diagram_ok = FALSE;
        guint diagram = g_timeout_add(10, check_testbench_diagram, &diagram_ok);
        if (!click(dialog, "Preview Diagram…"))
        {
            g_source_remove(diagram);
            return G_SOURCE_CONTINUE;
        }
        if (!diagram_ok || !gtk_text_buffer_get_selection_bounds(buffer, &first, &last))
            return G_SOURCE_CONTINUE;
        selection = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        correct = !strcmp(selection, "hex-table hex_out 16");
        g_free(selection);
        if (!correct)
            return G_SOURCE_CONTINUE;
    }
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* Answer the real file chooser once GTK has selected the requested file. */
/* Подтверждает реальный диалог выбора, когда GTK выбрал нужный файл. */
static gboolean accept_fixture(gpointer data)
{
    const gchar *path = data;
    GtkWidget *window = find_window("Load Verilog Observer");
    if (!GTK_IS_FILE_CHOOSER(window))
        return G_SOURCE_CONTINUE;
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(window);
    gchar *selected = gtk_file_chooser_get_filename(chooser);
    gboolean ready = !g_strcmp0(selected, path);
    g_free(selected);
    if (!ready)
    {
        gtk_file_chooser_select_filename(chooser, path);
        return G_SOURCE_CONTINUE;
    }
    gtk_dialog_response(GTK_DIALOG(window), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

