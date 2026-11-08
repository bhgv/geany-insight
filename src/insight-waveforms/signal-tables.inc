/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Signal table controls, pinning, filters and inspection pages.
 * RU: Команды таблиц сигналов, закрепление, фильтры и панели просмотра.
 */

/* EN: Apply the explicit pin action attached to item in simulation data.
 * RU: Применяет действие закрепления из item для сеанса data. */
static void signal_table_set_pin(GtkMenuItem *item, gpointer data)
{
    WaveSimulation *run = data;
    const gchar *key = g_object_get_data(G_OBJECT(item), "signal-pin-key");
    gboolean remove = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "signal-pin-remove"));
    GHashTable *pins = run->signal_pins_path ? signal_pins_load(run->signal_pins_path)
                                             : g_hash_table_ref(run->signal_pins);

    if (remove)
        g_hash_table_remove(pins, key);
    else if (g_hash_table_size(pins) < 256 || g_hash_table_contains(pins, key))
        g_hash_table_add(pins, g_strdup(key));
    else
    {
        gtk_label_set_text(GTK_LABEL(run->signal_status), insight_tr("Pinned signal limit reached."));
        g_hash_table_unref(pins);
        return;
    }

    GError *error = NULL;
    if (run->signal_pins_path && !signal_pins_save(run->signal_pins_path, pins, &error))
    {
        gtk_label_set_text(GTK_LABEL(run->signal_status), error->message);
        g_clear_error(&error);
        g_hash_table_unref(pins);
        return;
    }
    g_hash_table_unref(run->signal_pins);
    run->signal_pins = pins;
    signal_tables_refresh(run);
}

/* EN: Handle an edit or format-menu click in tree for simulation data.
 * event provides the button, click count and cell coordinates.
 * RU: Обрабатывает редактирование и меню форматов в tree для сеанса data.
 * event содержит кнопку, число щелчков и координаты ячейки. */
