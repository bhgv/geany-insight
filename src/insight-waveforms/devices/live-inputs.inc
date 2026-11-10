/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Live inputs, UART, virtual controls and instrument plots.
 * RU: Динамический ввод, UART, виртуальные кнопки и графики приборов.
 */

/* EN: Queue the text input selected by button for the simulation in data.
 * RU: Передаёт выбранный по кнопке button ввод в симуляцию из data. */
static void simulation_live_input(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    gchar *port = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(run->live_port_input));
    const gchar *bits = gtk_entry_get_text(GTK_ENTRY(run->live_port_value));
    gchar *directory = simulation_live_directory(run);
    if (port && directory && *bits && strlen(bits) <= 64 && strspn(bits, "01xz") == strlen(bits))
    {
        gchar *basename = g_strdup_printf("geany-input-%s", port);
        gchar *path = g_build_filename(directory, basename, NULL);
        if (!g_file_set_contents(path, bits, -1, NULL))
            gtk_widget_set_tooltip_text(run->live_port_send,
                                        insight_tr("Could not update the simulation input."));
        g_free(path);
        g_free(basename);
    }
    else
        gtk_widget_set_tooltip_text(run->live_port_send,
                                    "Choose an input and enter up to 64 binary digits (0, 1, x or z).");
    g_free(directory);
    g_free(port);
}

/* EN: Queue the bytes from entry for the UART session in data.
 * RU: Передаёт байты из entry в сеанс UART из data. */
static void simulation_uart_enter(GtkEntry *entry, gpointer data)
{
    WaveSimulation *run = data;
    const gchar *text = gtk_entry_get_text(entry);
    if (!*text || run->uart_send_sequence)
        return;
    gchar *directory = simulation_live_directory(run);
    if (!directory)
        return;
    gchar *path = g_build_filename(directory, "geany-uart-input", NULL);
    /* Alternate sequence numbers; the harness remembers the last accepted record. */
    guint next = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(run->dialog), "uart-next")) == 1 ? 2 : 1;
    GString *record = g_string_new(NULL);
    g_string_append_printf(record, "%u\n", next);
    for (const guchar *byte = (const guchar *)text; *byte; byte++)
        g_string_append_printf(record, "%02x\n", (guint)*byte);
    if (g_file_set_contents(path, record->str, record->len, NULL))
    {
        g_object_set_data(G_OBJECT(run->dialog), "uart-next", GUINT_TO_POINTER(next));
        run->uart_send_sequence = next;
        gtk_entry_set_text(entry, "");
        gtk_widget_set_sensitive(GTK_WIDGET(entry), FALSE);
    }
    g_string_free(record, TRUE);
    g_free(path);
    g_free(directory);
}

/* EN: Clear UART window references in simulation data when window is destroyed.
 * RU: Очищает ссылки на консоль UART сеанса data при уничтожении window. */
static void simulation_uart_closed(GtkWidget *window, gpointer data)
{
    (void)window;
    WaveSimulation *run = data;
    run->uart_window = run->uart_view = run->uart_entry = NULL;
}

/* EN: Open the tool from button for the current view or simulation in data.
 * RU: Открывает инструмент по button для текущего просмотра или симуляции из data. */
static void simulation_uart_show(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    if (run->uart_window)
    {
        gtk_window_present(GTK_WINDOW(run->uart_window));
        return;
    }
    GtkWidget *window =
        gtk_dialog_new_with_buttons(strstr(run->port_harness, "// GEANY USART CONSOLE\n") ?
                                    "USART Console" : "UART Console", GTK_WINDOW(run->dialog),
                                    GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    simulation_dialog_tools(
        window,
        "Enter text and press Enter to send it over the configured UART or USART. Received bytes appear "
        "in the transcript. Enable the console option in the device rule.",
        "Введите текст и нажмите Enter для отправки по UART или USART. Принятые байты появляются в "
        "журнале. Включите параметр console в правиле устройства.");
    run->uart_window = window;
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 330);
    g_signal_connect_swapped(window, "response", G_CALLBACK(gtk_widget_destroy), window);
    g_signal_connect(window, "destroy", G_CALLBACK(simulation_uart_closed), run);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(window));
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    run->uart_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(run->uart_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(run->uart_view), TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->uart_view)), run->uart_log->str,
                             -1);
    gtk_container_add(GTK_CONTAINER(scroll), run->uart_view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    run->uart_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(run->uart_entry), 256);
    gtk_entry_set_placeholder_text(GTK_ENTRY(run->uart_entry), "Text to send; press Enter");
    gtk_widget_set_sensitive(run->uart_entry, !run->uart_send_sequence);
    g_signal_connect(run->uart_entry, "activate", G_CALLBACK(simulation_uart_enter), run);
    gtk_box_pack_start(GTK_BOX(box), run->uart_entry, FALSE, FALSE, 4);
    gtk_widget_show_all(window);
}

