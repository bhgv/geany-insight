/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Source navigation and the recorded signal map.
 * RU: Переход к исходнику и карта записанных сигналов.
 */

/* EN: Navigate to the source of the selected signal in Viewer data. button is unused.
 * RU: Переходит к исходнику сигнала Viewer из data. button не используется. */
static void source_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    Viewer *viewer = data;
    if (viewer->navigate)
        viewer->navigate(gtk_entry_get_text(GTK_ENTRY(viewer->source)), FALSE, NULL);
}

typedef struct
{
    Viewer *viewer;
    GtkListStore *aliases;
    GtkWidget *details;
    gint selected;
} WaveSignalMap;

/* EN: Return whether first and second refer to the same recorded signal.
 * RU: Проверяет, относятся ли first и second к одному записанному сигналу. */
static gboolean wave_same_recorded_signal(const Signal *first, const Signal *second)
{
    return first->trace == second->trace;
}

/* EN: Select capture signal index in map and refresh its displayed details.
 * RU: Выбирает сигнал index в map и обновляет сведения о нём. */
static void wave_map_update(WaveSignalMap *map, gint index)
{
    map->selected = index;
    gtk_list_store_clear(map->aliases);
    if (index < 0)
    {
        gtk_label_set_text(GTK_LABEL(map->details), "Select a signal in a VCD scope.");
        return;
    }
    const Signal *signal = map->viewer->capture->signals->pdata[index];
    guint at = upper_bound(signal->trace, map->viewer->cursor);
    gchar *value = format_bits(at ? g_array_index(signal->trace->edges, Edge, at - 1).value : "unknown",
                               map->viewer->hexadecimal);
    gchar *label = g_strdup_printf("%s  ·  %u bits  ·  tick %" G_GUINT64_FORMAT "  ·  %.128s",
                                   signal->name, signal->trace->width, map->viewer->cursor, value);
    gtk_label_set_text(GTK_LABEL(map->details), label);
    g_free(label);
    g_free(value);
    for (guint i = 0; i < map->viewer->capture->signals->len; i++)
    {
        const Signal *alias = map->viewer->capture->signals->pdata[i];
        if ((gint)i != index && wave_same_recorded_signal(signal, alias))
            gtk_list_store_insert_with_values(map->aliases, NULL, -1, 0, alias->name, 1, (gint)i, -1);
    }
}

/* EN: Use selection to update the signal map stored in data.
 * RU: Обновляет карту сигналов из data по выбранной строке selection. */
static void wave_map_selected(GtkTreeSelection *selection, gpointer data)
{
    WaveSignalMap *map = data;
    GtkTreeModel *model;
    GtkTreeIter row;
    gint index = -1;
    if (gtk_tree_selection_get_selected(selection, &model, &row))
        gtk_tree_model_get(model, &row, 1, &index, -1);
    wave_map_update(map, index);
}

/* EN: Build the scope tree for the signals in capture.
 * RU: Создаёт дерево областей для сигналов capture. */
static GtkTreeStore *wave_map_scopes(const Capture *capture)
{
    GtkTreeStore *store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    GHashTable *groups = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
    for (guint i = 0; i < capture->signals->len; i++)
    {
        const Signal *signal = capture->signals->pdata[i];
        GtkTreeIter *scope = g_hash_table_lookup(groups, signal->scope);
        if (!scope)
        {
            scope = g_new(GtkTreeIter, 1);
            gtk_tree_store_insert_with_values(store, scope, NULL, -1, 0,
                                              *signal->scope ? signal->scope : "(root)", 1, -1, -1);
            g_hash_table_insert(groups, signal->scope, scope);
        }
        gtk_tree_store_insert_with_values(store, NULL, scope, -1, 0, signal->leaf, 1, (gint)i, -1);
    }
    g_hash_table_unref(groups);
    return store;
}

/* EN: Create a tree view for model and pack it in box with column title.
 * RU: Создаёт дерево для model и размещает в box со столбцом title. */
