/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Device form construction and validated rule insertion.
 * RU: Создание формы устройства и вставка проверенных правил.
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
        "The table displays hexadecimal bytes, including unknown and high-impedance states."};
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
        "Таблица показывает байты в hex, включая неизвестные и высокоимпедансные состояния."};
    gchar *full_english = g_strconcat(english, "\n\n", details_en[preset->icon], NULL);
    gchar *full_russian = g_strconcat(russian, "\n\n", details_ru[preset->icon], NULL);
    simulation_dialog_guidance(form->dialog, full_english, full_russian);
    g_free(full_english);
    g_free(full_russian);
    g_free(english);
    g_free(russian);
    gchar **fields = g_strsplit(preset->fields, "|", -1);
    gchar **specs = g_strsplit(preset->pins, "|", -1);
    guint pin_count = g_strv_length(specs);
    guint row = 0;
    for (; fields[row]; row++)
    {
        gchar *value = strchr(fields[row], '=');
        *value++ = 0;
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), value);
        if (row < pin_count)
            g_object_set_data_full(G_OBJECT(entry), "port-spec", g_strdup(specs[row]), g_free);
        g_signal_connect(entry, "focus-in-event", G_CALLBACK(device_port_focus), form);
        g_signal_connect(entry, "changed", G_CALLBACK(device_width_changed), form);
        if (row == 0)
            gtk_widget_set_name(entry, "insight-device-first-field");
        g_ptr_array_add(form->entries, device_field(form, fields[row], entry, row));
    }
    g_strfreev(fields);
    g_strfreev(specs);
    if (preset->script || preset->image >= 0)
    {
        GtkWidget *chooser =
            gtk_file_chooser_button_new("Choose device file", GTK_FILE_CHOOSER_ACTION_OPEN);
        gtk_widget_set_name(chooser, "insight-device-file");
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), form->directory);
        form->file =
            const gchar *file_label = preset->script ? "Lua script" :
                !strcmp(preset->rule, "hdmi-ddc") ? "EDID hex image (optional)" : "Memory image (optional)";
            device_field(form, file_label, chooser, row++);
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

/* EN: Append value to rule as one shell-quoted token.
 * RU: Добавляет value в rule одним аргументом в кавычках. */
static void device_argument(GString *rule, const gchar *value)
{
    gchar *quoted = g_shell_quote(value);
    g_string_append_printf(rule, " %s", quoted);
    g_free(quoted);
}

/* EN: Copy search and sort settings from source into the built destination view.
 * Compatibility is remembered even when the destination has no device field.
 * RU: Копирует поиск и сортировку из source в готовый список destination.
 * Фильтр совместимости сохраняется и для списка без поля устройства. */
static void port_browser_copy_view(PortBrowser *destination, const PortBrowser *source)
{
    destination->prefer_compatible =
        source->compatible ? gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(source->compatible))
                           : source->prefer_compatible;
    gtk_combo_box_set_active(GTK_COMBO_BOX(destination->search_target),
                             gtk_combo_box_get_active(GTK_COMBO_BOX(source->search_target)));
    gtk_combo_box_set_active(GTK_COMBO_BOX(destination->occupancy),
                             gtk_combo_box_get_active(GTK_COMBO_BOX(source->occupancy)));
    gtk_combo_box_set_active(GTK_COMBO_BOX(destination->sort_choice),
                             gtk_combo_box_get_active(GTK_COMBO_BOX(source->sort_choice)));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(destination->reverse),
                                 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(source->reverse)));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(destination->regex_toggle),
                                 gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(source->regex_toggle)));
    gtk_entry_set_text(GTK_ENTRY(destination->search), gtk_entry_get_text(GTK_ENTRY(source->search)));
    if (destination->compatible)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(destination->compatible),
                                     destination->prefer_compatible);
}

/* EN: Open the device form from button using the parent port browser in data.
 * Preserve list preferences and append a validated rule on confirmation.
 * RU: Открывает форму по button, используя родительский список из data.
 * Сохраняет настройки списка и добавляет проверенное правило при подтверждении. */
