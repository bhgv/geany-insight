/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Widget lookup and common test-device controls.
 * RU: Поиск виджетов и общие действия тестовых устройств. */

/* Find a visible top-level window by its editor title. */
/* Ищет видимое окно верхнего уровня по заголовку редактора. */
static GtkWidget *find_window(const gchar *title)
{
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *found = NULL;
    for (GList *entry = windows; entry && !found; entry = entry->next)
    {
        GtkWidget *window = entry->data;
        if (gtk_widget_get_visible(window) &&
            !g_strcmp0(gtk_window_get_title(GTK_WINDOW(window)), title))
            found = window;
    }
    g_list_free(windows);
    return found;
}

/* Find a control by caption or by its unique role in the dialog. */
/* Ищет элемент по подписи или по его однозначной роли в диалоге. */
static GtkWidget *find_control(GtkWidget *widget, const gchar *key)
{
    if (!g_strcmp0(gtk_widget_get_name(widget), key))
        return widget;
    if (GTK_IS_ENTRY(widget) && !g_strcmp0(gtk_entry_get_placeholder_text(GTK_ENTRY(widget)), key))
        return widget;
    if (GTK_IS_BUTTON(widget) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), key))
        return widget;
    if (GTK_IS_LABEL(widget) && !g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)), key))
        return widget;
    if (GTK_IS_TREE_VIEW(widget))
    {
        GtkTreeViewColumn *column = gtk_tree_view_get_column(GTK_TREE_VIEW(widget), 0);
        if (column && !g_strcmp0(gtk_tree_view_column_get_title(column), key))
            return widget;
    }
    if (GTK_IS_STATUSBAR(widget) && !strcmp(key, "statusbar"))
        return widget;
    if (GTK_IS_NOTEBOOK(widget) && !strcmp(key, "notebook"))
        return widget;
    if (GTK_IS_TEXT_VIEW(widget) && !strcmp(key, "console"))
        return widget;
    if (GTK_IS_COMBO_BOX_TEXT(widget) && !strcmp(key, "tracks"))
        return widget;
    if (GTK_IS_SPIN_BUTTON(widget) && !strcmp(key, "source-line") &&
        gtk_adjustment_get_upper(gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(widget))) == G_MAXINT)
        return widget;
    if (GTK_IS_MENU_ITEM(widget))
    {
        if (!g_strcmp0(gtk_widget_get_name(widget), key))
            return widget;
        GtkMenuItem *item = GTK_MENU_ITEM(widget);
        if (!g_strcmp0(gtk_menu_item_get_label(item), key))
            return widget;
        GtkWidget *submenu = gtk_menu_item_get_submenu(item);
        GtkWidget *found = submenu ? find_control(submenu, key) : NULL;
        if (found)
            return found;
    }
    if (!GTK_IS_CONTAINER(widget))
        return NULL;

    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *found = NULL;
    for (GList *entry = children; entry && !found; entry = entry->next)
        found = find_control(entry->data, key);
    g_list_free(children);
    return found;
}

/* Activate a button only when the user could click it. */
/* Нажимает кнопку, только когда она доступна пользователю. */
static gboolean click(GtkWidget *window, const gchar *label)
{
    GtkWidget *button = find_control(window, label);
    if (!GTK_IS_BUTTON(button) || !gtk_widget_is_sensitive(button))
        return FALSE;
    gtk_button_clicked(GTK_BUTTON(button));
    return TRUE;
}

/* Read the displayed simulator console without accessing private run state. */
/* Читает видимую консоль симулятора без доступа к внутреннему состоянию сеанса. */
static gboolean console_contains(GtkWidget *window, const gchar *expected)
{
    GtkWidget *console = find_control(window, "console");
    if (!console)
        return FALSE;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gboolean found = strstr(text, expected) != NULL;
    g_free(text);
    return found;
}

