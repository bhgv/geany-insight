/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Persistent port setup dialog and recipe application.
 * RU: Окно настроек портов, сохранение и применение рецепта.
 */

enum { PORT_SETUP_SAVE = 1, PORT_SETUP_LOAD, PORT_SETUP_NEW };

/* EN: Collect module names declared in source, ignoring comments and strings.
 * RU: Собирает имена модулей из source, пропуская комментарии и строки. */
static GPtrArray *simulation_recipe_modules(const gchar *source)
{
    GPtrArray *modules = g_ptr_array_new_with_free_func(g_free);
    if (!source)
        return modules;
    GRegex *tokens = g_regex_new(
        "//[^\\n]*|/\\*[\\s\\S]*?\\*/|\"(?:\\\\.|[^\"\\\\])*\"|"
        "\\bmodule\\s+(?:(?:automatic|static)\\s+)?([A-Za-z_][A-Za-z0-9_$]*)", 0, 0, NULL);
    GMatchInfo *match;
    g_regex_match(tokens, source, 0, &match);
    while (g_match_info_matches(match))
    {
        gchar *name = g_match_info_fetch(match, 1);
        gboolean duplicate = FALSE;
        for (guint i = 0; i < modules->len; ++i)
            duplicate |= g_str_equal(modules->pdata[i], name);
        if (*name && !duplicate)
            g_ptr_array_add(modules, name);
        else
            g_free(name);
        g_match_info_next(match, NULL);
    }
    g_match_info_free(match);
    g_regex_unref(tokens);
    return modules;
}

/* EN: Find the module declaration in buffer, returning its range and owned name.
 * RU: Находит объявление модуля в buffer, возвращая диапазон и имя. */
static gchar *simulation_recipe_module_line(GtkTextBuffer *buffer, GtkTextIter *first, GtkTextIter *last)
{
    gtk_text_buffer_get_start_iter(buffer, first);
    do
    {
        *last = *first;
        if (!gtk_text_iter_ends_line(last))
            gtk_text_iter_forward_to_line_end(last);
        gchar *line = gtk_text_buffer_get_text(buffer, first, last, FALSE);
        gchar **args = NULL;
        gint argc;
        gchar *name = NULL;
        if (g_shell_parse_argv(line, &argc, &args, NULL) && argc == 2 && !strcmp(args[0], "module"))
            name = g_strdup(args[1]);
        g_strfreev(args);
        g_free(line);
        if (name)
            return name;
    }
    while (gtk_text_iter_forward_line(first));
    return NULL;
}

/* EN: Reflect the module in buffer in combo without changing the recipe.
 * RU: Показывает модуль из buffer в combo без изменения рецепта. */
static void simulation_module_sync(GtkTextBuffer *buffer, GtkComboBox *combo)
{
    GtkTextIter first, last;
    gchar *name = simulation_recipe_module_line(buffer, &first, &last);
    g_object_set_data(G_OBJECT(combo), "module-sync", GINT_TO_POINTER(1));
    if (!name || !gtk_combo_box_set_active_id(combo, name))
        gtk_combo_box_set_active(combo, 0);
    g_object_set_data(G_OBJECT(combo), "module-sync", NULL);
    g_free(name);
}

/* EN: Apply the selected module from combo to buffer; buffer changes refresh the ports.
 * RU: Записывает выбранный в combo модуль в buffer, после чего обновляются порты. */
static void simulation_module_selected(GtkComboBox *combo, GtkTextBuffer *buffer)
{
    if (g_object_get_data(G_OBJECT(combo), "module-sync"))
        return;
    const gchar *name = gtk_combo_box_get_active_id(combo);
    if (!name || !*name)
        return;
    GtkTextIter first, last;
    gchar *old = simulation_recipe_module_line(buffer, &first, &last);
    if (g_strcmp0(old, name))
    {
        gchar *line = g_strdup_printf("module %s", name);
        if (old)
            gtk_text_buffer_delete(buffer, &first, &last);
        else
        {
            gtk_text_buffer_get_start_iter(buffer, &first);
            gchar *with_newline = g_strconcat(line, "\n", NULL);
            g_free(line);
            line = with_newline;
        }
        gtk_text_buffer_insert(buffer, &first, line, -1);
        g_free(line);
    }
    g_free(old);
}

/* EN: Rebuild the device list store from supported rules in buffer.
 * RU: Обновляет список устройств store по поддерживаемым правилам buffer. */