/* EN: Write value to the port assigned to widget in run. Return whether the update was staged.
 * RU: Записывает value для порта widget в run. Возвращает признак успешной записи. */
static gboolean simulation_control_write(WaveSimulation *run, GtkWidget *widget, guint value)
{
    const gchar *port = g_object_get_data(G_OBJECT(widget), "geany-port");
    gchar *directory = simulation_live_directory(run);
    if (!directory)
        return FALSE;
    gchar *name = g_strdup_printf("geany-control-%s", port);
    gchar *path = g_build_filename(directory, name, NULL);
    gchar *text = g_strdup_printf("%u\n", value);
    GError *error = NULL;
    gboolean written = g_file_set_contents(path, text, -1, &error);
    if (!written)
    {
        gtk_widget_set_tooltip_text(widget, error->message);
        ui_set_statusbar(FALSE, "%s: %s", port, error->message);
        g_error_free(error);
    }
    g_free(text);
    g_free(path);
    g_free(name);
    g_free(directory);
    return written;
}

/* EN: Send a button sequence or switch state from button to simulation data.
 * RU: Передаёт счётчик нажатий или состояние переключателя button в сеанс data. */
static void simulation_control_input(GtkButton *button, gpointer data)
{
    gboolean toggle = GTK_IS_TOGGLE_BUTTON(button);
    guint sequence = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "geany-sequence"));
    guint value = toggle ? (guint)gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)) : sequence + 1;
    if (simulation_control_write(data, GTK_WIDGET(button), value) && !toggle)
        g_object_set_data(G_OBJECT(button), "geany-sequence", GUINT_TO_POINTER(value));
}

/* EN: Send the current slider value to simulation data.
 * RU: Передаёт текущее значение slider в сеанс data. */
static void simulation_slider_input(GtkRange *slider, gpointer data)
{
    simulation_control_write(data, GTK_WIDGET(slider), (guint)gtk_range_get_value(slider));
}

/* EN: Return run's device container, creating it in contents when needed.
 * RU: Возвращает контейнер устройств run и создаёт его в contents при необходимости. */
static GtkWidget *simulation_live_device_box(WaveSimulation *run, GtkWidget *contents)
{
    if (!run->live_devices)
    {
        run->live_devices = gtk_flow_box_new();
        gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(run->live_devices), GTK_SELECTION_NONE);
        gtk_widget_set_sensitive(run->live_devices, FALSE);
        gtk_box_pack_start(GTK_BOX(contents), run->live_devices, FALSE, FALSE, 0);
    }
    return run->live_devices;
}

/* EN: Return the human-readable port or slice name for alias in run's harness.
 * RU: Возвращает читаемое имя порта или среза для alias из тестового модуля run. */
static gchar *simulation_live_port_caption(const WaveSimulation *run, const gchar *alias)
{
    gchar *marker = g_strdup_printf("// GEANY-PORT-SLICE %s ", alias);
    const gchar *start = strstr(run->port_harness, marker);
    if (start)
        start += strlen(marker);
    g_free(marker);
    const gchar *end = start ? strchr(start, '\n') : NULL;
    return end ? g_strndup(start, end - start) : g_strdup(alias);
}

/* EN: Create the live buttons and switches for run in contents.
 * RU: Создаёт живые кнопки и переключатели run в contents. */