/* Select the expected signal in the real waveform track list. */
/* Выбирает ожидаемый сигнал в списке дорожек настоящей диаграммы. */
static gboolean select_track(GtkWidget *window, const gchar *signal)
{
    GtkWidget *tracks = find_control(window, "tracks");
    if (!GTK_IS_COMBO_BOX(tracks))
        return FALSE;
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(tracks));
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *name = NULL;
        gtk_tree_model_get(model, &row, 0, &name, -1);
        gboolean found = !g_strcmp0(name, signal);
        g_free(name);
        if (found)
        {
            gtk_combo_box_set_active_iter(GTK_COMBO_BOX(tracks), &row);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean select_scope_signal(GtkTreeView *view, const gchar *scope, const gchar *signal)
{
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter root;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &root); valid;
         valid = gtk_tree_model_iter_next(model, &root))
    {
        gchar *name = NULL;
        gtk_tree_model_get(model, &root, 0, &name, -1);
        gboolean match = !g_strcmp0(name, scope);
        g_free(name);
        if (!match)
            continue;
        GtkTreeIter child;
        for (valid = gtk_tree_model_iter_children(model, &child, &root); valid;
             valid = gtk_tree_model_iter_next(model, &child))
        {
            gtk_tree_model_get(model, &child, 0, &name, -1);
            match = !g_strcmp0(name, signal);
            g_free(name);
            if (match)
            {
                GtkTreePath *path = gtk_tree_model_get_path(model, &child);
                gtk_tree_view_expand_to_path(view, path);
                gtk_tree_selection_select_path(gtk_tree_view_get_selection(view), path);
                gtk_tree_path_free(path);
                return TRUE;
            }
        }
        break;
    }
    return FALSE;
}

static gboolean open_hdl_uses(gpointer data)
{
    (void)data;
    GtkWidget *map = find_window("Signal Map");
    if (!map)
        return G_SOURCE_CONTINUE;
    GtkWidget *scopes = find_control(map, "VCD scopes and signals");
    GtkWidget *aliases = find_control(map, "Aliases on the same recorded trace");
    if (!scopes || !aliases ||
        !select_scope_signal(GTK_TREE_VIEW(scopes), "counter_tb.dut", "count"))
        return G_SOURCE_CONTINUE;
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(aliases)));
    GtkWidget *button = gtk_dialog_get_widget_for_response(GTK_DIALOG(map), GTK_RESPONSE_OK);
    if (!GTK_IS_BUTTON(button) || !gtk_widget_is_sensitive(button))
        return G_SOURCE_CONTINUE;
    g_print("Opening HDL Uses from the Signal Map.\n");
    gtk_button_clicked(GTK_BUTTON(button));
    return G_SOURCE_REMOVE;
}

typedef struct
{
    gboolean bit;
    gboolean clock;
    gboolean mapped;
} GraphChecks;

/* EN: Check model row against expected graph edges. path is unused; data stores results.
 * RU: Проверяет строку row модели model. path не используется; data хранит результат. */
static gboolean check_connection(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *row,
                                 gpointer data)
{
    (void)path;
    GraphChecks *checks = data;
    gchar *role, *signal, *selection;
    gtk_tree_model_get(model, row, 0, &role, 1, &signal, 6, &selection, -1);
    if (!g_strcmp0(signal, "counter_tb.dut.odd") &&
        !g_strcmp0(role, "assign-destination") && !g_strcmp0(selection, "[0] / all"))
        checks->bit = TRUE;
    if (!g_strcmp0(signal, "counter_tb.dut.clk") && !g_strcmp0(role, "posedge-source"))
        checks->clock = TRUE;
    if (!g_strcmp0(signal, "counter_tb.mapped_count") && !g_strcmp0(selection, "[0] / [4]"))
        checks->mapped = TRUE;
    g_free(role);
    g_free(signal);
    g_free(selection);
    return FALSE;
}

/* EN: Report the wizard check that is blocking progress. Return a retry request.
 * RU: Сообщает, на какой проверке мастера остановился тест. Запрашивает повтор. */
static gboolean slider_wizard_wait(const gchar *check)
{
    static const gchar *last;
    if (last != check)
        g_printerr("Verilog GTK slider wizard waiting: %s\n", check);
    last = check;
    return G_SOURCE_CONTINUE;
}

/* EN: Check modeless port and device filters, then assign a slider by activating a port; callback data is unused.
 * Return G_SOURCE_CONTINUE while the wizard is unavailable.
 * RU: Проверяет фильтры портов и устройств, затем назначает порт ползунку.
 * data не используется. Возвращает G_SOURCE_CONTINUE, пока мастер недоступен. */
