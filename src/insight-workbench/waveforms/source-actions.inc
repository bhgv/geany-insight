/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Waveform source actions and signal use views.
 * RU: Действия над исходниками и просмотр использования сигналов. */

/* Update actions for the current tab's selected source or signal. */
/* Обновляет действия для выбранного исходника или сигнала на текущей вкладке. */
static void waveform_use_actions(GtkTreeSelection *selection, GtkDialog *dialog)
{
    GtkTreeModel *model;
    GtkTreeIter row;
    gboolean connection = FALSE;
    guint line = 0;
    if (gtk_tree_selection_get_selected(selection, &model, &row))
        gtk_tree_model_get(model, &row, WAVE_USE_CONNECTION, &connection, WAVE_USE_LINE, &line, -1);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_ACCEPT, line != 0);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, connection);
}

/* Refresh the available actions after selecting a row. */
/* Обновляет доступные действия после выбора строки. */
static void waveform_use_selected(GtkTreeSelection *selection, gpointer data)
{
    waveform_use_actions(selection, GTK_DIALOG(data));
}

/* Refresh actions using the selection of the page being opened. */
/* Обновляет действия по выбранной строке открываемой вкладки. */
static void waveform_use_switch(GtkNotebook *notebook, GtkWidget *page, guint number, gpointer data)
{
    (void)notebook;
    (void)number;
    GtkTreeView *tree = g_object_get_data(G_OBJECT(page), "wave-use-tree");
    if (tree)
        waveform_use_actions(gtk_tree_view_get_selection(tree), GTK_DIALOG(data));
}

/* EN: Add a tab titled title to notebook for model. Show the originating
 * signal column when show_origin is set.
 * RU: Добавляет вкладку title с моделью model в notebook. При show_origin
 * показывает столбец исходного сигнала. */
static GtkTreeView *waveform_use_page(GtkNotebook *notebook, GtkTreeModel *model, const gchar *title, gboolean show_origin)
{
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkTreeView *tree = GTK_TREE_VIEW(gtk_tree_view_new_with_model(model));
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(tree));
    g_object_set_data(G_OBJECT(scroll), "wave-use-tree", tree);
    gtk_notebook_append_page(notebook, scroll, gtk_label_new(title));

    const gchar *titles[] = {insight_tr("Name"), insight_tr("Role"), insight_tr("File"), insight_tr("Line"), insight_tr("Hops"), insight_tr("Signal / peer selection"), insight_tr("From signal")};
    const gint columns[] = {WAVE_USE_NAME, WAVE_USE_ROLE, WAVE_USE_FILE, WAVE_USE_LINE, WAVE_USE_HOPS, WAVE_USE_SELECTION, WAVE_USE_FROM};
    for (guint column = 0; column < G_N_ELEMENTS(titles) - !show_origin; column++)
    {
        GtkTreeViewColumn *view_column = gtk_tree_view_column_new_with_attributes(
            titles[column], gtk_cell_renderer_text_new(), "text", columns[column], NULL);
        gtk_tree_view_column_set_sort_column_id(view_column, columns[column]);
        gtk_tree_view_append_column(tree, view_column);
    }
    return tree;
}

/* EN: Fill the four result models from jump's snapshot using the root selection.
 * Return true when a result cap was reached.
 * RU: Заполняет модели rows, graph, conditions и events из снимка jump
 * с учётом среза selection. Возвращает TRUE при достижении лимита. */