static gboolean signal_table_popup(GtkWidget *tree, GdkEventButton *event, gpointer data)
{
    WaveSimulation *run = data;
    GtkTreeView *view = GTK_TREE_VIEW(tree);
    if (event->button == 1 && gtk_tree_view_get_model(view) == GTK_TREE_MODEL(run->signal_tables[2]))
    {
        GtkTreePath *path = NULL;
        GtkTreeViewColumn *column;
        if (!gtk_tree_view_get_path_at_pos(view, event->x, event->y, &path, &column, NULL, NULL))
            return FALSE;
        gboolean value = column == gtk_tree_view_get_column(view, 1);
        if (value)
            gtk_tree_view_set_cursor(view, path, column, event->type == GDK_2BUTTON_PRESS);
        gtk_tree_path_free(path);
        return value;
    }
    if (event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;
    GtkWidget *menu = gtk_menu_new();
    gtk_menu_attach_to_widget(GTK_MENU(menu), tree, NULL);
    GtkTreePath *clicked = NULL;
    if (gtk_tree_view_get_path_at_pos(view, event->x, event->y, &clicked, NULL, NULL, NULL))
    {
        GtkTreeModel *model = gtk_tree_view_get_model(view);
        GtkTreeIter row;
        if (gtk_tree_model_get_iter(model, &row, clicked))
        {
            gchar *name = NULL, *bits = NULL, *kind = NULL;
            gtk_tree_model_get(model, &row, 0, &name, 2, &bits, 4, &kind, -1);
            gchar *pin_key = g_strconcat(kind, "\t", name, NULL);
            gboolean pinned = g_hash_table_contains(run->signal_pins, pin_key);
            GtkWidget *pin =
                gtk_menu_item_new_with_label(insight_tr(pinned ? "Unpin signal" : "Pin signal"));
            g_object_set_data_full(G_OBJECT(pin), "signal-pin-key", pin_key, g_free);
            g_object_set_data(G_OBJECT(pin), "signal-pin-remove", GINT_TO_POINTER(pinned));
            g_signal_connect(pin, "activate", G_CALLBACK(signal_table_set_pin), run);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), pin);
            if (run->signal_stops_ready && *bits && simulation_signal_path(name))
            {
                gboolean paused =
                    run->prompt_tail[0] == '>' && run->prompt_tail[1] == ' ' && !run->preview_pending;
                GtkWidget *arm = gtk_menu_item_new_with_label(insight_tr("Stop on signal…"));
                GtkWidget *clear = gtk_menu_item_new_with_label(insight_tr("Remove signal stop"));
                g_object_set_data_full(G_OBJECT(arm), "signal-stop-path", g_strdup(name), g_free);
                g_object_set_data(G_OBJECT(arm), "signal-stop-width", GUINT_TO_POINTER(strlen(bits)));
                g_object_set_data_full(G_OBJECT(clear), "signal-stop-path", g_strdup(name), g_free);
                gtk_widget_set_sensitive(arm, paused);
                gtk_widget_set_sensitive(clear, paused);
                g_signal_connect(arm, "activate", G_CALLBACK(signal_stop_configure), run);
                g_signal_connect(clear, "activate", G_CALLBACK(signal_stop_clear), run);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), arm);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), clear);
            }
            g_free(bits);
            if (run->navigate)
            {
                GtkWidget *graph = gtk_menu_item_new_with_label(insight_tr("Watch graph"));
                g_object_set_data_full(G_OBJECT(graph), "signal-name", g_strdup(name), g_free);
                g_signal_connect(graph, "activate", G_CALLBACK(signal_table_open_graph), run);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), graph);
            }
            GtkWidget *history = gtk_menu_item_new_with_label(insight_tr("Changes…"));
            g_object_set_data_full(G_OBJECT(history), "signal-name", name, g_free);
            g_object_set_data_full(G_OBJECT(history), "signal-kind", kind, g_free);
            g_signal_connect(history, "activate", G_CALLBACK(signal_table_open_history), run);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), history);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        }
        gtk_tree_path_free(clicked);
    }

    const gchar *formats[] = {"Binary",           "Verilog binary",
                              "C hexadecimal",    "Verilog hexadecimal",
                              "Unsigned decimal", "Character"};
    for (guint i = 0; i < G_N_ELEMENTS(formats); i++)
    {
        GtkWidget *item = gtk_menu_item_new_with_label(insight_tr(formats[i]));
        g_object_set_data(G_OBJECT(item), "signal-format", GUINT_TO_POINTER(i));
        g_signal_connect(item, "activate", G_CALLBACK(signal_table_format_selected), data);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}

/* EN: Refresh all tables after a search entry change. entry is unused.
 * RU: Обновляет таблицы сеанса data после изменения поиска. entry не используется. */
static void signal_table_search_changed(GtkEditable *entry, gpointer data)
{
    (void)entry;
    signal_tables_refresh(data);
}

/* EN: Reapply the snapshot filter for simulation data when toggle changes.
 * RU: Повторно применяет фильтр снимка сеанса data при переключении toggle. */
static void signal_table_changed_toggled(GtkToggleButton *toggle, gpointer data)
{
    (void)toggle;
    signal_tables_refresh(data);
}

/* EN: Render the kind of row in model into cell. column and data are unused.
 * RU: Показывает тип строки row модели model в cell. column и data не используются. */
static void signal_pin_kind(GtkTreeViewColumn *column, GtkCellRenderer *cell, GtkTreeModel *model,
                            GtkTreeIter *row, gpointer data)
{
    (void)column;
    (void)data;
    gchar *kind;
    gtk_tree_model_get(model, row, 4, &kind, -1);
    g_object_set(cell, "text",
                 insight_tr(*kind == 'P'   ? "Port"
                            : *kind == 'N' ? "Net"
                                           : "Register"),
                 NULL);
    g_free(kind);
}

/* EN: Build signal and pin tables, regex search and declaration context for run.
 * RU: Создаёт таблицы сигналов и закреплений, поиск и контекст для run. */