static void simulation_device_list_refresh(GtkTextBuffer *buffer, GtkListStore *store)
{
    gtk_list_store_clear(store);
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    gchar *recipe = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
    gchar **lines = g_strsplit(recipe, "\n", -1);
    for (guint line = 0; lines[line]; ++line)
    {
        gchar **args = NULL;
        if (!g_shell_parse_argv(lines[line], NULL, &args, NULL))
            continue;
        for (guint i = 0; i < G_N_ELEMENTS(device_presets); ++i)
        {
            if (strcmp(args[0], device_presets[i].rule))
                continue;
            gtk_list_store_insert_with_values(store, NULL, -1, 0, device_presets[i].title,
                                              1, lines[line], 2, line,
                                              3, device_kind_names[device_presets[i].icon],
                                              4, device_bus_names[device_bus(&device_presets[i])], -1);
            break;
        }
        g_strfreev(args);
    }
    g_strfreev(lines);
    g_free(recipe);
}

/* EN: Edit the rule at path in tree via wizard. column is unused.
 * RU: Редактирует правило по path в tree через wizard. column не используется. */
static void simulation_device_list_edit(GtkTreeView *tree, GtkTreePath *path,
                                        GtkTreeViewColumn *column, GtkWidget *wizard)
{
    (void)column;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter(model, &row, path))
        return;
    guint line;
    gtk_tree_model_get(model, &row, 2, &line, -1);
    GtkTextBuffer *buffer = g_object_get_data(G_OBJECT(wizard), "device-recipe");
    GtkTextIter first;
    gtk_text_buffer_get_iter_at_line(buffer, &first, line);
    GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, &first, TRUE);
    g_object_set_data(G_OBJECT(wizard), "edit-rule", mark);
    gtk_button_clicked(GTK_BUTTON(wizard));
    g_object_set_data(G_OBJECT(wizard), "edit-rule", NULL);
    gtk_text_buffer_delete_mark(buffer, mark);
}

/* EN: Remove the rule anchored by the mark on item from its recipe buffer.
 * RU: Удаляет из рецепта правило, отмеченное меткой в item. */
static void simulation_device_remove(GtkMenuItem *item, GtkTextBuffer *buffer)
{
    GtkTextMark *mark = g_object_get_data(G_OBJECT(item), "device-mark");
    GtkTextIter first, last;
    gtk_text_buffer_get_iter_at_mark(buffer, &first, mark);
    last = first;
    if (!gtk_text_iter_ends_line(&last))
        gtk_text_iter_forward_to_line_end(&last);
    if (!gtk_text_iter_is_end(&last))
        gtk_text_iter_forward_char(&last);
    gtk_text_buffer_delete(buffer, &first, &last);
}

/* EN: Delete mark when its menu item is disposed.
 * RU: Удаляет mark при уничтожении пункта меню. */
static void simulation_device_remove_mark(gpointer data)
{
    GtkTextMark *mark = data;
    GtkTextBuffer *buffer = gtk_text_mark_get_buffer(mark);
    if (buffer)
        gtk_text_buffer_delete_mark(buffer, mark);
    g_object_unref(mark);
}

/* EN: Open the removal menu for the device under event in tree.
 * RU: Открывает меню удаления устройства под event в tree. */
static gboolean simulation_device_menu(GtkTreeView *tree, GdkEventButton *event, GtkTextBuffer *buffer)
{
    GtkTreePath *path;
    if (event->button != 3 ||
        !gtk_tree_view_get_path_at_pos(tree, event->x, event->y, &path, NULL, NULL, NULL))
        return FALSE;

    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter row;
    gboolean found = gtk_tree_model_get_iter(model, &row, path);
    gtk_tree_path_free(path);
    if (!found)
        return FALSE;

    guint line;
    gtk_tree_model_get(model, &row, 2, &line, -1);
    GtkTextIter first;
    gtk_text_buffer_get_iter_at_line(buffer, &first, line);
    GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, &first, TRUE);
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *remove = gtk_menu_item_new_with_label(insight_tr("Remove device"));
    g_object_set_data_full(G_OBJECT(remove), "device-mark", g_object_ref(mark),
                           simulation_device_remove_mark);
    g_signal_connect_object(remove, "activate", G_CALLBACK(simulation_device_remove), buffer, 0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
    g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}

/* EN: Choose a device-set path from parent in save or load mode.
 * RU: Выбирает путь набора устройств из parent для сохранения или загрузки. */