static gboolean waveform_fill_hdl_uses(WaveJump *jump, const gchar *selection,
    GtkListStore *rows, GtkTreeStore *graph, GtkListStore *conditions, GtkListStore *events)
{
    const InsightIndex *index = jump->snapshot->result->index;
    gtk_list_store_clear(rows);
    gtk_tree_store_clear(graph);
    gtk_list_store_clear(conditions);
    gtk_list_store_clear(events);
    WaveDependencyIndex dependencies = {
        .edges = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref),
        .declarations = g_hash_table_new(g_str_hash, g_str_equal),
        .references = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref),
        .references_remaining = 2000
    };
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gchar *target = g_strdup_printf("%u:%u", jump->line, jump->column);
    gchar *root_key = g_strdup_printf("%s\n%s", jump->path, target);

    for (gsize i = 0; i < insight_index_count(index); i++)
    {
        const InsightSymbol *row = insight_index_at(index, i);
        if (!row->line)
            continue;
        if (!g_strcmp0(row->kind, "@hdlwire"))
            waveform_group_index_row(dependencies.edges, g_strdup(row->scope), row);
        else if (!g_strcmp0(row->kind, "@hdldef") &&
                 (!g_strcmp0(row->type, "Variable") || !g_strcmp0(row->type, "Net")))
            g_hash_table_insert(dependencies.declarations, (gpointer)row->scope, (gpointer)row);
        else if (!g_strcmp0(row->kind, "@hdlref") &&
                 (!g_strcmp0(row->inheritance, "read") || !g_strcmp0(row->inheritance, "write") ||
                  !g_strcmp0(row->inheritance, "read/write") || !g_strcmp0(row->inheritance, "value") ||
                  !g_strcmp0(row->inheritance, "member")))
            waveform_group_index_row(dependencies.references,
                g_strdup_printf("%s\n%s", row->scope, row->type), row);

        if (g_hash_table_size(seen) >= 2000 || g_strcmp0(row->kind, "@hdlref") ||
            g_strcmp0(row->scope, jump->path) || g_strcmp0(row->type, target))
            continue;

        gchar *key = g_strdup_printf("%s\n%u\n%s\n%s\n%s", row->path, row->line,
            row->signature, row->inheritance, row->name);
        if (g_hash_table_add(seen, key))
            gtk_list_store_insert_with_values(rows, NULL, -1, WAVE_USE_ROLE, row->inheritance,
                WAVE_USE_NAME, row->name, WAVE_USE_FILE, row->path, WAVE_USE_LINE, row->line, -1);
    }
    guint upstream_remaining = 1000, downstream_remaining = 1000;
    guint upstream_controls = 1000, downstream_controls = 1000;
    waveform_walk_dependencies(rows, graph, conditions, events, &dependencies,
        jump->name, root_key, selection, TRUE, &upstream_remaining, &upstream_controls);
    waveform_walk_dependencies(rows, graph, conditions, events, &dependencies,
        jump->name, root_key, selection, FALSE, &downstream_remaining, &downstream_controls);
    g_hash_table_unref(dependencies.edges);
    g_hash_table_unref(dependencies.declarations);
    g_hash_table_unref(dependencies.references);
    g_free(root_key);
    g_free(target);
    gboolean reference_limit_reached = g_hash_table_size(seen) >= 2000;
    g_hash_table_unref(seen);

    return !upstream_remaining || !downstream_remaining || !upstream_controls ||
        !downstream_controls || reference_limit_reached || dependencies.reference_limit_reached;
}

/* EN: Show source uses, data paths, conditions and events for the signal in jump.
 * RU: Показывает использования, поток данных, условия и события сигнала из jump. */
