/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Device wizard fields and help text.
 * RU: Поля и справка мастера устройств.
 */

/* EN: Attach field and its title to row in form and return field.
 * RU: Добавляет field с подписью title в строку row формы form и возвращает field. */
static GtkWidget *device_field(DeviceForm *form, const gchar *title, GtkWidget *field, guint row)
{
    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), field);
    g_object_set_data(G_OBJECT(field), "port-label", label);
    g_object_set_data_full(G_OBJECT(field), "port-title", g_strdup(title), g_free);
    gtk_widget_set_hexpand(field, TRUE);
    gtk_grid_attach(GTK_GRID(form->grid), label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(form->grid), field, 1, row, 1, 1);
    return field;
}

/* EN: Find a conventional one-bit serial port in form for the requested direction.
 * RU: Находит в form однобитный последовательный порт нужного направления. */
static const gchar *device_serial_port(DeviceForm *form, const gchar *value, gchar direction)
{
    if (!form->browser || !form->browser->ports)
        return value;
    GHashTableIter iter;
    gpointer key, data;
    const gchar *match = NULL;
    guint best_rank = 0;
    g_hash_table_iter_init(&iter, form->browser->ports);
    while (g_hash_table_iter_next(&iter, &key, &data))
    {
        PortInfo *port = data;
        const gchar *name = key;
        if (port->width != 1 || !port_browser_matches(port->direction, direction))
            continue;
        if (!strcmp(name, value))
            return name;
        guint rank = 0;
        if (!g_ascii_strcasecmp(name, value))
            rank = 2;
        else if (strlen(name) == 3 && !g_ascii_strncasecmp(name, value, 2) &&
                 g_ascii_tolower(name[2]) == 'd')
            rank = 1;
        if (rank > best_rank)
        {
            best_rank = rank;
            match = name;
        }
        else if (rank && rank == best_rank)
            match = NULL;
    }
    return match ? match : value;
}

/* EN: Store the polarity selected in combo in the recipe entry.
 * RU: Записывает полярность из combo в поле рецепта entry. */
static void device_button_polarity(GtkComboBox *combo, GtkEntry *entry)
{
    gtk_entry_set_text(entry, gtk_combo_box_get_active(combo) == 1 ? "1" : "0");
}

/* EN: Reflect entry's saved idle level in the polarity combo.
 * RU: Показывает сохранённый в entry уровень покоя в combo. */
static void device_button_polarity_sync(GtkEntry *entry, GtkComboBox *combo)
{
    gtk_combo_box_set_active(combo, !strcmp(gtk_entry_get_text(entry), "1"));
}

/* EN: Enable match field only for the match condition chosen in combo.
 * RU: Включает поле field только при выборе сравнения в combo. */
static void device_trigger_match(GtkComboBox *combo, GtkWidget *field)
{
    gtk_widget_set_sensitive(field, !g_strcmp0(gtk_combo_box_get_active_id(combo), "match"));
}

/* EN: Rebuild the fields for combo's selected preset in the form stored in data.
 * RU: Перестраивает поля формы из data для шаблона, выбранного в combo. */