static GtkWidget *wave_map_tree(GtkTreeModel *model, const gchar *title, GtkWidget *box)
{
    GtkWidget *tree = gtk_tree_view_new_with_model(model);
    GtkTreeViewColumn *column =
        gtk_tree_view_column_new_with_attributes(title, gtk_cell_renderer_text_new(), "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    return tree;
}

/* Open the recorded signal map and act on a selected signal or alias. */
/* Открывает карту записи и выполняет действие над сигналом или его псевдонимом. */
/* EN: Open the tool from button for the current view or simulation in data.
 * RU: Открывает инструмент по button для текущего просмотра или симуляции из data. */
static void wave_show_signal_map(GtkButton *button, gpointer data)
{
    (void)button;
    Viewer *viewer = data;
    GtkWidget *owner = g_object_ref(viewer->window);
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Signal Map", GTK_WINDOW(owner), GTK_DIALOG_DESTROY_WITH_PARENT, "_Close",
        GTK_RESPONSE_CLOSE, "_Select Track", GTK_RESPONSE_ACCEPT, "_Pin Track", GTK_RESPONSE_APPLY,
        "_Go to HDL", GTK_RESPONSE_REJECT, "_HDL Uses…", GTK_RESPONSE_OK, NULL);
    simulation_dialog_tools(
        dialog,
        "Browse recorded scopes and signals. Aliases refer to the same recorded trace. Select a "
        "signal to open its trace or inspect HDL uses.",
        "Просматривайте области и сигналы записи. Псевдонимы относятся к одной трассе. Выберите "
        "сигнал, чтобы открыть его трассу или места использования в HDL.");
    g_object_ref_sink(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 740, 410);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *details = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(details), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_selectable(GTK_LABEL(details), TRUE);
    gtk_box_pack_start(GTK_BOX(box), details, FALSE, FALSE, 6);
    GtkWidget *panes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(box), panes, TRUE, TRUE, 0);

    GtkTreeStore *scopes = wave_map_scopes(viewer->capture);
    GtkListStore *aliases = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    GtkWidget *scope_tree = wave_map_tree(GTK_TREE_MODEL(scopes), "VCD scopes and signals", panes);
    GtkWidget *alias_tree =
        wave_map_tree(GTK_TREE_MODEL(aliases), "Aliases on the same recorded trace", panes);
    GtkTreeSelection *scope_selection =
        g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(scope_tree)));
    GtkTreeSelection *alias_selection =
        g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(alias_tree)));
    WaveSignalMap map = {viewer, aliases, details, -1};
    g_signal_connect(scope_selection, "changed", G_CALLBACK(wave_map_selected), &map);
    wave_map_update(&map, viewer->active);

    gtk_widget_show_all(dialog);
    gint response = insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog));
    GtkTreeModel *alias_model;
    GtkTreeIter alias_row;
    gint index = map.selected;
    if (response != GTK_RESPONSE_NONE &&
        gtk_tree_selection_get_selected(alias_selection, &alias_model, &alias_row))
        gtk_tree_model_get(alias_model, &alias_row, 1, &index, -1);
    g_signal_handlers_disconnect_by_data(scope_selection, &map);
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    g_object_unref(alias_selection);
    g_object_unref(scope_selection);
    g_object_unref(aliases);
    g_object_unref(scopes);

    if (index < 0 || (response != GTK_RESPONSE_ACCEPT && response != GTK_RESPONSE_APPLY &&
                      response != GTK_RESPONSE_REJECT && response != GTK_RESPONSE_OK))
    {
        g_object_unref(owner);
        return;
    }
    const Signal *signal = viewer->capture->signals->pdata[index];
    if (response == GTK_RESPONSE_REJECT || response == GTK_RESPONSE_OK)
    {
        if (viewer->navigate)
            viewer->navigate(signal->name, response == GTK_RESPONSE_OK, GTK_WINDOW(owner));
        g_object_unref(owner);
        return;
    }
    if (response == GTK_RESPONSE_APPLY && pinned_index(viewer, index) < 0 &&
        viewer->tracks->len < WAVE_TRACKS)
        g_array_append_val(viewer->tracks, index);
    activate_signal(viewer, index);
    refresh_selector(viewer);
    g_object_unref(owner);
}