static void waveform_show_hdl_uses(WaveJump *jump)
{
    GType types[] = {G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT,
        G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING};
    GtkListStore *rows = gtk_list_store_newv(WAVE_USE_COLUMNS, types);
    GtkTreeStore *graph = gtk_tree_store_newv(WAVE_USE_COLUMNS, types);
    GtkListStore *conditions = gtk_list_store_newv(WAVE_USE_COLUMNS, types);
    GtkListStore *events = gtk_list_store_newv(WAVE_USE_COLUMNS, types);
    enum { FILTER_SLICE = 101 };
    GtkWidget *dialog = gtk_dialog_new_with_buttons(insight_tr("HDL Uses"), GTK_WINDOW(main_widgets.window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, insight_tr("_Close"), GTK_RESPONSE_CLOSE,
        insight_tr("_Open Source"), GTK_RESPONSE_ACCEPT,
        insight_tr("_Select Track"), GTK_RESPONSE_APPLY,
        insight_tr("Apply slice"), FILTER_SLICE, NULL);
    g_object_ref_sink(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 400);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(jump->name);
    GtkWidget *filter = gtk_entry_new();
    gtk_widget_set_name(filter, "insight-hdl-use-slice");
    gtk_entry_set_max_length(GTK_ENTRY(filter), 64);
    gtk_entry_set_placeholder_text(GTK_ENTRY(filter), insight_tr("Whole signal or slice, e.g. [7:4]"));
    gtk_box_pack_start(GTK_BOX(box), filter, FALSE, FALSE, 0);
    GtkWidget *status = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 0);
    const gchar *limit = insight_tr("Result limit reached. Some HDL uses may be omitted.");
    if (waveform_fill_hdl_uses(jump, NULL, rows, graph, conditions, events))
        gtk_label_set_text(GTK_LABEL(status), limit);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 6);
    GtkNotebook *notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(notebook), TRUE, TRUE, 0);
    g_object_ref(notebook);
    GtkTreeView *uses_view = waveform_use_page(notebook, GTK_TREE_MODEL(rows), insight_tr("Uses"), FALSE);
    GtkTreeView *graph_view = waveform_use_page(notebook, GTK_TREE_MODEL(graph), insight_tr("Connections"), FALSE);
    GtkTreeView *condition_view = waveform_use_page(notebook, GTK_TREE_MODEL(conditions),
        insight_tr("Conditions"), TRUE);
    GtkTreeView *event_view = waveform_use_page(notebook, GTK_TREE_MODEL(events),
        insight_tr("Clock / events"), TRUE);
    GtkTreeSelection *condition_selection = g_object_ref(gtk_tree_view_get_selection(condition_view));
    GtkTreeSelection *event_selection = g_object_ref(gtk_tree_view_get_selection(event_view));
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(rows), WAVE_USE_NAME, GTK_SORT_ASCENDING);

    GtkTreeIter selected;
    GtkTreeSelection *uses_selection = g_object_ref(gtk_tree_view_get_selection(uses_view));
    GtkTreeSelection *graph_selection = g_object_ref(gtk_tree_view_get_selection(graph_view));
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(rows), &selected))
        gtk_tree_selection_select_iter(uses_selection, &selected);


    for (gint root = 0; root < 2; root++)
    {
        GtkTreePath *path = gtk_tree_path_new_from_indices(root, -1);
        gtk_tree_view_expand_row(graph_view, path, FALSE);
        gtk_tree_path_free(path);
    }
    waveform_use_actions(uses_selection, GTK_DIALOG(dialog));
    g_signal_connect(uses_selection, "changed", G_CALLBACK(waveform_use_selected), dialog);
    g_signal_connect(graph_selection, "changed", G_CALLBACK(waveform_use_selected), dialog);
    g_signal_connect(condition_selection, "changed", G_CALLBACK(waveform_use_selected), dialog);
    g_signal_connect(event_selection, "changed", G_CALLBACK(waveform_use_selected), dialog);
    g_signal_connect(notebook, "switch-page", G_CALLBACK(waveform_use_switch), dialog);
    gtk_widget_show_all(dialog);
    gint response;
    while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == FILTER_SLICE)
    {
        gchar *slice = g_strstrip(g_strdup(gtk_entry_get_text(GTK_ENTRY(filter))));
        gint64 low, high;
        if (*slice && (strstr(slice, " / ") || strstr(slice, " => ") || !waveform_selection_range(slice, &low, &high)))
        {
            gtk_label_set_text(GTK_LABEL(status),
                insight_tr("Enter a literal bit or slice, or clear the field for the whole signal."));
            g_free(slice);
            continue;
        }
        gboolean limited = waveform_fill_hdl_uses(jump, *slice ? slice : NULL,
            rows, graph, conditions, events);
        gtk_label_set_text(GTK_LABEL(status), limited ? limit : "");
        for (gint root = 0; root < 2; root++)
        {
            GtkTreePath *path = gtk_tree_path_new_from_indices(root, -1);
            gtk_tree_view_expand_row(graph_view, path, FALSE);
            gtk_tree_path_free(path);
        }
        g_free(slice);
    }
    GtkTreeSelection *selection = uses_selection;
    GtkTreeModel *model = GTK_TREE_MODEL(rows);
    if (response != GTK_RESPONSE_NONE)
    {
        gint page = gtk_notebook_get_current_page(notebook);
        GtkTreeSelection *selections[] = {uses_selection, graph_selection, condition_selection, event_selection};
        GtkTreeModel *models[] = {GTK_TREE_MODEL(rows), GTK_TREE_MODEL(graph),
            GTK_TREE_MODEL(conditions), GTK_TREE_MODEL(events)};
        if (page >= 0 && (guint)page < G_N_ELEMENTS(selections))
        {
            selection = selections[page];
            model = models[page];
        }
        gtk_widget_hide(dialog);
    }
    if (response == GTK_RESPONSE_APPLY &&
        gtk_tree_selection_get_selected(selection, NULL, &selected))
    {
        gchar *peer;
        gboolean connection;
        gtk_tree_model_get(model, &selected, WAVE_USE_NAME, &peer, WAVE_USE_CONNECTION, &connection, -1);
        GtkWindow *viewer = g_weak_ref_get(&jump->viewer);
        if (!connection)
            ui_set_statusbar(TRUE, "%s", insight_tr("Select a row with a full connected signal path."));
        else if (!insight_waveforms_select_track(viewer, peer))
            ui_set_statusbar(TRUE, insight_tr("Signal has no track in this recording: %s"), peer);
        g_clear_object(&viewer);
        g_free(peer);
    }
    else if (response == GTK_RESPONSE_ACCEPT &&
        gtk_tree_selection_get_selected(selection, NULL, &selected))
    {
        gchar *utf8_path;
        guint line;
        gtk_tree_model_get(model, &selected, WAVE_USE_FILE, &utf8_path, WAVE_USE_LINE, &line, -1);
        gchar *path = utils_get_locale_from_utf8(utf8_path);
        diff_source_open(path, line);
        g_free(path);
        g_free(utf8_path);
    }
    g_signal_handlers_disconnect_by_data(notebook, dialog);
    g_signal_handlers_disconnect_by_data(uses_selection, dialog);
    g_signal_handlers_disconnect_by_data(graph_selection, dialog);
    g_signal_handlers_disconnect_by_data(condition_selection, dialog);
    g_signal_handlers_disconnect_by_data(event_selection, dialog);
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    g_object_unref(notebook);
    g_object_unref(uses_selection);
    g_object_unref(graph_selection);
    g_object_unref(condition_selection);
    g_object_unref(event_selection);
    g_object_unref(conditions);
    g_object_unref(events);
    g_object_unref(graph);
    g_object_unref(rows);
}
