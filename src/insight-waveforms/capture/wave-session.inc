/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Waveform window construction, view persistence and resource ownership.
 * RU: Создание окна диаграмм, сохранение вида и управление ресурсами.
 */

/* EN: Find the signal named name in capture. Return its index or -1.
 * RU: Находит сигнал name в capture. Возвращает индекс или -1. */
static gint find_signal(Capture *capture, const gchar *name)
{
    if (!name)
        return -1;
    for (guint i = 0; i < capture->signals->len; i++)
        if (!strcmp(((Signal *)capture->signals->pdata[i])->name, name))
            return (gint)i;
    return -1;
}

/* EN: Restore viewer's saved zoom, tracks, markers and instrument selections.
 * RU: Восстанавливает масштаб, дорожки, отметки и приборы viewer. */
static void restore_session(Viewer *viewer)
{
    GKeyFile *session = g_key_file_new();
    if (viewer->session_path &&
        g_key_file_load_from_file(session, viewer->session_path, G_KEY_FILE_NONE, NULL))
    {
        guint64 total = viewer->span;
        guint64 span = g_key_file_get_uint64(session, "view", "span", NULL);
        if (span)
            viewer->span = MIN(total, span);
        viewer->start = MIN(total - viewer->span, g_key_file_get_uint64(session, "view", "start", NULL));
        viewer->cursor =
            MIN(viewer->capture->end, g_key_file_get_uint64(session, "view", "cursor", NULL));
        viewer->hexadecimal = g_key_file_get_boolean(session, "view", "hex", NULL);
        const gchar *keys[] = {"marker-a", "marker-b"};
        for (guint i = 0; i < G_N_ELEMENTS(keys); i++)
        {
            GError *error = NULL;
            guint64 tick = g_key_file_get_uint64(session, "view", keys[i], &error);
            viewer->marker_set[i] = !error && tick <= viewer->capture->end;
            if (viewer->marker_set[i])
                viewer->marker[i] = tick;
            g_clear_error(&error);
        }
        gchar *active = g_key_file_get_string(session, "view", "active", NULL);
        gint found = find_signal(viewer->capture, active);
        if (found >= 0)
            viewer->active = found;
        g_free(active);
        const gchar *axis_keys[] = {"x", "y", "z"};
        for (guint i = 0; i < G_N_ELEMENTS(axis_keys); i++)
        {
            gchar *name = g_key_file_get_string(session, "instruments", axis_keys[i], NULL);
            if (name)
                viewer->instrument_index[i] = find_signal(viewer->capture, name) + 1;
            g_free(name);
        }
        gsize length = 0;
        gchar **names = g_key_file_get_string_list(session, "view", "tracks", &length, NULL);
        for (gsize i = 0; i < MIN(length, WAVE_TRACKS); i++)
        {
            gint index = find_signal(viewer->capture, names[i]);
            if (index >= 0 && pinned_index(viewer, index) < 0)
                g_array_append_val(viewer->tracks, index);
        }
        g_strfreev(names);
    }
    g_key_file_unref(session);
}

/* EN: Persist viewer's current view settings when a session path exists.
 * RU: Сохраняет настройки вида viewer при наличии пути сеанса. */
static void save_session(Viewer *viewer)
{
    if (!viewer->session_path)
        return;
    gchar *directory = g_path_get_dirname(viewer->session_path);
    if (g_mkdir_with_parents(directory, 0700) == 0)
    {
        GKeyFile *session = g_key_file_new();
        g_key_file_set_uint64(session, "view", "span", viewer->span);
        g_key_file_set_uint64(session, "view", "start", viewer->start);
        g_key_file_set_uint64(session, "view", "cursor", viewer->cursor);
        g_key_file_set_boolean(session, "view", "hex", viewer->hexadecimal);
        const gchar *keys[] = {"marker-a", "marker-b"};
        for (guint i = 0; i < G_N_ELEMENTS(keys); i++)
            if (viewer->marker_set[i])
                g_key_file_set_uint64(session, "view", keys[i], viewer->marker[i]);
        Signal *active = selected(viewer);
        if (active)
            g_key_file_set_string(session, "view", "active", active->name);
        const gchar *axis_keys[] = {"x", "y", "z"};
        for (guint i = 0; i < G_N_ELEMENTS(axis_keys); i++)
        {
            gint index = viewer->instrument_index[i];
            if (index >= 0)
            {
                Signal *signal = index ? viewer->capture->signals->pdata[index - 1] : NULL;
                g_key_file_set_string(session, "instruments", axis_keys[i], signal ? signal->name : "");
            }
        }
        const gchar *names[WAVE_TRACKS];
        for (guint i = 0; i < viewer->tracks->len; i++)
        {
            Signal *signal = viewer->capture->signals->pdata[g_array_index(viewer->tracks, gint, i)];
            names[i] = signal->name;
        }
        if (viewer->tracks->len)
            g_key_file_set_string_list(session, "view", "tracks", names, viewer->tracks->len);
        gsize length = 0;
        gchar *contents = g_key_file_to_data(session, &length, NULL);
        g_file_set_contents(viewer->session_path, contents, length, NULL);
        g_free(contents);
        g_key_file_unref(session);
    }
    g_free(directory);
}