static void simulation_live_controls(WaveSimulation *run, GtkWidget *contents)
{
    const gchar *marker = "// GEANY-CONTROL ";
    const gchar *cursor = run->port_harness;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '\n');
        if (!end)
            break;
        gchar *line = g_strndup(cursor, end - cursor);
        gchar **parts = g_strsplit(line, " ", 3);
        gboolean toggle = !strcmp(parts[0], "switch");
        gchar *caption = simulation_live_port_caption(run, parts[1]);
        GtkWidget *widget =
            toggle ? gtk_check_button_new_with_label(caption) : gtk_button_new_with_label(caption);
        g_free(caption);
        if (toggle)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), *parts[2] == '1');
        g_object_set_data_full(G_OBJECT(widget), "geany-port", g_strdup(parts[1]), g_free);
        g_signal_connect(widget, toggle ? "toggled" : "clicked", G_CALLBACK(simulation_control_input),
                         run);
        gtk_container_add(GTK_CONTAINER(simulation_live_device_box(run, contents)), widget);
        g_strfreev(parts);
        g_free(line);
        cursor = end;
    }
}

/* EN: Create the live sliders for run in contents.
 * RU: Создаёт живые ползунки run в contents. */
static void simulation_live_sliders(WaveSimulation *run, GtkWidget *contents)
{
    const gchar *marker = "// GEANY-SLIDER ";
    const gchar *cursor = run->port_harness;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '\n');
        if (!end)
            break;
        gchar *line = g_strndup(cursor, end - cursor);
        gchar **parts = g_strsplit(line, " ", 3);
        gdouble maximum = g_ascii_strtod(parts[1], NULL);
        gchar *caption = simulation_live_port_caption(run, parts[0]);
        GtkWidget *frame = gtk_frame_new(caption);
        GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, maximum, 1);
        gchar *name = g_strconcat("insight-slider-", caption, NULL);
        g_free(caption);
        gtk_widget_set_name(scale, name);
        g_free(name);
        gtk_widget_set_size_request(scale, 160, -1);
        gtk_scale_set_digits(GTK_SCALE(scale), 0);
        gtk_range_set_round_digits(GTK_RANGE(scale), 0);
        gtk_range_set_value(GTK_RANGE(scale), g_ascii_strtod(parts[2], NULL));
        g_object_set_data_full(G_OBJECT(scale), "geany-port", g_strdup(parts[0]), g_free);
        g_signal_connect(scale, "value-changed", G_CALLBACK(simulation_slider_input), run);
        gtk_container_add(GTK_CONTAINER(frame), scale);
        gtk_container_add(GTK_CONTAINER(simulation_live_device_box(run, contents)), frame);
        g_strfreev(parts);
        g_free(line);
        cursor = end;
    }
}

/* EN: Create live indicator cards for run in contents.
 * RU: Создаёт панели индикаторов run в contents. */
/* EN: Build indicator widgets for run in contents, using a fixed-width font for tables.
 * RU: Создаёт индикаторы run в contents с моноширинным шрифтом для таблиц. */
static void simulation_live_indicators(WaveSimulation *run, GtkWidget *contents)
{
    const gchar *marker = "// GEANY-INDICATOR ";
    const gchar *cursor = run->port_harness;
    GtkWidget *flow = NULL;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '\n');
        if (!end)
            break;
        gchar *line = g_strndup(cursor, end - cursor);
        gchar **parts = g_strsplit(line, " ", 2);
        if (!flow)
        {
            flow = gtk_flow_box_new();
            gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
            gtk_box_pack_start(GTK_BOX(contents), flow, FALSE, FALSE, 0);
            run->live_indicators = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        }
        gchar *caption = simulation_live_port_caption(run, parts[0]);
        GtkWidget *frame = gtk_frame_new(caption);
        g_free(caption);
        GtkWidget *value = gtk_label_new("—");
        gchar *name = g_strconcat("insight-indicator-", parts[0], NULL);
        gtk_widget_set_name(value, name);
        g_free(name);
        gtk_label_set_selectable(GTK_LABEL(value), TRUE);
        if (!strcmp(parts[1], "hex-table"))
        {
            PangoAttrList *font = pango_attr_list_new();
            pango_attr_list_insert(font, pango_attr_family_new("monospace"));
            gtk_label_set_attributes(GTK_LABEL(value), font);
            pango_attr_list_unref(font);
        }
        gtk_label_set_line_wrap(GTK_LABEL(value), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(value), PANGO_WRAP_CHAR);
        gtk_label_set_max_width_chars(GTK_LABEL(value), 32);
        gtk_container_set_border_width(GTK_CONTAINER(frame), 4);
        g_object_set_data_full(G_OBJECT(value), "geany-format", g_strdup(parts[1]), g_free);
        g_hash_table_insert(run->live_indicators, g_strdup(parts[0]), value);
        gtk_container_add(GTK_CONTAINER(frame), value);
        gtk_container_add(GTK_CONTAINER(flow), frame);
        g_strfreev(parts);
        g_free(line);
        cursor = end;
    }
}

