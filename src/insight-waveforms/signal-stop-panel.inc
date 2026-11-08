/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Configure and manage signal-triggered stops.
 * RU: Настройка и управление остановками по сигналам.
 */

/* EN: Send a watch command for path in run when its simulator is paused.
 * mode selects the transition and mask contains the optional bit pattern.
 * RU: Отправляет команду для path в остановленном сеансе run.
 * mode задаёт переход, mask содержит необязательную маску. */
static void signal_stop_send(WaveSimulation *run, const gchar *path, const gchar *mode, const gchar *mask)
{
    if (!run->signal_stops_ready || !simulation_signal_path(path) || run->preview_pending ||
        run->prompt_tail[0] != '>' || run->prompt_tail[1] != ' ')
        return;
    gchar *command =
        g_strdup_printf("geany-watch-signal %s %s%s%s\n", path, mode, mask ? " " : "", mask ? mask : "");
    simulation_write_command(run, command);
    g_free(command);
}

/* EN: Set mask entry sensitivity from the mode selector. data is the entry.
 * RU: Включает поле маски data при выборе режима в combo. */
static void signal_stop_mode_changed(GtkComboBox *combo, gpointer data)
{
    gtk_widget_set_sensitive(GTK_WIDGET(data), gtk_combo_box_get_active(combo) == 4);
}

/* EN: Edit path in run using width, mode and mask. enabled controls arming.
 * RU: Редактирует path в run с width, mode и mask. enabled задаёт активацию. */
static void signal_stop_edit_rule(WaveSimulation *run, const gchar *path, guint width, const gchar *mode,
                                  const gchar *mask, gboolean enabled)
{
    GtkWidget *owner = g_object_ref(run->dialog);
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        insight_tr("Stop on signal"), GTK_WINDOW(run->dialog),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, insight_tr("Cancel"), GTK_RESPONSE_CANCEL,
        insight_tr(enabled ? "Arm signal stop" : "Save rule"), GTK_RESPONSE_ACCEPT, NULL);
    g_object_ref_sink(dialog);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 6);
    GtkWidget *name = gtk_label_new(path);
    gtk_label_set_selectable(GTK_LABEL(name), TRUE);
    gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0);
    GtkWidget *combo = gtk_combo_box_text_new();
    for (guint i = 0; i < G_N_ELEMENTS(stop_labels); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), insight_tr(stop_labels[i]));
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), MAX(0, signal_stop_mode(mode)));
    gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 0);
    GtkWidget *pattern = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(pattern), width ? width : 4096);
    gtk_entry_set_text(GTK_ENTRY(pattern), mask ? mask : "");
    gtk_entry_set_placeholder_text(GTK_ENTRY(pattern), insight_tr("Mask: 0, 1, X, Z and ? for any bit"));
    signal_stop_mode_changed(GTK_COMBO_BOX(combo), pattern);
    gtk_box_pack_start(GTK_BOX(box), pattern, FALSE, FALSE, 0);
    g_signal_connect(combo, "changed", G_CALLBACK(signal_stop_mode_changed), pattern);
    GtkWidget *status = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT &&
           g_object_get_data(G_OBJECT(owner), "insight-live-simulation") == run)
    {
        gint mode = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (mode < 0)
            continue;
        if ((mode == 1 || mode == 2) && width && width != 1)
        {
            gtk_label_set_text(GTK_LABEL(status), insight_tr("Choose a one-bit signal for edge stops."));
            continue;
        }
        gchar *mask = mode == 4 ? g_ascii_strdown(gtk_entry_get_text(GTK_ENTRY(pattern)), -1) : NULL;
        if (mask && (!*mask || (width && strlen(mask) != width) ||
                     strspn(mask, "01xz?") != strlen(mask) || strspn(mask, "?") == strlen(mask)))
        {
            gtk_label_set_text(
                GTK_LABEL(status),
                insight_tr("Enter one mask character per bit and at least one non-wildcard bit."));
            g_free(mask);
            continue;
        }
        if (enabled)
            signal_stop_send(run, path, stop_modes[mode], mask);
        else
        {
            GtkTreeIter row;
            if (signal_stop_find(run, path, &row))
            {
                gtk_list_store_set(run->signal_stop_store, &row, STOP_MODE, stop_modes[mode],
                                   STOP_CONDITION, insight_tr(stop_labels[mode]), STOP_MASK,
                                   mask ? mask : "", -1);
                signal_stop_save(run);
            }
        }
        g_free(mask);
        break;
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    g_object_unref(owner);
}

/* EN: Configure the signal attached to item in simulation data.
 * RU: Настраивает сигнал из item в сеансе data. */