static gchar *simulation_recipe_choose(GtkWindow *parent, gboolean save)
{
    GtkWidget *chooser = gtk_file_chooser_dialog_new(
        save ? "Save Device Set" : "Load Device Set", parent,
        save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, save ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT, NULL);
    g_object_ref_sink(chooser);
    if (save)
    {
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), "devices.ports");
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
    }
    gchar *path = insight_waveforms_run_setup_dialog(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT
                    ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)) : NULL;
    gtk_widget_destroy(chooser);
    g_object_unref(chooser);
    return path;
}

/* EN: Recognize common clock-wizard port names and build a draft rule from pins.
 * RU: Распознаёт типовые порты генератора частоты и собирает черновик правила из pins. */
static gchar *simulation_pll_suggestion(const gchar *module, const gchar *pins)
{
    GRegex *named = g_regex_new("\\.([A-Za-z_][A-Za-z0-9_$]*)\\s*\\(", 0, 0, NULL);
    GMatchInfo *match;
    g_regex_match(named, pins, 0, &match);
    gchar *input = NULL, *reset = NULL, *locked = NULL;
    GPtrArray *outputs = g_ptr_array_new_with_free_func(g_free);
    while (g_match_info_matches(match))
    {
        gchar *pin = g_match_info_fetch(match, 1);
        if ((!strcmp(pin, "clk_in1") || !strcmp(pin, "clk_in") || !strcmp(pin, "inclk0")) && !input)
            input = g_strdup(pin);
        else if ((!strcmp(pin, "reset") || !strcmp(pin, "rst") || !strcmp(pin, "areset")) && !reset)
            reset = g_strdup(pin);
        else if (!strcmp(pin, "locked") && !locked)
            locked = g_strdup(pin);
        else if (g_regex_match_simple("^(clk_out[0-9]*|clk_o[0-9]+|c[0-9]+)$", pin, 0, 0))
            g_ptr_array_add(outputs, g_strdup(pin));
        g_free(pin);
        g_match_info_next(match, NULL);
    }
    g_match_info_free(match);
    g_regex_unref(named);

    gchar *suggestion = NULL;
    if (input && outputs->len && outputs->len <= 5)
    {
        GString *rule = g_string_new(NULL);
        g_string_append_printf(rule, "pll-multi %s %s %s %s 4", module, input,
                               reset ? reset : "-", locked ? locked : "-");
        for (guint i = 0; i < 5; ++i)
            g_string_append_printf(rule, " %s - - -", i < outputs->len ? outputs->pdata[i] : "-");
        suggestion = g_string_free(rule, FALSE);
    }
    g_ptr_array_unref(outputs);
    g_free(input);
    g_free(reset);
    g_free(locked);
    return suggestion;
}

/* EN: Offer PLL and HDMI replacement drafts from active HDL in source, using browser for the wizard.
 * RU: Предлагает черновики замен PLL и HDMI из активного HDL source и открывает мастер через browser. */
static void simulation_replacement_offers(const gchar *source, const gchar *directory,
                                  GtkBox *box, PortBrowser *browser)
{
    if (!source)
        return;
    GRegex *comments = g_regex_new("//[^\\n]*|/\\*[\\s\\S]*?\\*/|\"(?:\\\\.|[^\"\\\\])*\"", 0, 0, NULL);
    gchar *clean = g_regex_replace_literal(comments, source, -1, 0, " ", 0, NULL);
    g_regex_unref(comments);
    GPtrArray *defined = simulation_recipe_modules(clean);
    GHashTable *offered = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GRegex *instances = g_regex_new(
        "\\b(OBUFDS|clk_wiz_[A-Za-z0-9_$]+|pll[A-Za-z0-9_$]*|[A-Za-z_][A-Za-z0-9_$]*_pll[A-Za-z0-9_$]*)"
        "\\s+[A-Za-z_][A-Za-z0-9_$]*\\s*\\(([^;]*)\\)\\s*;", 0, 0, NULL);
    GMatchInfo *match;
    g_regex_match(instances, clean, 0, &match);
    while (g_match_info_matches(match) && g_hash_table_size(offered) < 16)
    {
        gchar *module = g_match_info_fetch(match, 1);
        gboolean exists = g_hash_table_contains(offered, module);
        for (guint i = 0; i < defined->len; ++i)
            exists |= !strcmp(module, defined->pdata[i]);
        if (!exists)
        {
            gchar *pins = g_match_info_fetch(match, 2);
            gboolean differential = !strcmp(module, "OBUFDS");
            gchar *suggestion = differential ? g_strdup("obufds-model OBUFDS")
                                              : simulation_pll_suggestion(module, pins);
            g_free(pins);
            if (suggestion)
            {
                gchar *caption = g_strdup_printf(differential ? insight_tr("Configure TMDS output primitive %s…")
                                                              : insight_tr("Configure missing PLL %s…"), module);
                GtkWidget *button = gtk_button_new_with_label(caption);
                g_free(caption);
                g_object_set_data_full(G_OBJECT(button), "replacement-suggestion", suggestion, g_free);
                g_object_set_data(G_OBJECT(button), "source-directory", (gpointer)directory);
                g_signal_connect(button, "clicked", G_CALLBACK(simulation_device_wizard), browser);
                gtk_box_pack_start(box, button, FALSE, FALSE, 0);
                g_hash_table_add(offered, g_strdup(module));
            }
        }
        g_free(module);
        g_match_info_next(match, NULL);
    }
    g_match_info_free(match);
    g_regex_unref(instances);
    g_hash_table_unref(offered);
    g_ptr_array_unref(defined);
    g_free(clean);
}