/* EN: Draw live scope data in widget with Cairo context cr.
 * RU: Рисует осциллограф из data в widget через контекст Cairo cr. */
static gboolean simulation_scope_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    LiveScope *scope = data;
    guint64 low[3], high[3];
    guint start = (scope->next + LIVE_SCOPE_POINTS - scope->count) % LIVE_SCOPE_POINTS;
    guint64 first = scope->samples[start].time;
    guint64 last = scope->samples[(scope->next + LIVE_SCOPE_POINTS - 1) % LIVE_SCOPE_POINTS].time;
    simulation_scope_bounds(scope, start, low, high);
    gdouble w = MAX(1, gtk_widget_get_allocated_width(widget) - 24);
    gdouble h = MAX(1, gtk_widget_get_allocated_height(widget) - 24);
    GdkRGBA color;
    gtk_style_context_get_color(gtk_widget_get_style_context(widget), gtk_widget_get_state_flags(widget),
                                &color);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, .35);
    cairo_rectangle(cr, 12, 12, w, h);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, .18, .52, .82);
    gboolean previous = FALSE;
    gdouble previous_y = 0;
    CaptureView *view = g_object_get_data(G_OBJECT(widget), "capture-view");
    const LiveScopeSample *selected = NULL;
    gdouble nearest = 144;
    for (guint i = 0; i < scope->count; i++)
    {
        const LiveScopeSample *sample = &scope->samples[(start + i) % LIVE_SCOPE_POINTS];
        if (!sample->point.valid)
        {
            previous = FALSE;
            continue;
        }
        gdouble x, y;
        instrument_project(&sample->point, low, high, scope->axes, &x, &y);
        if (scope->axes == 1)
        {
            y = x;
            x = last == first ? 0 : (gdouble)(sample->time - first) / (gdouble)(last - first);
        }
        x = 12 + x * w;
        y = 12 + (1 - y) * h;
        if (view->pick_pending)
        {
            gdouble distance = capture_point_distance(view, x, y);
            if (distance <= nearest)
            {
                nearest = distance;
                selected = sample;
            }
        }
        if (!previous)
        {
            cairo_move_to(cr, x, y);
            cairo_rel_line_to(cr, .5, 0);
            cairo_move_to(cr, x, y);
        }
        else
        {
            if (scope->axes == 1)
                cairo_line_to(cr, x, previous_y);
            cairo_line_to(cr, x, y);
        }
        previous = TRUE;
        previous_y = y;
    }
    cairo_stroke(cr);
    if (view->pick_pending)
    {
        if (selected && scope->axes == 1)
        {
            guint64 coordinates[] = {selected->time, selected->point.values[0]};
            capture_scope_point(view, coordinates, 2);
        }
        else
            capture_scope_point(view, selected ? selected->point.values : NULL, scope->axes);
    }
    return FALSE;
}

/* EN: Create live scope widgets from run's harness and pack them into contents.
 * RU: Создаёт осциллографы по тестбенчу run и помещает их в contents. */
