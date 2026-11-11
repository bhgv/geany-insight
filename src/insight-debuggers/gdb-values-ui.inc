/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Present native expression trees and request members only when users expand them.
 * RU: Показывает деревья выражений и запрашивает члены только при раскрытии пользователем. */

/* EN: Schedule inspection of session's expression after invalidating the previous frame snapshot.
 * RU: Запрашивает выражение session после сброса предыдущего снимка кадра. */
static void native_values_inspect(NativeDebug *session, const gchar *expression)
{
    if (!session->ready || !session->stopped || session->pending || session->writing || !*expression)
        return;
    gchar *copy = g_strdup(expression);
    native_values_reset(session);
    gtk_entry_set_text(GTK_ENTRY(session->value_expression), copy);
    g_free(copy);
    session->value_reload = TRUE;
    GtkWidget *page = gtk_widget_get_parent(session->value_status);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(session->tabs),
        gtk_notebook_page_num(GTK_NOTEBOOK(session->tabs), page));
    native_debug_resume(session->window, session);
}

/* EN: Inspect the entry in session data; button is unused.
 * RU: Исследует поле ввода сеанса data; button не используется. */
static void native_value_inspect_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    NativeDebug *session = data;
    native_values_inspect(session, gtk_entry_get_text(GTK_ENTRY(session->value_expression)));
}

/* EN: Fetch a page at offset for row in session; keep a reference across the async reply.
 * RU: Загружает страницу offset строки row в session и сохраняет ссылку на время запроса. */
static void native_value_expand(NativeDebug *session, GtkTreeIter *row, guint offset)
{
    if (!session->stopped || !session->ready || session->pending || session->writing)
        return;
    if (session->value_nodes >= 4096)
    {
        gtk_label_set_text(GTK_LABEL(session->value_status),
            insight_tr("Value tree limit reached (4096 entries). Inspect a narrower expression."));
        return;
    }
    GtkTreeModel *model = GTK_TREE_MODEL(session->value_rows);
    gchar *name = NULL;
    gtk_tree_model_get(model, row, 3, &name, -1);
    if (!name)
        return;
    GtkTreePath *path = gtk_tree_model_get_path(model, row);
    if (gtk_tree_path_get_depth(path) > 64)
    {
        gtk_label_set_text(GTK_LABEL(session->value_status), insight_tr("Inspect a narrower expression (depth limit: 64)."));
        gtk_tree_path_free(path);
        g_free(name);
        return;
    }
    if (session->value_pending_row)
        gtk_tree_row_reference_free(session->value_pending_row);
    session->value_pending_row = gtk_tree_row_reference_new(model, path);
    gtk_tree_path_free(path);
    g_free(session->value_pending_name);
    session->value_pending_name = name;
    session->value_request = NATIVE_VALUE_CHILDREN;
    session->value_offset = offset;
    session->value_pending_generation = session->value_generation;
    gchar *escaped = g_strescape(name, NULL);
    gchar *command = g_strdup_printf("-var-list-children --all-values \"%s\" %u %u",
                                     escaped, offset, offset + 128);
    native_debug_send(session->window, command);
    g_free(command);
    g_free(escaped);
}

/* EN: Load members of row in view for session data if not loaded; path is unused.
 * RU: Загружает ещё не полученные члены row из view для сеанса data; path не используется. */
static void native_value_expanded(GtkTreeView *view, GtkTreeIter *row, GtkTreePath *path, gpointer data)
{
    (void)path;
    gboolean loaded = FALSE;
    guint mode = 0, children = 0;
    gtk_tree_model_get(gtk_tree_view_get_model(view), row, 4, &loaded, 5, &children, 7, &mode, -1);
    if (!loaded && !mode && children)
        native_value_expand(data, row, 0);
}

/* EN: Return the selected normal value's object name and optionally its expression/value in session.
 * RU: Возвращает имя выбранного обычного объекта session, при необходимости его выражение и значение. */
static gchar *native_value_selected(NativeDebug *session, gchar **expression, gchar **value)
{
    GtkTreeIter row;
    GtkTreeModel *model = NULL;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(session->value_tree));
    if (!gtk_tree_selection_get_selected(selection, &model, &row))
        return NULL;
    guint mode = 0;
    gchar *name = NULL;
    gtk_tree_model_get(model, &row, 7, &mode, -1);
    if (!mode)
    {
        gtk_tree_model_get(model, &row, 3, &name, -1);
        if (expression)
            gtk_tree_model_get(model, &row, 0, expression, -1);
        if (value)
            gtk_tree_model_get(model, &row, 2, value, -1);
    }
    return name;
}

/* EN: Apply button's selected-value action to session data, resolving real C++ paths through GDB.
 * RU: Применяет действие button к выбранному значению сеанса data, получая точные пути C++ через GDB. */