static gboolean add_test_slider(gpointer data)
{
    (void)data;
    GtkWidget *wizard = find_window("Add Test Device");
    if (!wizard)
        return slider_wizard_wait("wizard window");
    GtkWidget *ports = find_control(wizard, "insight-test-device-ports");
    if (!GTK_IS_TREE_VIEW(ports))
        return slider_wizard_wait("port tree");
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(ports));
    GtkTreeIter row;
    gboolean shared = FALSE, available = FALSE, inactive = FALSE;
    GtkTreePath *selected = NULL;
    gboolean next = gtk_tree_model_get_iter_first(model, &row);
    while (next)
    {
        gchar *name, *direction, *bindings, *width;
        gtk_tree_model_get(model, &row, 0, &name, 1, &direction, 2, &bindings, 3, &width, -1);
        if (!strcmp(name, "level_out") && !strcmp(direction, "output") &&
            strstr(bindings, "display") && strstr(bindings, "scope-xy"))
            shared = TRUE;
        if (!strcmp(name, "level") && !strcmp(direction, "input") &&
            !strcmp(bindings, "Unassigned") && !strcmp(width, "16"))
        {
            available = TRUE;
            selected = gtk_tree_model_get_path(model, &row);
        }
        g_free(width);
        inactive |= !strcmp(name, "inactive_slider");
        g_free(name);
        g_free(direction);
        g_free(bindings);
        next = gtk_tree_model_iter_next(model, &row);
    }
    if (gtk_window_get_modal(GTK_WINDOW(wizard)) || !shared || !available || inactive)
    {
        g_clear_pointer(&selected, gtk_tree_path_free);
        return slider_wizard_wait("modeless window and preprocessed port bindings");
    }
    GtkWidget *kind = find_control(wizard, "insight-test-device-type");
    GtkWidget *device_search = find_control(wizard, "insight-device-search");
    GtkWidget *category = find_control(wizard, "insight-device-category");
    GtkWidget *device_regex = find_control(wizard, "Device Regex");
    gboolean catalog_ok = GTK_IS_COMBO_BOX(kind) && GTK_IS_ENTRY(device_search) &&
                          GTK_IS_COMBO_BOX(category) && GTK_IS_TOGGLE_BUTTON(device_regex);
    if (catalog_ok)
    {
        GtkTreeModel *devices = gtk_combo_box_get_model(GTK_COMBO_BOX(kind));
        gtk_combo_box_set_active(GTK_COMBO_BOX(category), 7); /* Memory */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(device_regex), TRUE);
        gtk_entry_set_text(GTK_ENTRY(device_search), "^spi-");
        catalog_ok = gtk_tree_model_iter_n_children(devices, NULL) == 4;
        GtkTreeIter device;
        if (catalog_ok && gtk_tree_model_get_iter_first(devices, &device))
        {
            gchar *icon = NULL;
            gtk_tree_model_get(devices, &device, 1, &icon, -1);
            catalog_ok = icon && *icon;
            g_free(icon);
        }
        gtk_entry_set_text(GTK_ENTRY(device_search), "");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(device_regex), FALSE);
        gtk_combo_box_set_active(GTK_COMBO_BOX(category), 0);
    }
    if (catalog_ok)
    {
        catalog_ok = gtk_combo_box_set_active_id(GTK_COMBO_BOX(kind), "obufds-model");
        GtkWidget *primitive = find_control(wizard, "insight-device-first-field");
        catalog_ok = catalog_ok && GTK_IS_ENTRY(primitive) &&
                     !strcmp(gtk_entry_get_text(GTK_ENTRY(primitive)), "OBUFDS");
    }
    if (!catalog_ok || !gtk_combo_box_set_active_id(GTK_COMBO_BOX(kind), "slider"))
    {
        gtk_tree_path_free(selected);
        return slider_wizard_wait("catalog filtering and slider selection");
    }
    GtkWidget *entry = find_control(wizard, "insight-device-first-field");
    if (!GTK_IS_ENTRY(entry))
    {
        gtk_tree_path_free(selected);
        return slider_wizard_wait("slider port entry");
    }
    gtk_entry_set_text(GTK_ENTRY(entry), "replace_me");
    gtk_widget_grab_focus(entry);
    gtk_tree_view_row_activated(GTK_TREE_VIEW(ports), selected,
                                gtk_tree_view_get_column(GTK_TREE_VIEW(ports), 0));
    gtk_tree_path_free(selected);
    if (strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "level[7:0]"))
        return slider_wizard_wait("activation of level[7:0]");
    GtkWidget *search = find_control(wizard, "insight-port-search");
    GtkWidget *compatible = find_control(wizard, "Compatible only");
    if (!GTK_IS_ENTRY(search) || !GTK_IS_TOGGLE_BUTTON(compatible))
        return slider_wizard_wait("port search and compatibility controls");
    gtk_entry_set_text(GTK_ENTRY(search), "LEVEL");
    if (gtk_tree_model_iter_n_children(model, NULL) != 2)
        return slider_wizard_wait("case-insensitive port search");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compatible), TRUE);
    if (gtk_tree_model_iter_n_children(model, NULL) != 1)
        return slider_wizard_wait("compatible port filter");
    gtk_entry_set_text(GTK_ENTRY(search), "");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(compatible), FALSE);
    GtkWidget *regex = find_control(wizard, "Regex");
    GtkWidget *sort = find_control(wizard, "insight-port-sort");
    if (!GTK_IS_TOGGLE_BUTTON(regex) || !GTK_IS_COMBO_BOX(sort))
        return slider_wizard_wait("regex and sort controls");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(regex), TRUE);
    gtk_entry_set_text(GTK_ENTRY(search), "^level(_out)?$");
    gboolean pattern_ok = gtk_tree_model_iter_n_children(model, NULL) == 2;
    gtk_entry_set_text(GTK_ENTRY(search), "[");
    pattern_ok &= gtk_tree_model_iter_n_children(model, NULL) == 0;
    gtk_entry_set_text(GTK_ENTRY(search), "");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(regex), FALSE);

    gtk_combo_box_set_active(GTK_COMBO_BOX(sort), 3);
    gchar *first = NULL;
    if (gtk_tree_model_get_iter_first(model, &row))
        gtk_tree_model_get(model, &row, 0, &first, -1);
    gboolean order_ok = !g_strcmp0(first, "enable");
    g_free(first);
    gtk_combo_box_set_active(GTK_COMBO_BOX(sort), 2);
    first = NULL;
    if (gtk_tree_model_get_iter_first(model, &row))
        gtk_tree_model_get(model, &row, 2, &first, -1);
    order_ok &= !g_strcmp0(first, "Unassigned");
    g_free(first);
    GtkWidget *target = find_control(wizard, "insight-port-search-target");
    GtkWidget *occupancy = find_control(wizard, "insight-port-occupancy");
    if (!GTK_IS_COMBO_BOX(target) || !GTK_IS_COMBO_BOX(occupancy))
        return slider_wizard_wait("binding search controls");
    gtk_combo_box_set_active(GTK_COMBO_BOX(target), 2);
    gtk_entry_set_text(GTK_ENTRY(search), "scope-xyz");
    gboolean binding_ok = gtk_tree_model_iter_n_children(model, NULL) == 3;
    gtk_combo_box_set_active(GTK_COMBO_BOX(occupancy), 1);
    binding_ok &= gtk_tree_model_iter_n_children(model, NULL) == 0;
    gtk_combo_box_set_active(GTK_COMBO_BOX(occupancy), 2);
    binding_ok &= gtk_tree_model_iter_n_children(model, NULL) == 3;
    gtk_combo_box_set_active(GTK_COMBO_BOX(occupancy), 0);
    gtk_entry_set_text(GTK_ENTRY(search), "");
    gtk_combo_box_set_active(GTK_COMBO_BOX(target), 0);
    if (!pattern_ok || !order_ok || !binding_ok)
        return slider_wizard_wait("regex, sort and occupancy checks");
    gtk_dialog_response(GTK_DIALOG(wizard), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

typedef struct
{
    const gchar *kind, *first_port, *image;
} MemoryWizard;