/* EN: Release the Viewer in data and its owned capture and session resources.
 * RU: Освобождает Viewer из data, его запись и ресурсы сеанса. */
static void viewer_free(gpointer data)
{
    Viewer *viewer = data;
    save_session(viewer);
    g_free(viewer->session_path);
    capture_free(viewer->capture);
    g_array_unref(viewer->tracks);
    g_clear_object(&viewer->watch_values);
    g_free(viewer);
}

/* EN: Open the dependency graph for the selected Watch row in viewer data.
 * button provides the tree selection through its attached watch view.
 * RU: Открывает граф выбранной строки Watch в viewer из data.
 * Представление для выбора прикреплено к button. */
static void wave_watch_graph(GtkButton *button, gpointer data)
{
    Viewer *viewer = data;
    GtkTreeView *tree = g_object_get_data(G_OBJECT(button), "watch-tree");
    GtkTreeModel *model;
    GtkTreeIter row;
    if (!viewer->navigate ||
        !gtk_tree_selection_get_selected(gtk_tree_view_get_selection(tree), &model, &row))
        return;
    gchar *name = NULL;
    gtk_tree_model_get(model, &row, 0, &name, -1);
    viewer->navigate(name, TRUE, GTK_WINDOW(viewer->window));
    g_free(name);
}

/* EN: Show capture in a viewer under parent with session_path and navigate actions.
 * RU: Показывает capture в окне parent с настройками session_path и переходами navigate. */