static void native_value_action(GtkButton *button, gpointer data)
{
    NativeDebug *session = data;
    if (!session->ready || !session->stopped || session->pending || session->writing)
        return;
    gchar *expression = NULL, *value = NULL;
    gchar *name = native_value_selected(session, &expression, &value);
    NativeDataAction action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "value-action"));
    if (name && action == NATIVE_DATA_ASSIGN)
        native_assign_dialog(session, expression, value, name);
    else if (name)
    {
        session->value_request = NATIVE_VALUE_PATH;
        session->value_path_action = action;
        session->value_pending_generation = session->value_generation;
        gchar *escaped = g_strescape(name, NULL);
        gchar *command = g_strdup_printf("-var-info-path-expression \"%s\"", escaped);
        native_debug_send(session->window, command);
        g_free(command);
        g_free(escaped);
    }
    g_free(name);
    g_free(value);
    g_free(expression);
}

/* EN: Load pagination rows or edit scalar values activated at path in view for session data.
 * RU: Загружает дополнительные страницы или редактирует скаляр по path из view для сеанса data. */
static void native_value_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data)
{
    (void)column;
    NativeDebug *session = data;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter(model, &row, path))
        return;
    guint mode = 0, offset = 0, children = 0;
    gtk_tree_model_get(model, &row, 7, &mode, 6, &offset, 5, &children, -1);
    if (mode == NATIVE_VALUE_MORE)
    {
        GtkTreeIter parent;
        if (gtk_tree_model_iter_parent(model, &parent, &row))
            native_value_expand(session, &parent, offset);
    }
    else if (!mode && !children && session->stopped && !session->pending && !session->writing)
    {
        gchar *name = NULL, *expression = NULL, *value = NULL;
        gtk_tree_model_get(model, &row, 3, &name, 0, &expression, 2, &value, -1);
        if (name)
            native_assign_dialog(session, expression, value, name);
        g_free(value);
        g_free(expression);
        g_free(name);
    }
}

/* EN: Build the lazy value-tree page for session and register its paused-only controls.
 * RU: Создаёт страницу дерева session и регистрирует элементы, доступные только на паузе. */
static GtkWidget *native_values_widget(NativeDebug *session)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    session->value_expression = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(session->value_expression), 1024);
    gtk_entry_set_placeholder_text(GTK_ENTRY(session->value_expression), insight_tr("C/C++ expression to inspect"));
    gtk_widget_set_name(session->value_expression, "insight-gdb-inspect-expression");
    GtkWidget *inspect = gtk_button_new_with_label(insight_tr("Inspect Expression"));
    g_signal_connect(inspect, "clicked", G_CALLBACK(native_value_inspect_clicked), session);
    g_signal_connect_swapped(session->value_expression, "activate", G_CALLBACK(gtk_button_clicked), inspect);
    gtk_box_pack_start(GTK_BOX(bar), session->value_expression, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bar), inspect, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), bar, FALSE, FALSE, 0);
    g_ptr_array_add(session->paused_controls, bar);
    session->value_rows = gtk_tree_store_new(8, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
    session->value_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(session->value_rows));
    gtk_widget_set_name(session->value_tree, "insight-gdb-value-tree");
    gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(session->value_tree), TRUE);
    const gchar *titles[] = {"Expression / member", "Type", "Value"};
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
    {
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            insight_tr(titles[i]), gtk_cell_renderer_text_new(), "text", i, NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(session->value_tree), column);
    }
    g_signal_connect(session->value_tree, "row-expanded", G_CALLBACK(native_value_expanded), session);
    g_signal_connect(session->value_tree, "row-activated", G_CALLBACK(native_value_activated), session);
    g_ptr_array_add(session->paused_controls, session->value_tree);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), session->value_tree);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    GtkWidget *actions = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(actions), GTK_SELECTION_NONE);
    const gchar *labels[] = {"Change Value", "Add selected watch", "Break on Write", "Break on Read", "Break on Access"};
    const NativeDataAction commands[] = {NATIVE_DATA_ASSIGN, NATIVE_DATA_WATCH,
                                        NATIVE_DATA_WRITE, NATIVE_DATA_READ, NATIVE_DATA_ACCESS};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        g_object_set_data(G_OBJECT(button), "value-action", GUINT_TO_POINTER(commands[i]));
        g_signal_connect(button, "clicked", G_CALLBACK(native_value_action), session);
        gtk_container_add(GTK_CONTAINER(actions), button);
    }
    g_ptr_array_add(session->paused_controls, actions);
    gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
    session->value_status = gtk_label_new(insight_tr("Pause, choose a thread and frame, then inspect an expression."));
    gtk_label_set_line_wrap(GTK_LABEL(session->value_status), TRUE);
    gtk_box_pack_start(GTK_BOX(page), session->value_status, FALSE, FALSE, 0);
    return page;
}
