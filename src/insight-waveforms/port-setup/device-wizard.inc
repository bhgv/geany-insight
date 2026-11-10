/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Device wizard and recipe updates.
 * RU: Мастер устройств и обновление рецепта.
 */

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

/* EN: Insert the inferred DUT into a nonempty recipe if buffer has no module rule.
 * RU: Добавляет найденный DUT в непустой рецепт buffer без правила module. */
static void simulation_recipe_module(GtkTextBuffer *buffer)
{
    const gchar *module = g_object_get_data(G_OBJECT(buffer), "default-module");
    if (!module)
        return;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *recipe = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gchar **lines = g_strsplit(recipe, "\n", -1);
    gboolean rules = FALSE, explicit_module = FALSE;
    for (guint i = 0; lines[i]; ++i)
    {
        const gchar *line = g_strstrip(lines[i]);
        rules |= *line && *line != '#';
        explicit_module |= g_str_has_prefix(line, "module ") || g_str_has_prefix(line, "module\t");
    }
    if (rules && !explicit_module)
    {
        gchar *header = g_strdup_printf("module %s\n", module);
        gtk_text_buffer_insert(buffer, &start, header, -1);
        g_free(header);
    }
    g_strfreev(lines);
    g_free(recipe);
}

/* EN: Open the device form from button using the parent port browser in data.
 * Preserve list preferences and append a validated rule on confirmation.
 * RU: Открывает форму по button, используя родительский список из data.
 * Сохраняет настройки списка и добавляет проверенное правило при подтверждении. */
static void simulation_device_wizard(GtkButton *button, gpointer data)
{
    PortBrowser *parent_browser = data;
    GtkTextBuffer *buffer = parent_browser->recipe;
    const gchar *fixed_rule = g_object_get_data(G_OBJECT(button), "timing-rule");
    GtkTextMark *edit_mark = g_object_get_data(G_OBJECT(button), "edit-rule");
    gchar **saved_args = NULL;
    gint saved_count = 0;
    if (edit_mark)
    {
        GtkTextIter first, last;
        gtk_text_buffer_get_iter_at_mark(buffer, &first, edit_mark);
        last = first;
        gtk_text_iter_forward_to_line_end(&last);
        gchar *line = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        g_shell_parse_argv(line, &saved_count, &saved_args, NULL);
        g_free(line);
    }
    else
    {
        const gchar *suggestion = g_object_get_data(G_OBJECT(button), "replacement-suggestion");
        if (suggestion)
            g_shell_parse_argv(suggestion, &saved_count, &saved_args, NULL);
    }
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    DeviceForm form = {.entries = g_ptr_array_new(),
                       .directory = g_object_get_data(G_OBJECT(button), "source-directory")};
    const gchar *title = edit_mark ? "Edit Test Device" : "Add Test Device";
    if (fixed_rule)
        for (guint i = 0; i < G_N_ELEMENTS(device_presets); i++)
            if (!strcmp(fixed_rule, device_presets[i].rule))
            {
                title = device_presets[i].title;
                break;
            }
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(insight_tr(title), parent,
                                    GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
                                    GTK_RESPONSE_CANCEL, edit_mark ? "_Apply" : "_Insert Rule", GTK_RESPONSE_ACCEPT, NULL);
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
    GtkWidget *catalog_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(box), catalog_box, FALSE, FALSE, 0);
    device_catalog_widget(&catalog, catalog_box);
    if (fixed_rule)
    {
        gtk_widget_set_no_show_all(catalog_box, TRUE);
        gtk_widget_hide(catalog_box);
        GtkWidget *heading = gtk_label_new(insight_tr(title));
        gtk_label_set_xalign(GTK_LABEL(heading), 0);
        gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 4);
        GtkWidget *summary = gtk_label_new(insight_tr(
            !strcmp(fixed_rule, "duration") ? "Set the time from simulation start in nanoseconds."
            : !strcmp(fixed_rule, "duration-ticks") ? "Choose the clock and count its rising edges."
            : "Choose the signal and the event that starts waveform recording."));
        gtk_label_set_xalign(GTK_LABEL(summary), 0);
        gtk_label_set_line_wrap(GTK_LABEL(summary), TRUE);
        gtk_box_pack_start(GTK_BOX(box), summary, FALSE, FALSE, 4);
    }
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
    if (fixed_rule)
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), fixed_rule);
    if (saved_args && gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), saved_args[0]))
    {
        guint count = MIN(form.entries->len, (guint)(saved_count - 1));
        for (guint i = 0; i < count; ++i)
            gtk_entry_set_text(GTK_ENTRY(form.entries->pdata[i]), saved_args[i + 1]);
        gint preset = device_catalog_selected(GTK_COMBO_BOX(combo));
        guint next = form.entries->len + 1;
        if (preset >= 0 && device_presets[preset].script && form.file && next < (guint)saved_count)
        {
            gchar *native = g_filename_from_utf8(saved_args[next++], -1, NULL, NULL, NULL);
            if (native)
            {
                gchar *path = insight_canonicalize_filename(native, form.directory);
                gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(form.file), path);
                g_free(path);
                g_free(native);
            }
            if (form.reset && next < (guint)saved_count)
                gtk_entry_set_text(GTK_ENTRY(form.reset), saved_args[next]);
        }
    }
    g_strfreev(saved_args);
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
            if (i == 2 && !strcmp(preset->rule, "capture-trigger") &&
                strcmp(gtk_entry_get_text(GTK_ENTRY(form.entries->pdata[0])), "match"))
                continue;
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
            if (edit_mark)
            {
                gtk_text_buffer_get_iter_at_mark(buffer, &end, edit_mark);
                previous = end;
                gtk_text_iter_forward_to_line_end(&previous);
                if (!gtk_text_iter_is_end(&previous))
                    gtk_text_iter_forward_char(&previous);
                gtk_text_buffer_delete(buffer, &end, &previous);
            }
            else if (gtk_text_iter_backward_char(&previous) && gtk_text_iter_get_char(&previous) != '\n')
                gtk_text_buffer_insert(buffer, &end, "\n", 1);
            gtk_text_buffer_insert(buffer, &end, rule->str, rule->len);
            gtk_text_buffer_insert(buffer, &end, "\n", 1);
            simulation_recipe_module(buffer);
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
        if (!fixed_rule && last_device >= 0)
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