static GtkWidget *show_capture(GtkWindow *parent, Capture *capture, const gchar *session_path,
                               InsightWaveSource navigate)
{
    Viewer *viewer = g_new0(Viewer, 1);
    viewer->capture = capture;
    for (guint i = 0; i < G_N_ELEMENTS(viewer->instrument_index); i++)
        viewer->instrument_index[i] = -1;
    viewer->tracks = g_array_new(FALSE, FALSE, sizeof(gint));
    viewer->span = MAX(G_GUINT64_CONSTANT(1), capture->end);
    viewer->navigate = navigate;
    viewer->session_path = g_strdup(session_path);
    restore_session(viewer);
    viewer->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(viewer->window), "VCD Waveforms");
    gtk_window_set_transient_for(GTK_WINDOW(viewer->window), parent);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(viewer->window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(viewer->window), 760, 340);
    g_object_set_data_full(G_OBJECT(viewer->window), "insight-wave-viewer", viewer, viewer_free);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_container_add(GTK_CONTAINER(viewer->window), box);
    viewer->selector = gtk_combo_box_text_new();
    viewer->filter = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(viewer->filter), "Filter signal names…");
    gtk_box_pack_start(GTK_BOX(box), viewer->filter, FALSE, FALSE, 0);
    g_signal_connect(viewer->filter, "search-changed", G_CALLBACK(filter_changed), viewer);
    g_signal_connect(viewer->filter, "activate", G_CALLBACK(filter_activate), viewer);
    GtkWidget *signal_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), signal_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(signal_row), viewer->selector, TRUE, TRUE, 0);
    viewer->pin = gtk_button_new_with_label("Pin track");
    g_signal_connect(viewer->pin, "clicked", G_CALLBACK(pin_clicked), viewer);
    gtk_box_pack_start(GTK_BOX(signal_row), viewer->pin, FALSE, FALSE, 0);
    GtkWidget *condition = gtk_button_new_with_label("Find Condition…");
    g_signal_connect(condition, "clicked", G_CALLBACK(wave_search_condition), viewer);
    gtk_box_pack_start(GTK_BOX(signal_row), condition, FALSE, FALSE, 0);
    GtkWidget *signal_map = gtk_button_new_with_label("Signal Map…");
    g_signal_connect(signal_map, "clicked", G_CALLBACK(wave_show_signal_map), viewer);
    gtk_box_pack_start(GTK_BOX(signal_row), signal_map, FALSE, FALSE, 0);
    GtkWidget *instruments = gtk_button_new_with_label("Signal Instruments…");
    g_signal_connect(instruments, "clicked", G_CALLBACK(instruments_show), viewer);
    gtk_box_pack_start(GTK_BOX(signal_row), instruments, FALSE, FALSE, 0);
    viewer->canvas = gtk_drawing_area_new();
    gtk_widget_add_events(viewer->canvas, GDK_BUTTON_PRESS_MASK);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), viewer->canvas);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    viewer->value = gtk_label_new(NULL);
    gtk_widget_set_name(viewer->value, "insight-wave-cursor-value");
    gtk_label_set_ellipsize(GTK_LABEL(viewer->value), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(box), viewer->value, FALSE, FALSE, 0);
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), controls, FALSE, FALSE, 0);
    const gchar *labels[] = {
        "Previous transition", "Next transition", "Zoom in", "Zoom out", "Earlier", "Later"};
    const gint actions[] = {-1, 1, 2, 3, 4, 5};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(labels[i]);
        g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(actions[i]));
        g_signal_connect(button, "clicked", G_CALLBACK(wave_action), viewer);
        gtk_box_pack_start(GTK_BOX(controls), button, FALSE, FALSE, 0);
    }
    GtkWidget *watch_panel = gtk_expander_new("Signal Watch");
    GtkWidget *watch_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(watch_panel), watch_box);
    gtk_box_pack_start(GTK_BOX(box), watch_panel, FALSE, FALSE, 0);
    GtkWidget *watch_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(watch_box), watch_controls, FALSE, FALSE, 0);
    const gchar *step_labels[] = {"Previous visible event", "Next visible event"};
    for (guint i = 0; i < G_N_ELEMENTS(step_labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(step_labels[i]);
        g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(i ? 6 : -6));
        g_signal_connect(button, "clicked", G_CALLBACK(wave_action), viewer);
        gtk_box_pack_start(GTK_BOX(watch_controls), button, FALSE, FALSE, 0);
    }
    viewer->watch_values =
        gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *watch_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(viewer->watch_values));
    GtkWidget *watch_graph = gtk_button_new_with_label(insight_tr("Watch graph"));
    gtk_widget_set_name(watch_graph, "insight-wave-watch-graph");
    gtk_widget_set_sensitive(watch_graph, viewer->navigate != NULL);
    g_object_set_data(G_OBJECT(watch_graph), "watch-tree", watch_tree);
    g_signal_connect(watch_graph, "clicked", G_CALLBACK(wave_watch_graph), viewer);
    gtk_box_pack_start(GTK_BOX(watch_controls), watch_graph, FALSE, FALSE, 0);

    const gchar *headings[] = {"Signal", "Value", "Last event tick", "Event at cursor"};
    for (guint i = 0; i < G_N_ELEMENTS(headings); i++)
    {
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            headings[i], gtk_cell_renderer_text_new(), "text", i, NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(watch_tree), column);
    }
    GtkWidget *watch_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(watch_scroll, -1, 128);
    gtk_container_add(GTK_CONTAINER(watch_scroll), watch_tree);
    gtk_box_pack_start(GTK_BOX(watch_box), watch_scroll, TRUE, TRUE, 0);
    GtkWidget *measure = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), measure, FALSE, FALSE, 0);
    viewer->radix = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(viewer->radix), "Binary");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(viewer->radix), "Hexadecimal");
    gtk_combo_box_set_active(GTK_COMBO_BOX(viewer->radix), viewer->hexadecimal ? 1 : 0);
    g_signal_connect(viewer->radix, "changed", G_CALLBACK(radix_changed), viewer);
    gtk_box_pack_start(GTK_BOX(measure), viewer->radix, FALSE, FALSE, 0);
    const gchar *marker_labels[] = {"Set A", "Set B", "Clear markers"};
    for (guint i = 0; i < G_N_ELEMENTS(marker_labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(marker_labels[i]);
        g_object_set_data(G_OBJECT(button), "marker", GINT_TO_POINTER(i < 2 ? (gint)i : -1));
        g_signal_connect(button, "clicked", G_CALLBACK(marker_clicked), viewer);
        gtk_box_pack_start(GTK_BOX(measure), button, FALSE, FALSE, 0);
    }
    viewer->markers = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(measure), viewer->markers, FALSE, FALSE, 0);
    GtkWidget *source_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), source_row, FALSE, FALSE, 0);
    viewer->source = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(viewer->source),
                                   "HDL hierarchy (edit to remove simulator-only scopes)");
    gtk_box_pack_start(GTK_BOX(source_row), viewer->source, TRUE, TRUE, 0);
    GtkWidget *open = gtk_button_new_with_label("Go to HDL declaration");
    g_signal_connect(open, "clicked", G_CALLBACK(source_clicked), viewer);
    gtk_box_pack_start(GTK_BOX(source_row), open, FALSE, FALSE, 0);
    g_signal_connect(viewer->canvas, "draw", G_CALLBACK(draw_wave), viewer);
    g_signal_connect(viewer->canvas, "button-press-event", G_CALLBACK(click_wave), viewer);
    g_signal_connect(viewer->selector, "changed", G_CALLBACK(select_wave), viewer);
    refresh_selector(viewer);
    activate_signal(viewer, viewer->active);
    gtk_widget_show_all(viewer->window);
    return viewer->window;
}
