/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Group stopped GDB backtraces by thread and select their source frames.
 * RU: Группирует стеки GDB по потокам и выбирает соответствующие кадры исходников. */

/* EN: Append scoped stack record to the thread currently requested by session.
 * RU: Добавляет ответ record в стек потока, запрошенного сеансом session. */
static void native_all_stacks_reply(NativeDebug *session, const gchar *record)
{
    GtkTreeIter *root = g_hash_table_lookup(session->thread_roots, session->all_stack_thread);
    if (!root)
        return;
    const gchar *prefix = "^done,stack=[";
    if (!g_str_has_prefix(record, prefix))
    {
        gtk_tree_store_set(session->all_stack_rows, root, 1, insight_tr("Stack unavailable"), -1);
        return;
    }
    const gchar *cursor = record + strlen(prefix);
    const gchar *start, *end;
    guint frames = 0;
    while (frames++ < 64 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *level = native_debug_field(tuple, "level");
        if (native_context_id(level))
        {
            gchar *function = native_debug_field(tuple, "func");
            gchar *file = native_debug_field(tuple, "fullname");
            gchar *line = native_debug_field(tuple, "line");
            gchar *fallback = file ? NULL : native_debug_field(tuple, "file");
            gchar *display = g_filename_display_name(file ? file : fallback ? fallback : "");
            gchar *label = g_strdup_printf("#%s", level);
            gchar *valid = g_utf8_make_valid(function ? function : "??", -1);
            gtk_tree_store_insert_with_values(session->all_stack_rows, NULL, root, -1,
                0, label, 1, valid, 2, display, 3, line, 4, file,
                5, session->all_stack_thread, 6, level, -1);
            g_free(valid);
            g_free(label);
            g_free(display);
            g_free(fallback);
            g_free(line);
            g_free(file);
            g_free(function);
        }
        g_free(level);
        g_free(tuple);
    }
    if (!g_strcmp0(session->all_stack_thread, session->selected_thread))
    {
        GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(session->all_stack_rows), root);
        gtk_tree_view_expand_row(GTK_TREE_VIEW(session->all_stacks_view), path, FALSE);
        gtk_tree_path_free(path);
    }
}

/* EN: Select the thread and frame at path in view for the dialog passed as data.
 * RU: Выбирает поток и кадр path из view для диалога data. */
static void native_all_stacks_activate(GtkTreeView *view, GtkTreePath *path,
                                       GtkTreeViewColumn *column, gpointer data)
{
    (void)column;
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (!session->ready || !session->stopped || session->pending || session->writing)
        return;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter(model, &row, path))
        return;
    gchar *thread = NULL, *level = NULL;
    gtk_tree_model_get(model, &row, 5, &thread, 6, &level, -1);
    if (!native_context_id(thread) || !native_context_id(level))
    {
        if (gtk_tree_view_row_expanded(view, path))
            gtk_tree_view_collapse_row(view, path);
        else
            gtk_tree_view_expand_row(view, path, FALSE);
    }
    else
    {
        gboolean same_thread = !g_strcmp0(session->selected_thread, thread);
        g_free(session->requested_frame);
        session->requested_frame = g_strdup(level);
        if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(session->threads), thread))
            g_clear_pointer(&session->requested_frame, g_free);
        else if (same_thread)
        {
            session->stack_needed = TRUE;
            native_debug_resume(dialog, session);
        }
        native_open_frame(view, path, NULL, NULL);
    }
    g_free(level);
    g_free(thread);
}

/* EN: Expand or collapse the thread tree in data according to button's action.
 * RU: Раскрывает либо сворачивает дерево потоков data по действию button. */
static void native_all_stacks_expand(GtkButton *button, gpointer data)
{
    if (g_object_get_data(G_OBJECT(button), "expand"))
        gtk_tree_view_expand_all(GTK_TREE_VIEW(data));
    else
        gtk_tree_view_collapse_all(GTK_TREE_VIEW(data));
}

/* EN: Schedule fresh backtraces for dialog when notebook shows page; number is unused.
 * RU: Запрашивает свежие стеки при показе общей страницы сеанса из dialog. */
static void native_all_stacks_page(GtkNotebook *notebook, GtkWidget *page, guint number,
                                   gpointer dialog)
{
    (void)notebook;
    (void)number;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    session->all_stacks_visible = page == session->all_stacks_page;
    if (session->all_stacks_visible && session->stopped)
    {
        session->threads_needed = TRUE;
        native_debug_resume(GTK_WIDGET(dialog), session);
    }
}

/* EN: Create the all-thread stack page for session and connect navigation to dialog.
 * RU: Создаёт общую страницу стеков session и подключает переходы к dialog. */
static GtkWidget *native_all_stacks_widget(NativeDebug *session, GtkWidget *dialog)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    session->all_stacks_page = page;
    session->thread_ids = g_ptr_array_new_with_free_func(g_free);
    session->thread_roots = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    session->all_stack_rows = gtk_tree_store_new(
        7, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    session->all_stacks_status = gtk_label_new(insight_tr("Open this page while paused to collect all thread stacks."));
    gtk_label_set_line_wrap(GTK_LABEL(session->all_stacks_status), TRUE);
    gtk_box_pack_start(GTK_BOX(page), session->all_stacks_status, FALSE, FALSE, 0);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(session->all_stack_rows));
    session->all_stacks_view = tree;
    gtk_widget_set_name(tree, "insight-gdb-all-thread-stacks");
    gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(tree), TRUE);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(tree), 4);
    const gchar *titles[] = {"Thread / frame", "Function", "File", "Line number"};
    const gchar *colors[] = {"#8a50ae", "#167c70", NULL, "#a85416"};
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
    {
        GtkCellRenderer *cell = gtk_cell_renderer_text_new();
        if (colors[i])
            g_object_set(cell, "foreground", colors[i], NULL);
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            insight_tr(titles[i]), cell, "text", i, NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    }
    g_signal_connect(tree, "row-activated", G_CALLBACK(native_all_stacks_activate), dialog);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    const gchar *labels[] = {"Expand all", "Collapse all"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        g_object_set_data(G_OBJECT(button), "expand", GINT_TO_POINTER(i == 0));
        g_signal_connect(button, "clicked", G_CALLBACK(native_all_stacks_expand), tree);
        gtk_box_pack_start(GTK_BOX(actions), button, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    return page;
}