static void signal_stop_configure(GtkMenuItem *item, gpointer data)
{
    gchar *path = g_strdup(g_object_get_data(G_OBJECT(item), "signal-stop-path"));
    guint width = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "signal-stop-width"));
    WaveSimulation *run = data;
    GtkTreeIter existing;
    if (path && width && simulation_signal_path(path))
    {
        if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(run->signal_stop_store), NULL) < 64 ||
            signal_stop_find(run, path, &existing))
            signal_stop_edit_rule(run, path, width, NULL, NULL, TRUE);
        else
            ui_set_statusbar(TRUE, "%s", insight_tr("Signal stop limit reached."));
    }
    g_free(path);
}

/* EN: Remove the watch on item's signal in simulation data.
 * RU: Снимает остановку на сигнале из item в сеансе data. */
static void signal_stop_clear(GtkMenuItem *item, gpointer data)
{
    const gchar *path = g_object_get_data(G_OBJECT(item), "signal-stop-path");
    if (path)
        signal_stop_send(data, path, "off", NULL);
}

/* EN: Remove the selected rule in simulation data. button is unused.
 * RU: Снимает выбранное правило сеанса data. button не используется. */
static void signal_stop_remove_selected(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    GtkTreeModel *model;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(run->signal_stop_selection, &model, &row))
        return;
    gchar *name;
    gtk_tree_model_get(model, &row, 0, &name, -1);
    run->signal_stop_disabling = FALSE;
    signal_stop_send(run, name, "off", NULL);
    g_free(name);
}

/* EN: Edit or toggle the selected rule for data according to button.
 * RU: Редактирует или переключает выбранное правило data по кнопке button. */
static void signal_stop_change_selected(GtkButton *button, gpointer data)
{
    WaveSimulation *run = data;
    GtkTreeModel *model;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(run->signal_stop_selection, &model, &row))
        return;
    gchar *name, *mode, *mask;
    gboolean enabled;
    gtk_tree_model_get(model, &row, STOP_PATH, &name, STOP_MODE, &mode, STOP_MASK, &mask, STOP_ENABLED,
                       &enabled, -1);
    if (GTK_WIDGET(button) == run->signal_stop_edit)
        signal_stop_edit_rule(run, name, 0, mode, mask, enabled);
    else
    {
        run->signal_stop_disabling = enabled;
        signal_stop_send(run, name, enabled ? "off" : mode, !enabled && *mask ? mask : NULL);
    }
    g_free(name);
    g_free(mode);
    g_free(mask);
}

/* EN: Build the active signal stop page for run.
 * RU: Создаёт вкладку активных остановок по сигналам для сеанса run. */
static GtkWidget *simulation_signal_stop_page(WaveSimulation *run)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *help = gtk_label_new(insight_tr("Set signal stops from a row in HDL Signals."));
    gtk_label_set_line_wrap(GTK_LABEL(help), TRUE);
    gtk_box_pack_start(GTK_BOX(box), help, FALSE, FALSE, 0);
    run->signal_stop_store =
        gtk_list_store_new(STOP_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT,
                           G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    signal_stop_load(run);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(run->signal_stop_store));
    gtk_widget_set_name(tree, "insight-verilog-signal-stops");
    const gchar *titles[] = {"Signal", "Condition", "Mask", "Hits", "State"};
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
    {
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            insight_tr(titles[i]), gtk_cell_renderer_text_new(), "text", i, NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_column_set_sort_column_id(column, i);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(run->signal_stop_store), 0,
                                         GTK_SORT_ASCENDING);
    run->signal_stop_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    g_signal_connect(tree, "row-activated", G_CALLBACK(signal_table_activated), run);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    run->signal_stop_remove = gtk_button_new_with_label(insight_tr("Remove signal stop"));
    gtk_widget_set_sensitive(run->signal_stop_remove, FALSE);
    g_signal_connect(run->signal_stop_remove, "clicked", G_CALLBACK(signal_stop_remove_selected), run);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(actions), run->signal_stop_remove, FALSE, FALSE, 0);
    run->signal_stop_edit = gtk_button_new_with_label(insight_tr("Edit signal rule…"));
    run->signal_stop_toggle = gtk_button_new_with_label(insight_tr("Enable / disable rule"));
    GtkWidget *changes[] = {run->signal_stop_edit, run->signal_stop_toggle};
    for (guint i = 0; i < G_N_ELEMENTS(changes); i++)
    {
        gtk_widget_set_sensitive(changes[i], FALSE);
        g_signal_connect(changes[i], "clicked", G_CALLBACK(signal_stop_change_selected), run);
        gtk_box_pack_start(GTK_BOX(actions), changes[i], FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), actions, FALSE, FALSE, 0);
    return box;
}