static GtkWidget *simulation_signal_tables(WaveSimulation *run)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    run->signal_rows = g_ptr_array_new_with_free_func((GDestroyNotify)g_strfreev);
    run->signal_pins = signal_pins_load(run->signal_pins_path);
    run->signal_previous_values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    run->signal_refresh_needed = TRUE;
    run->signal_search = gtk_search_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(run->signal_search), 256);
    gtk_entry_set_placeholder_text(GTK_ENTRY(run->signal_search),
                                   insight_tr("Regex: signal name or value"));
    GtkWidget *filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(filters), run->signal_search, TRUE, TRUE, 0);
    run->signal_changed_only = gtk_check_button_new_with_label(insight_tr("Changed only"));
    gtk_widget_set_tooltip_text(run->signal_changed_only,
                                insight_tr("Compare four-state values with the previous snapshot."));
    gtk_box_pack_start(GTK_BOX(filters), run->signal_changed_only, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), filters, FALSE, FALSE, 0);
    run->signal_state = gtk_label_new(insight_tr("Waiting for a paused signal snapshot."));
    gtk_box_pack_start(GTK_BOX(box), run->signal_state, FALSE, FALSE, 0);
    run->signal_status = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(run->signal_status), TRUE);
    gtk_box_pack_start(GTK_BOX(box), run->signal_status, FALSE, FALSE, 0);
    GtkWidget *tabs = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(box), tabs, TRUE, TRUE, 0);
    const gchar *titles[] = {"Ports", "Nets", "Registers", "Pinned signals"};
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
    {
        run->signal_tables[i] = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                                   G_TYPE_STRING, G_TYPE_STRING);
        GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(run->signal_tables[i]));
        GtkCellRenderer *name = gtk_cell_renderer_text_new();
        GtkCellRenderer *value = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *name_column =
            gtk_tree_view_column_new_with_attributes(insight_tr("Name"), name, "text", 0, NULL);
        GtkTreeViewColumn *value_column =
            gtk_tree_view_column_new_with_attributes(insight_tr("Value"), value, "text", 1, NULL);
        gtk_tree_view_column_set_sort_column_id(name_column, 0);
        gtk_tree_view_column_set_sort_column_id(value_column, 1);
        gtk_tree_view_column_set_resizable(name_column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), name_column);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), value_column);
        GtkTreeViewColumn *changed_column = gtk_tree_view_column_new_with_attributes(
            insight_tr("Changed"), gtk_cell_renderer_text_new(), "text", 3, NULL);
        gtk_tree_view_column_set_sort_column_id(changed_column, 3);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), changed_column);
        if (i == 3)
        {
            gtk_widget_set_name(tree, "insight-verilog-pinned-signals");
            GtkCellRenderer *kind = gtk_cell_renderer_text_new();
            GtkTreeViewColumn *kind_column =
                gtk_tree_view_column_new_with_attributes(insight_tr("Kind"), kind, NULL);
            gtk_tree_view_column_set_cell_data_func(kind_column, kind, signal_pin_kind, NULL, NULL);
            gtk_tree_view_column_set_sort_column_id(kind_column, 4);
            gtk_tree_view_append_column(GTK_TREE_VIEW(tree), kind_column);
        }

        run->signal_selection[i] = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
        g_signal_connect(run->signal_selection[i], "changed", G_CALLBACK(signal_table_selected), run);
        g_signal_connect(tree, "row-activated", G_CALLBACK(signal_table_activated), run);
        g_signal_connect(tree, "button-press-event", G_CALLBACK(signal_table_popup), run);
        if (i == 2)
        {
            gtk_widget_set_name(tree, "insight-verilog-registers");
            run->signal_register_renderer = value;
            g_signal_connect(value, "editing-started", G_CALLBACK(signal_edit_started), run);
            g_signal_connect(value, "edited", G_CALLBACK(signal_register_edited), run);
        }
        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_container_add(GTK_CONTAINER(scroll), tree);
        gtk_notebook_append_page(GTK_NOTEBOOK(tabs), scroll, gtk_label_new(insight_tr(titles[i])));
    }
    run->signal_context = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(run->signal_context), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(run->signal_context), TRUE);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, -1, 100);
    gtk_container_add(GTK_CONTAINER(scroll), run->signal_context);
    gtk_box_pack_start(GTK_BOX(box), scroll, FALSE, TRUE, 0);
    g_signal_connect(run->signal_search, "changed", G_CALLBACK(signal_table_search_changed), run);
    g_signal_connect(run->signal_changed_only, "toggled", G_CALLBACK(signal_table_changed_toggled), run);
    signal_tables_refresh(run);
    return box;
}