static void simulation_live_scopes(WaveSimulation *run, GtkWidget *contents)
{
    const gchar *marker = "// GEANY-SCOPE ";
    const gchar *cursor = run->port_harness;
    GtkWidget *notebook = NULL;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '\n');
        if (!end)
            break;
        if (!notebook)
        {
            notebook = gtk_notebook_new();
            GtkWidget *expander = gtk_expander_new("Live Plots");
            gtk_container_add(GTK_CONTAINER(expander), notebook);
            gtk_box_pack_start(GTK_BOX(contents), expander, FALSE, FALSE, 0);
            run->live_scopes = g_ptr_array_new_with_free_func(g_free);
        }
        LiveScope *scope = g_new0(LiveScope, 1);
        scope->axes = (guint)(*cursor - '0');
        gchar *signals = g_strndup(cursor + 2, end - cursor - 2);
        GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *heading = gtk_label_new(signals);
        gtk_label_set_line_wrap(GTK_LABEL(heading), TRUE);
        gtk_box_pack_start(GTK_BOX(page), heading, FALSE, FALSE, 0);
        g_free(signals);
        scope->drawing = gtk_drawing_area_new();
        gtk_widget_set_size_request(scope->drawing, 260, 130);
        capture_view_new(page, scope->drawing, simulation_scope_draw, scope);
        gtk_box_pack_start(GTK_BOX(page), scope->drawing, TRUE, TRUE, 0);
        scope->status = gtk_label_new("Waiting for samples…");
        gtk_label_set_line_wrap(GTK_LABEL(scope->status), TRUE);
        gchar *status_name = g_strdup_printf("insight-live-scope-%u", run->live_scopes->len + 1);
        gtk_widget_set_name(scope->status, status_name);
        g_free(status_name);
        gtk_box_pack_start(GTK_BOX(page), scope->status, FALSE, FALSE, 0);
        const gchar *kind = scope->axes == 1 ? "Time" : scope->axes == 2 ? "XY" : "XYZ";
        gchar *tab = g_strdup_printf("%s %u", kind, run->live_scopes->len + 1);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, gtk_label_new(tab));
        g_free(tab);
        g_ptr_array_add(run->live_scopes, scope);
        cursor = end;
    }
}

/* EN: Build the controls and outputs of run in box.
 * RU: Размещает органы управления и индикаторы run в box. */
static void simulation_live_panel(WaveSimulation *run, GtkWidget *box)
{
    const gchar *marker = "$fopen(\"geany-input-";
    GtkWidget *contents = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(box), contents, FALSE, FALSE, 0);
    run->live_port_samples = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    simulation_live_controls(run, contents);
    simulation_live_sliders(run, contents);
    simulation_live_indicators(run, contents);
    simulation_live_scopes(run, contents);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    run->live_port_input = gtk_combo_box_text_new();
    const gchar *cursor = run->port_harness;
    guint count = 0;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '"');
        if (!end)
            break;
        gchar *name = g_strndup(cursor, end - cursor);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(run->live_port_input), name);
        g_free(name);
        cursor = end;
        count++;
    }
    if (count)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(run->live_port_input), 0);
        run->live_port_value = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->live_port_value), 64);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->live_port_value), "Binary value (0, 1, x, z)");
        run->live_port_send = gtk_button_new_with_label("Apply Input");
        gtk_widget_set_sensitive(run->live_port_send, FALSE);
        g_signal_connect(run->live_port_send, "clicked", G_CALLBACK(simulation_live_input), run);
        gtk_box_pack_start(GTK_BOX(row), run->live_port_input, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), run->live_port_value, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), run->live_port_send, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(contents), row, FALSE, FALSE, 0);
    }
    else
        gtk_widget_destroy(row);
    run->live_port_display = gtk_label_new("Waiting for port samples…");
    gtk_widget_set_name(run->live_port_display, "insight-live-port-readings");
    gtk_label_set_selectable(GTK_LABEL(run->live_port_display), TRUE);
    gtk_label_set_xalign(GTK_LABEL(run->live_port_display), 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, -1, 105);
    gtk_container_add(GTK_CONTAINER(scroll), run->live_port_display);
    gtk_box_pack_start(GTK_BOX(contents), scroll, TRUE, TRUE, 0);
}