static void simulation_device_type(GtkComboBox *combo, gpointer data)
{
    DeviceForm *form = data;
    gint selected = device_catalog_selected(combo);
    GList *children = gtk_container_get_children(GTK_CONTAINER(form->grid));
    g_ptr_array_set_size(form->entries, 0);
    g_list_free_full(children, (GDestroyNotify)gtk_widget_destroy);
    form->file = form->reset = form->active = NULL;
    device_port_filter(form);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(form->dialog), GTK_RESPONSE_ACCEPT, selected >= 0);
    if (selected < 0)
    {
        simulation_dialog_guidance(
            form->dialog, "No device matches. Change the name filter, regular expression or category.",
            "Совпадений нет. Измените имя, регулярное выражение или категорию.");
        return;
    }
    const DevicePreset *preset = &device_presets[selected];
    gchar *english = g_strdup_printf(
        "%s (%s)\\n\\nSet the port names and values in the form. Double-click a compatible port "
        "or use Choose Slice for a specific range. Insert Rule adds the validated rule.",
        preset->title, preset->rule);
    gchar *russian = g_strdup_printf(
        "%s (%s)\\n\\nУкажите порты и значения. Двойной щелчок назначает подходящий порт, "
        "Choose Slice выбирает диапазон. Insert Rule добавляет проверенное правило.",
        preset->title, preset->rule);
    if (preset->script || preset->image >= 0)
    {
        gchar *en = g_strconcat(english,
                                preset->script
                                    ? "\\nSelect a Lua script. The active-low reset signal is optional."
                                    : "\\nThe hex memory image is optional.",
                                NULL);
        gchar *ru = g_strconcat(
            russian,
            preset->script ? "\\nВыберите Lua-скрипт. Сигнал сброса с активным нулём необязателен."
                           : "\\nФайл hex-образа необязателен.",
            NULL);
        g_free(english);
        g_free(russian);
        english = en;
        russian = ru;
    }
    const gchar *details_en[] = {
        "Use the module name declared in the source.",
        "Times are in nanoseconds. The testbench drives the selected input.",
        "The control appears in Live Port I/O and drives the selected input.",
        "The indicator reads a DUT output at the live sampling interval.",
        "Plots read signals or slices relative to the DUT and show recent samples.",
        "RX and TX are relative to the DUT. The console exchanges asynchronous 8N1 bytes.",
        "The memory device attaches to the DUT controller. The optional image preloads its contents.",
        "SPI uses mode 0 and an individual active-low chip select. SCK, MOSI and MISO can be shared.",
        "I²C uses open-drain lines with pull-ups and a 7-bit address.",
        "The testbench drives a constant expression at the selected width.",
        "The table displays hexadecimal bytes, including unknown and high-impedance states.",
        "Display models receive video signals.",
        "The OBUFDS model converts a TMDS bit into a digital differential pair. The HDL transmitter encodes RGB."};
    const gchar *details_ru[] = {
        "Укажите имя модуля, объявленное в исходнике.",
        "Время задаётся в наносекундах. Тестбенч управляет выбранным входом.",
        "Элемент появляется в Live Port I/O и управляет входом схемы.",
        "Индикатор считывает выход схемы с интервалом живого опроса.",
        "График считывает сигналы или срезы DUT и показывает недавние выборки.",
        "RX и TX задаются относительно DUT. Консоль передаёт байты в режиме 8N1.",
        "Память подключается к контроллеру DUT. Файл образа загружает начальные данные.",
        "SPI работает в режиме 0 с отдельным CS#. Линии SCK, MOSI и MISO можно делить.",
        "I²C использует открытый сток, подтяжки и 7-битный адрес.",
        "Тестбенч задаёт постоянное выражение выбранной ширины.",
        "Таблица показывает байты в hex, включая неизвестные и высокоимпедансные состояния.",
        "Модели дисплеев принимают видеосигналы.",
        "Модель OBUFDS формирует цифровую дифференциальную пару из бита TMDS. RGB кодирует передатчик в HDL."};
    G_STATIC_ASSERT(G_N_ELEMENTS(details_en) == G_N_ELEMENTS(device_kind_names));
    G_STATIC_ASSERT(G_N_ELEMENTS(details_ru) == G_N_ELEMENTS(device_kind_names));
    const gchar *help_en = details_en[preset->icon];
    const gchar *help_ru = details_ru[preset->icon];
    gboolean trigger = !strcmp(preset->rule, "capture-trigger");
    if (!strcmp(preset->rule, "duration"))
    {
        help_en = "Set the time from simulation start in nanoseconds. If a clock limit is also set, the first limit reached ends the run.";
        help_ru = "Укажите время от начала симуляции в наносекундах. Если задан и предел тактов, сработает первое достигнутое ограничение.";
    }
    else if (!strcmp(preset->rule, "duration-ticks"))
    {
        help_en = "Select a one-bit DUT clock and enter the number of rising edges to count from simulation start.";
        help_ru = "Укажите однобитный клок схемы и число положительных фронтов с начала симуляции.";
    }
    else if (trigger)
    {
        help_en = "Start VCD recording on a rising edge, falling edge or value match while simulation is running. Use one bit for an edge and a sized Verilog literal for a match.";
        help_ru = "Начинает запись VCD во время симуляции по фронту, спаду или совпадению значения. Для перепада укажите один бит, для сравнения — литерал Verilog с шириной.";
    }
    gchar *full_english = g_strconcat(english, "\n\n", help_en, NULL);
    gchar *full_russian = g_strconcat(russian, "\n\n", help_ru, NULL);
    simulation_dialog_guidance(form->dialog, full_english, full_russian);
    g_free(full_english);
    g_free(full_russian);
    g_free(english);
    g_free(russian);
    gchar **fields = g_strsplit(preset->fields, "|", -1);
    gchar **specs = g_strsplit(preset->pins, "|", -1);
    guint pin_count = g_strv_length(specs);
    GtkWidget *trigger_mode = NULL;
    guint row = 0;
    for (; fields[row]; row++)
    {
        gchar *value = strchr(fields[row], '=');
        *value++ = 0;
        GtkWidget *entry = gtk_entry_new();
        gboolean serial = !strcmp(preset->rule, "uart") || g_str_has_prefix(preset->rule, "usart");
        if (serial && row < 2)
            value = (gchar *)device_serial_port(form, value, row ? 'o' : 'i');
        gtk_entry_set_text(GTK_ENTRY(entry), value);
        if (row < pin_count && *specs[row])
            g_object_set_data_full(G_OBJECT(entry), "port-spec", g_strdup(specs[row]), g_free);
        g_signal_connect(entry, "focus-in-event", G_CALLBACK(device_port_focus), form);
        g_signal_connect(entry, "changed", G_CALLBACK(device_width_changed), form);
        if (row == 0)
            gtk_widget_set_name(entry, "insight-device-first-field");
        if (trigger && row == 0)
        {
            trigger_mode = gtk_combo_box_text_new();
            GtkComboBoxText *choices = GTK_COMBO_BOX_TEXT(trigger_mode);
            gtk_combo_box_text_append(choices, "rise", insight_tr("Rising edge (0 → 1)"));
            gtk_combo_box_text_append(choices, "fall", insight_tr("Falling edge (1 → 0)"));
            gtk_combo_box_text_append(choices, "match", insight_tr("Value match"));
            gtk_combo_box_set_active(GTK_COMBO_BOX(trigger_mode), 0);
            gtk_grid_attach(GTK_GRID(form->grid), entry, 2, row, 1, 1);
            gtk_widget_set_no_show_all(entry, TRUE);
            device_field(form, insight_tr(fields[row]), trigger_mode, row);
            g_object_bind_property(trigger_mode, "active-id", entry, "text",
                                   G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
            g_ptr_array_add(form->entries, entry);
        }
        else if (row == 1 && !strcmp(preset->rule, "button"))
        {
            GtkWidget *polarity = gtk_combo_box_text_new();
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(polarity), "0 → 1 → 0");
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(polarity), "1 → 0 → 1");
            gtk_combo_box_set_active(GTK_COMBO_BOX(polarity), 0);
            gtk_grid_attach(GTK_GRID(form->grid), entry, 2, row, 1, 1);
            gtk_widget_set_no_show_all(entry, TRUE);
            device_field(form, "Pulse polarity", polarity, row);
            g_signal_connect_object(polarity, "changed", G_CALLBACK(device_button_polarity), entry, 0);
            g_signal_connect_object(entry, "changed", G_CALLBACK(device_button_polarity_sync), polarity, 0);
            g_ptr_array_add(form->entries, entry);
        }
        else
        {
            g_ptr_array_add(form->entries, device_field(form, insight_tr(fields[row]), entry, row));
            if (trigger && row == 2)
            {
                device_trigger_match(GTK_COMBO_BOX(trigger_mode), entry);
                g_signal_connect_object(trigger_mode, "changed", G_CALLBACK(device_trigger_match), entry, 0);
            }
        }
    }
    g_strfreev(fields);
    g_strfreev(specs);
    if (preset->script || preset->image >= 0)
    {
        GtkWidget *chooser =
            gtk_file_chooser_button_new("Choose device file", GTK_FILE_CHOOSER_ACTION_OPEN);
        gtk_widget_set_name(chooser, "insight-device-file");
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), form->directory);
        const gchar *file_label = "Memory image (optional)";
        if (preset->script)
            file_label = "Lua script";
        else if (!strcmp(preset->rule, "hdmi-ddc"))
            file_label = "EDID hex image (optional)";
        form->file = device_field(form, file_label, chooser, row++);
        if (!preset->script)
        {
            GtkWidget *clear = gtk_button_new_with_label("Clear image");
            g_signal_connect_swapped(clear, "clicked", G_CALLBACK(gtk_file_chooser_unselect_all),
                                     chooser);
            gtk_grid_attach(GTK_GRID(form->grid), clear, 1, row++, 1, 1);
        }
    }
    if (preset->script)
    {
        form->reset = device_field(form, "Reset signal (optional, 1 bit)", gtk_entry_new(), row);
        g_object_set_data(G_OBJECT(form->reset), "port-spec", "o1");
        g_signal_connect(form->reset, "focus-in-event", G_CALLBACK(device_port_focus), form);
    }
    device_width_changed(NULL, form);
    gtk_widget_show_all(form->grid);
}