/* EN: Edit the recipe saved at path in a child of parent. run supplies the compiler,
 * input sources, and include path. On cancellation, publish preprocessing errors.
 * On acceptance, optional output
 * pointers harness, model_sources and port_images receive the generated configuration.
 * RU: Редактирует рецепт из path в дочернем окне parent. run задаёт компилятор,
 * исходники и путь include. При отмене публикует ошибки препроцессора.
 * При подтверждении необязательные указатели harness,
 * model_sources и port_images получают сгенерированную конфигурацию. */
static gboolean simulation_edit_ports(GtkWindow *parent, const gchar *path, const WaveSimulation *run,
                                      gchar **harness, GPtrArray **model_sources,
                                      GHashTable **port_images)
{
    static GtkWidget *active_setup;
    if (active_setup)
    {
        gtk_window_present(GTK_WINDOW(active_setup));
        return FALSE;
    }
    gchar *include_directory = g_strdup(run->include_dir);
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Port Signals", parent, GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        harness ? "_Start Debugging" : "_Save for Next Run", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_New", PORT_SETUP_NEW);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Load…", PORT_SETUP_LOAD);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "_Save…", PORT_SETUP_SAVE);
    simulation_dialog_tools(
        dialog,
        "Choose a module and add device rules, or leave the recipe empty to run an existing "
        "testbench. Ports shows directions, widths and assignments. Source is copyable. Use the "
        "separate HDL and diagram previews to review the recipe.",
        "Укажите модуль и добавьте правила устройств или оставьте настройки пустыми для "
        "готового тестбенча. В Ports видны направления, ширина и привязки. Из Source можно "
        "копировать исходник. Для проверки откройте просмотр HDL или схему.");
    g_object_ref_sink(dialog);
    active_setup = dialog;
    gchar *source_directory = g_path_get_dirname(run->sources[0]);
    gchar *source_snapshot = port_browser_source(run->sources[0]);
    gchar *preprocess_error = NULL;
    gchar *expanded = port_browser_preprocess(run, &preprocess_error);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 390);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hint = gtk_label_new(
        "Leave empty to run an existing testbench. Otherwise enter one rule per line (times in ns).\n"
        "module NAME   ·   duration NS\n"
        "clock PORT START HIGH LOW   ·   clock-hz PORT START HZ\n"
        "pulse PORT START LENGTH 0|1   ·   value PORT WIDTH EXPR\n"
        "bus PORT WIDTH INTERVAL EXPR   ·   output PORT WIDTH\n"
        "capture-trigger rise|fall SIGNAL   ·   capture-trigger match SIGNAL SIZED_HEX_OR_BINARY\n"
        "button PORT [IDLE [LENGTH_NS]]   ·   switch PORT [INITIAL]\n"
        "slider PORT [WIDTH=8 [INITIAL=0]]\n"
        "scope SIGNAL   ·   scope-xy X Y   ·   scope-xyz X Y Z\n"
        "led PORT [ACTIVE=1]   ·   display PORT [WIDTH=8 [hex|dec|bin]]\n"
        "hex-table PORT [WIDTH=32]\n"
        "live-interval NS   ·   live-input PORT WIDTH INITIAL   ·   monitor PORT WIDTH\n"
        "Memory presets: sram, parallel-psram, sdram, ddr1, ddr2, ddr3, ddr4, ddr5, spi-nor, spi-nand, spi-eeprom, spi-psram, i2c-eeprom.\n"
        "Use a preset alone for standard port names, or list your ports below. '-' uses the default.\n"
        "parallel-psram ADDR DQ CE_N WE_N OE_N LB_N UB_N [ADDRESS_BITS=20]\n"
        "sram ADDR DQ CE_N WE_N OE_N [ADDRESS_BITS=16 [DATA_BITS=8]]\n"
        "sdram CK CKE CS_N RAS_N CAS_N WE_N A BA DQ DQM\n"
        "memory-image ADDR_OR_SPI_CS_N_OR_SDA FILE (before the memory rule)\n"
        "i2c-eeprom SCL SDA [ADDRESS=0x50]\n"
        "spi-controller SCK CS_N MOSI MISO TX_HEX MODE PHASE_NS START_NS\n"
        "i2c-controller SCL SDA ADDRESS TX_HEX_OR_DASH READ_BYTES PHASE_NS START_NS\n"
        "spi-lua [SCK CS_N MOSI MISO] SCRIPT [RESET_N]\n"
        "i2c-lua [SCL SDA ADDRESS] SCRIPT [RESET_N]\n"
        "spi-nor / spi-nand / spi-eeprom / spi-psram SCK CS_N MOSI MISO\n"
        "ddr1 CK CKE CS_N RAS_N CAS_N WE_N A BA DQ DQS DM [RL=2 [WL=1]] (functional x8)\n"
        "ddr2 CK CKE CS_N RAS_N CAS_N WE_N A BA DQ DQS DM [RL=3 [WL=2]] (functional x8)\n"
        "ddr3 CK RESET_N CKE CS_N RAS_N CAS_N WE_N A BA DQ DQS DM [RL=3 [WL=2]] (functional x8)\n"
        "ddr4 CK RESET_N CKE CS_N ACT_N A BA BG DQ DQS DM_N [RL=3 [WL=2]] (functional x8)\n"
        "ddr5 CK RESET_N CS_N CA DQ DQS DM_N [RL=3 [WL=2]] (functional x8 BL16)\n"
        "rgb-display PCLK DE VS R G B WIDTH HEIGHT BITS VS_ACTIVE\n"
        "lvds-display CLK D0 D1 D2 D3_OR_DASH WIDTH HEIGHT BITS jeida|vesa PHASE VS_ACTIVE\n"
        "lvds-dual-display CLK A0 A1 A2 A3_OR_DASH B0 B1 B2 B3_OR_DASH WIDTH HEIGHT BITS jeida|vesa PHASE VS_ACTIVE a-first|b-first\n"
        "hdmi-ddc SCL SDA HPD [CONNECT_NS=0 [HDMI_CLK=auto [REFRESH_HZ=auto]]] (optional EDID image keyed by SDA)\n"
        "hdmi-display CLK D0 D1 D2 WIDTH HEIGHT BITS hdmi|dvi PHASE VS_ACTIVE\n"
        "uart RX TX BAUD [console]\n"
        "usart RX TX XCK rise|fall [console] (DUT clock)\n"
        "usart-clock RX TX XCK PERIOD_NS rise|fall [console]\n"
        "model INSTANCE MODULE SOURCE   ·   model-link INSTANCE DUT_PORT MODEL_PORT WIDTH\n"
        "model-share INSTANCE DUT_PORT MODEL_PORT   ·   model-param / model-value INSTANCE NAME EXPR\n"
        "Use step in bus expressions and $time for simulation time. Quote formulas containing spaces.");
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    GtkWidget *syntax = gtk_expander_new("Text rule reference");
    gtk_container_add(GTK_CONTAINER(syntax), hint);
    gtk_box_pack_start(GTK_BOX(box), syntax, FALSE, FALSE, 4);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GPtrArray *modules = simulation_recipe_modules(expanded ? expanded : source_snapshot);
    if (modules->len == 1)
        g_object_set_data_full(G_OBJECT(buffer), "default-module", g_strdup(modules->pdata[0]), g_free);
    GtkWidget *module_picker = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(module_picker), "", "Choose DUT module…");
    for (guint i = 0; i < modules->len; ++i)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(module_picker), modules->pdata[i], modules->pdata[i]);
    g_ptr_array_unref(modules);
    gtk_box_pack_start(GTK_BOX(box), module_picker, FALSE, FALSE, 4);
    g_signal_connect(module_picker, "changed", G_CALLBACK(simulation_module_selected), buffer);
    g_signal_connect_object(buffer, "changed", G_CALLBACK(simulation_module_sync), module_picker, 0);
    g_object_set_data_full(G_OBJECT(dialog), "verilog-port-recipe", g_object_ref(buffer), g_object_unref);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *wizard = gtk_button_new_with_label("Add Test Device…");
    g_object_set_data(G_OBJECT(wizard), "source-directory", source_directory);
    gtk_box_pack_start(GTK_BOX(actions), wizard, FALSE, FALSE, 0);
    GtkWidget *preview = gtk_button_new_with_label("Preview Testbench…");
    g_object_set_data(G_OBJECT(preview), "source-directory", source_directory);
    g_signal_connect(preview, "clicked", G_CALLBACK(simulation_preview_harness), buffer);
    gtk_box_pack_start(GTK_BOX(actions), preview, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 4);
    GFile *file = g_file_new_for_path(path);
    GFileInputStream *stream = g_file_read(file, NULL, NULL);
    if (stream)
    {
        gchar saved[16385];
        gsize length = 0;
        if (g_input_stream_read_all(G_INPUT_STREAM(stream), saved, sizeof saved, &length, NULL, NULL) &&
            length < sizeof saved && g_utf8_validate(saved, length, NULL))
            gtk_text_buffer_set_text(buffer, saved, length);
        g_object_unref(stream);
    }
    g_object_unref(file);
    simulation_module_sync(buffer, GTK_COMBO_BOX(module_picker));
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    PortBrowser browser = {.source = source_snapshot,
                           .expanded = expanded,
                           .preprocess_error = preprocess_error,
                           .recipe = buffer,
                           .recipe_view = GTK_TEXT_VIEW(view)};
    GtkWidget *panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *recipe_tabs = gtk_notebook_new();
    GtkListStore *devices = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT,
                                                G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *device_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(devices));
    gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(device_list), TRUE);
    const gchar *headings[] = {"Device", "Type", "Bus", "Rule"};
    const gint columns[] = {0, 3, 4, 1};
    for (guint i = 0; i < G_N_ELEMENTS(columns); ++i)
    {
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            headings[i], gtk_cell_renderer_text_new(), "text", columns[i], NULL);
        gtk_tree_view_column_set_sort_column_id(column, columns[i]);
        gtk_tree_view_append_column(GTK_TREE_VIEW(device_list), column);
    }
    GtkWidget *device_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(device_scroll), device_list);
    gtk_notebook_append_page(GTK_NOTEBOOK(recipe_tabs), device_scroll, gtk_label_new("Devices"));
    gtk_notebook_append_page(GTK_NOTEBOOK(recipe_tabs), scroll, gtk_label_new("Recipe"));
    simulation_device_list_refresh(buffer, devices);
    g_signal_connect_object(buffer, "changed", G_CALLBACK(simulation_device_list_refresh), devices, 0);
    g_object_unref(devices);
    g_object_set_data(G_OBJECT(wizard), "device-recipe", buffer);
    g_signal_connect(device_list, "row-activated", G_CALLBACK(simulation_device_list_edit), wizard);
    g_signal_connect(device_list, "button-press-event", G_CALLBACK(simulation_device_menu), buffer);
    gtk_paned_pack1(GTK_PANED(panes), recipe_tabs, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(panes), port_browser_widget(&browser), TRUE, FALSE);
    simulation_replacement_offers(expanded, source_directory, GTK_BOX(box), &browser);
    gtk_paned_set_position(GTK_PANED(panes), 320);
    GtkWidget *diagram = gtk_button_new_with_label("Preview Diagram…");
    g_object_set_data(G_OBJECT(diagram), "source-directory", source_directory);
    g_signal_connect(diagram, "clicked", G_CALLBACK(simulation_preview_diagram), &browser);
    gtk_box_pack_start(GTK_BOX(actions), diagram, FALSE, FALSE, 0);
    g_signal_connect(wizard, "clicked", G_CALLBACK(simulation_device_wizard), &browser);
    g_object_ref(buffer);
    gulong changed = g_signal_connect(buffer, "changed", G_CALLBACK(port_browser_refresh), &browser);
    gtk_box_pack_start(GTK_BOX(box), panes, TRUE, TRUE, 0);
    GtkWidget *status =
        gtk_label_new(harness ? "" : "Saved changes apply the next time debugging starts.");
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 6);
    gtk_widget_show_all(dialog);
    gboolean accepted = FALSE;
    gint response;
    while ((response = insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog))) == GTK_RESPONSE_ACCEPT ||
           response == PORT_SETUP_SAVE || response == PORT_SETUP_LOAD || response == PORT_SETUP_NEW)
    {
        if (response == PORT_SETUP_NEW)
        {
            gtk_text_buffer_set_text(buffer, "", 0);
            gtk_label_set_text(GTK_LABEL(status), "New device set.");
            continue;
        }
        gchar *set_path = response == GTK_RESPONSE_ACCEPT ? NULL :
            simulation_recipe_choose(GTK_WINDOW(dialog), response == PORT_SETUP_SAVE);
        if (response != GTK_RESPONSE_ACCEPT && !set_path)
            continue;
        if (response == PORT_SETUP_LOAD)
        {
            gchar *loaded = NULL;
            gsize length = 0;
            gchar *directory = g_path_get_dirname(path);
            if (g_file_get_contents(set_path, &loaded, &length, NULL) && length < 16385 &&
                g_utf8_validate(loaded, length, NULL) && g_mkdir_with_parents(directory, 0700) == 0 &&
                g_file_set_contents(path, loaded, length, NULL))
            {
                gtk_text_buffer_set_text(buffer, loaded, length);
                gtk_label_set_text(GTK_LABEL(status), "Device set loaded and saved for this project.");
            }
            else
                gtk_label_set_text(GTK_LABEL(status), "Cannot load or remember this device set.");
            g_free(directory);
            g_free(loaded);
            g_free(set_path);
            continue;
        }
        simulation_recipe_module(buffer);
        GtkTextIter first, last;
        gtk_text_buffer_get_bounds(buffer, &first, &last);
        gchar *recipe = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        GError *error = NULL;
        GPtrArray *sources = g_ptr_array_new_with_free_func(g_free);
        GHashTable *images =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
        gchar *generated = simulation_setup_harness(recipe, source_directory, sources, images, &error);
        gchar *directory = g_path_get_dirname(path);
        if (!error && strlen(recipe) < 16385 &&
            (!set_path || g_file_set_contents(set_path, recipe, -1, &error)) &&
            g_mkdir_with_parents(directory, 0700) == 0 &&
            g_file_set_contents(path, recipe, -1, &error))
        {
            accepted = response == GTK_RESPONSE_ACCEPT;
            if (!accepted)
                gtk_label_set_text(GTK_LABEL(status), "Device setup saved.");
            if (accepted && harness)
                *harness = g_steal_pointer(&generated);
            if (accepted && model_sources)
                *model_sources = g_steal_pointer(&sources);
            if (accepted && port_images)
                *port_images = g_steal_pointer(&images);
        }
        else
            gtk_label_set_text(GTK_LABEL(status),
                               error ? error->message : "Cannot save the port recipe.");
        g_free(directory);
        g_free(set_path);
        g_clear_pointer(&sources, g_ptr_array_unref);
        g_clear_pointer(&images, g_hash_table_unref);
        g_free(generated);
        g_free(recipe);
        g_clear_error(&error);
        if (accepted)
            break;
    }
    g_signal_handler_disconnect(buffer, changed);
    g_object_unref(buffer);
    active_setup = NULL;
    g_free(source_snapshot);
    g_free(expanded);
    g_free(source_directory);
    g_clear_pointer(&browser.ports, g_hash_table_unref);
    g_free(browser.module);
    g_clear_pointer(&browser.query_regex, g_regex_unref);
    g_free(browser.folded_query);
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    if (!accepted && preprocess_error)
        msgwin_debugger_report(preprocess_error, include_directory, NULL);
    g_free(include_directory);
    g_free(preprocess_error);
    return accepted;
}

/* EN: Reopen saved port settings from button for the running simulation in data.
 * RU: Повторно открывает настройки портов по кнопке button для сеанса из data. */
static void simulation_port_settings(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    gchar *path = g_strdup(run->port_settings);
    simulation_edit_ports(GTK_WINDOW(run->dialog), path, run, NULL, NULL, NULL);
    g_free(path);
}
