/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Recipe and device-list actions.
 * RU: Действия над рецептом и списком устройств.
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

/* EN: Edit the existing timing rule selected by button, or insert it in browser's recipe.
 * RU: Редактирует выбранное button правило времени или добавляет его в рецепт browser. */
static void simulation_edit_timing(GtkButton *button, gpointer data)
{
    PortBrowser *browser = data;
    GtkTextBuffer *buffer = browser->recipe;
    const gchar *rule = g_object_get_data(G_OBJECT(button), "timing-rule");
    GtkTextIter first;
    GtkTextMark *mark = NULL;
    gtk_text_buffer_get_start_iter(buffer, &first);
    do
    {
        GtkTextIter last = first;
        if (!gtk_text_iter_ends_line(&last))
            gtk_text_iter_forward_to_line_end(&last);
        gchar *line = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        gchar **args = NULL;
        gboolean matches = g_shell_parse_argv(line, NULL, &args, NULL) &&
                           !g_strcmp0(args[0], rule);
        g_strfreev(args);
        g_free(line);
        if (matches)
        {
            if (mark)
                gtk_text_buffer_move_mark(buffer, mark, &first);
            else
                mark = gtk_text_buffer_create_mark(buffer, NULL, &first, TRUE);
        }
    } while (gtk_text_iter_forward_line(&first));

    g_object_set_data(G_OBJECT(button), "edit-rule", mark);
    simulation_device_wizard(button, browser);
    g_object_set_data(G_OBJECT(button), "edit-rule", NULL);
    if (mark)
        gtk_text_buffer_delete_mark(buffer, mark);
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