static void simulation_device_wizard(GtkButton *button, gpointer data)
{
    PortBrowser *parent_browser = data;
    GtkTextBuffer *buffer = parent_browser->recipe;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    DeviceForm form = {.entries = g_ptr_array_new(),
                       .directory = g_object_get_data(G_OBJECT(button), "source-directory")};
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons("Add Test Device", parent, GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
                                    GTK_RESPONSE_CANCEL, "_Insert Rule", GTK_RESPONSE_ACCEPT, NULL);
    simulation_dialog_tools(
        dialog,
        "Find a device by name, regular expression or type. Pick a field, then double-click a "
        "compatible port or choose a slice. Insert Rule adds the device to Port Signals.",
        "Найдите устройство по имени, регулярному выражению или типу. Выберите поле и дважды "
        "щёлкните по порту либо укажите срез. Insert Rule добавляет устройство в Port Signals.");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 960, 520);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    g_object_ref_sink(dialog);
    form.dialog = dialog;
    DeviceCatalog catalog = {.preferred = parent_browser->last_device};
    device_catalog_widget(&catalog, box);
    GtkWidget *combo = catalog.combo;
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    form.grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(form.grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(form.grid), 4);
    gtk_container_add(GTK_CONTAINER(scroll), form.grid);
    PortBrowser browser = {.source = parent_browser->source,
                           .expanded = parent_browser->expanded,
                           .preprocess_error = parent_browser->preprocess_error,
                           .recipe = buffer,
                           .choosing = TRUE};
    GtkWidget *panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(panes), scroll, TRUE, FALSE);
    GtkWidget *browser_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(browser_box), port_browser_widget(&browser), TRUE, TRUE, 0);
    GtkWidget *choose_slice = gtk_button_new_with_label("Choose Slice…");
    g_signal_connect(choose_slice, "clicked", G_CALLBACK(device_port_choose_slice), &form);
    gtk_box_pack_start(GTK_BOX(browser_box), choose_slice, FALSE, FALSE, 0);
    gtk_paned_pack2(GTK_PANED(panes), browser_box, TRUE, FALSE);
    port_browser_copy_view(&browser, parent_browser);
    form.browser = &browser;
    g_signal_connect(browser.tree, "row-activated", G_CALLBACK(device_port_activate), &form);
    gtk_paned_set_position(GTK_PANED(panes), 420);
    gtk_box_pack_start(GTK_BOX(box), panes, TRUE, TRUE, 0);
    GtkWidget *status = gtk_label_new(NULL);
    form.status = status;
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 6);
    catalog.selection_changed =
        g_signal_connect(combo, "changed", G_CALLBACK(simulation_device_type), &form);
    device_catalog_filter(NULL, &catalog);
    gtk_widget_show_all(dialog);
    gint response;
    while ((response = insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog))) == GTK_RESPONSE_ACCEPT)
    {
        gint selected = device_catalog_selected(GTK_COMBO_BOX(combo));
        if (selected < 0)
            continue;
        const DevicePreset *preset = &device_presets[selected];
        gchar *native = form.file ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(form.file)) : NULL;
        gchar *file = native ? g_filename_to_utf8(native, -1, NULL, NULL, NULL) : NULL;
        gboolean valid =
            (!native || file) && (!preset->script || file) && (!file || !strpbrk(file, "\r\n"));
        GString *rule = g_string_new(NULL);
        GPtrArray *arguments = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; i < form.entries->len; i++)
        {
            GtkWidget *entry = form.entries->pdata[i];
            const gchar *value = gtk_entry_get_text(GTK_ENTRY(entry));
            gchar *argument = device_port_argument(&form, entry, value, i, rule);
            valid = valid && argument && *value && !strpbrk(value, "\r\n");
            g_ptr_array_add(arguments, argument ? argument : g_strdup(""));
        }
        if (file && preset->image >= 0)
        {
            g_string_append(rule, "memory-image");
            device_argument(rule, arguments->pdata[preset->image]);
            device_argument(rule, file);
            g_string_append_c(rule, '\n');
        }
        g_string_append(rule, preset->rule);
        for (guint i = 0; i < arguments->len; i++)
            device_argument(rule, arguments->pdata[i]);
        g_ptr_array_unref(arguments);
        if (preset->script && file)
            device_argument(rule, file);
        const gchar *reset = form.reset ? gtk_entry_get_text(GTK_ENTRY(form.reset)) : "";
        if (*reset)
        {
            valid = valid && simulation_capture_signal(reset);
            device_argument(rule, reset);
        }
        GError *error = NULL;
        if (valid)
        {
            gchar *recipe = !strcmp(preset->rule, "module")
                                ? g_strdup(rule->str)
                                : g_strconcat("module geany_device_check\n", rule->str, NULL);
            GPtrArray *sources = g_ptr_array_new_with_free_func(g_free);
            GHashTable *images =
                g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
            gchar *generated = simulation_port_harness(recipe, form.directory, sources, images, &error);
            valid = generated != NULL;
            g_free(generated);
            g_hash_table_unref(images);
            g_ptr_array_unref(sources);
            g_free(recipe);
        }
        g_free(native);
        g_free(file);
        if (valid)
        {
            GtkTextIter end, previous;
            gtk_text_buffer_get_end_iter(buffer, &end);
            previous = end;
            gtk_text_buffer_begin_user_action(buffer);
            if (gtk_text_iter_backward_char(&previous) && gtk_text_iter_get_char(&previous) != '\n')
                gtk_text_buffer_insert(buffer, &end, "\n", 1);
            gtk_text_buffer_insert(buffer, &end, rule->str, rule->len);
            gtk_text_buffer_insert(buffer, &end, "\n", 1);
            gtk_text_buffer_end_user_action(buffer);
        }
        else
            gtk_label_set_text(GTK_LABEL(status),
                               error
                                   ? error->message
                                   : "Check the required fields and select a Lua script when requested.");
        g_clear_error(&error);
        g_string_free(rule, TRUE);
        if (valid)
            break;
    }
    if (response != GTK_RESPONSE_NONE)
    {
        gint last_device = device_catalog_selected(GTK_COMBO_BOX(combo));
        if (last_device >= 0)
            parent_browser->last_device = last_device;
        port_browser_copy_view(parent_browser, &browser);
    }
    g_clear_pointer(&browser.ports, g_hash_table_unref);
    g_free(browser.module);
    g_clear_pointer(&browser.query_regex, g_regex_unref);
    g_free(browser.folded_query);
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    g_ptr_array_unref(form.entries);
}
