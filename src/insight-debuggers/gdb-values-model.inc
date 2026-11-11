/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Own GDB variable objects and populate a bounded, lazily loaded value tree.
 * RU: Управляет объектами переменных GDB и ограниченным деревом значений с отложенной загрузкой. */

enum { NATIVE_VALUE_PLACEHOLDER = 1, NATIVE_VALUE_MORE = 2 };

/* EN: Clear session's frame-bound tree and queue disposal of its confirmed GDB root.
 * RU: Очищает дерево кадра session и ставит подтверждённый корневой объект GDB в очередь удаления. */
static void native_values_reset(NativeDebug *session)
{
    session->value_generation++;
    session->value_reload = FALSE;
    if (session->value_root)
        g_queue_push_tail(&session->value_cleanup, g_steal_pointer(&session->value_root));
    if (session->value_pending_row)
    {
        gtk_tree_row_reference_free(session->value_pending_row);
        session->value_pending_row = NULL;
    }
    gtk_tree_store_clear(session->value_rows);
    session->value_nodes = 0;
    gtk_label_set_text(GTK_LABEL(session->value_status),
        insight_tr("Pause, choose a thread and frame, then inspect an expression."));
}

/* EN: Return a cleanup or root-creation command for session; cleanup selects which queue is considered.
 * RU: Возвращает команду удаления либо создания корня session; cleanup выбирает тип очереди. */
static gchar *native_values_next(NativeDebug *session, gboolean cleanup)
{
    if (session->value_request != NATIVE_VALUE_NONE)
        return NULL;
    if (cleanup)
    {
        gchar *name = g_queue_pop_head(&session->value_cleanup);
        if (!name)
            return NULL;
        gchar *escaped = g_strescape(name, NULL);
        gchar *command = g_strdup_printf("-var-delete \"%s\"", escaped);
        g_free(escaped);
        g_free(name);
        session->value_request = NATIVE_VALUE_DELETE;
        return command;
    }
    session->value_reload = FALSE;
    const gchar *expression = gtk_entry_get_text(GTK_ENTRY(session->value_expression));
    if (!session->stopped || !*expression)
        return NULL;
    g_free(session->value_pending_expression);
    session->value_pending_expression = g_strdup(expression);
    session->value_request = NATIVE_VALUE_CREATE;
    session->value_pending_generation = session->value_generation;
    gchar *escaped = g_strescape(expression, NULL);
    gchar *command = g_strdup_printf("-var-create insight_value_%u * \"%s\"",
                                     ++session->value_serial, escaped);
    g_free(escaped);
    return command;
}

/* EN: Append one GDB tuple below parent in session, labelling the root with expression when supplied.
 * RU: Добавляет кортеж GDB tuple под parent в session; expression задаёт подпись корня, если передано. */
static void native_value_row(NativeDebug *session, GtkTreeIter *parent, const gchar *tuple,
                             const gchar *expression)
{
    if (session->value_nodes >= 4096)
        return;
    gchar *name = native_debug_field(tuple, "name");
    if (!name)
        return;
    gchar *label = expression ? g_strdup(expression) : native_debug_field(tuple, "exp");
    gchar *type = native_debug_field(tuple, "type");
    gchar *value = native_debug_field(tuple, "value");
    gchar *count = native_debug_field(tuple, "numchild");
    guint64 children = 0;
    if (count)
        g_ascii_string_to_unsigned(count, 10, 0, G_MAXUINT, &children, NULL);
    gchar *more = native_debug_field(tuple, "has_more");
    if (!children && !g_strcmp0(more, "1"))
        children = 1;
    g_free(more);
    gchar *safe_label = g_utf8_make_valid(label ? label : name, -1);
    gchar *safe_type = g_utf8_make_valid(type ? type : "", -1);
    gchar *safe_value = g_utf8_make_valid(value ? value : insight_tr("Unavailable"), -1);
    GtkTreeIter row;
    gtk_tree_store_insert_with_values(session->value_rows, &row, parent, -1,
        0, safe_label, 1, safe_type, 2, safe_value, 3, name, 4, FALSE,
        5, (guint)children, 6, 0u, 7, 0u, -1);
    session->value_nodes++;
    if (children)
        gtk_tree_store_insert_with_values(session->value_rows, NULL, &row, -1,
            0, insight_tr("Expand to load members"), 7, (guint)NATIVE_VALUE_PLACEHOLDER, -1);
    g_free(safe_value);
    g_free(safe_type);
    g_free(safe_label);
    g_free(count);
    g_free(value);
    g_free(type);
    g_free(label);
    g_free(name);
}

/* EN: Merge record's child page into parent of session, retaining previously loaded normal rows.
 * RU: Добавляет страницу дочерних значений record к parent в session, сохраняя загруженные обычные строки. */
static void native_value_children(NativeDebug *session, GtkTreeIter *parent, const gchar *record)
{
    GtkTreeModel *model = GTK_TREE_MODEL(session->value_rows);
    GtkTreeIter row;
    gboolean valid = gtk_tree_model_iter_children(model, &row, parent);
    while (valid)
    {
        guint mode = 0;
        gtk_tree_model_get(model, &row, 7, &mode, -1);
        valid = mode ? gtk_tree_store_remove(session->value_rows, &row) :
                       gtk_tree_model_iter_next(model, &row);
    }
    const gchar *cursor = strstr(record, "children=[");
    guint returned = 0;
    if (cursor)
    {
        cursor += strlen("children=[");
        const gchar *start, *end;
        while (returned < 128 && (end = native_next_tuple(&cursor, &start)))
        {
            gchar *tuple = g_strndup(start, end - start);
            native_value_row(session, parent, tuple, NULL);
            g_free(tuple);
            returned++;
        }
    }
    guint total = 0;
    gtk_tree_model_get(model, parent, 5, &total, -1);
    guint next = session->value_offset + returned;
    gchar *more = native_debug_field(record, "has_more");
    if (returned && session->value_nodes < 4096 && (next < total || !g_strcmp0(more, "1")))
        gtk_tree_store_insert_with_values(session->value_rows, NULL, parent, -1,
            0, insight_tr("Load next 128 members…"), 6, next, 7, (guint)NATIVE_VALUE_MORE, -1);
    gtk_tree_store_set(session->value_rows, parent, 4, TRUE, -1);
    gtk_label_set_text(GTK_LABEL(session->value_status), insight_tr(session->value_nodes >= 4096 ?
        "Value tree limit reached (4096 entries). Inspect a narrower expression." :
        "Values belong to the selected thread and frame at this stop."));
    g_free(more);
}

/* EN: Consume session's variable-object reply; return TRUE only for quiet cleanup replies.
 * RU: Обрабатывает ответ объектов переменных session; TRUE возвращается только для тихого удаления. */
static gboolean native_values_reply(NativeDebug *session, const gchar *record)
{
    NativeValueRequest request = session->value_request;
    session->value_request = NATIVE_VALUE_NONE;
    if (request == NATIVE_VALUE_NONE || request == NATIVE_VALUE_DELETE)
        return request == NATIVE_VALUE_DELETE;
    gboolean success = g_str_has_prefix(record, "^done");
    gboolean current = session->value_pending_generation == session->value_generation && session->stopped;
    if (request == NATIVE_VALUE_CREATE && success)
    {
        gchar *name = native_debug_field(record, "name");
        if (!current)
        {
            if (name)
                g_queue_push_tail(&session->value_cleanup, name);
        }
        else
        {
            session->value_root = name;
            native_value_row(session, NULL, record, session->value_pending_expression);
            gtk_label_set_text(GTK_LABEL(session->value_status),
                insight_tr("Expand a value to inspect its members. Double-click to edit a scalar."));
        }
    }
    else if (request == NATIVE_VALUE_CHILDREN && current)
    {
        GtkTreePath *path = session->value_pending_row ?
            gtk_tree_row_reference_get_path(session->value_pending_row) : NULL;
        GtkTreeIter row;
        if (path && gtk_tree_model_get_iter(GTK_TREE_MODEL(session->value_rows), &row, path))
        {
            if (success)
                native_value_children(session, &row, record);
            else
                gtk_tree_store_set(session->value_rows, &row, 4, FALSE, -1);
        }
        if (path)
            gtk_tree_path_free(path);
    }
    else if (request == NATIVE_VALUE_ASSIGN && success && current)
        native_data_refresh(session);
    else if (request == NATIVE_VALUE_PATH && success && current)
    {
        gchar *path = native_debug_field(record, "path_expr");
        if (path)
            native_data_use_expression(session, path, session->value_path_action);
        g_free(path);
    }
    if (!success && current)
    {
        gchar *message = native_debug_field(record, "msg");
        gtk_label_set_text(GTK_LABEL(session->value_status), message ? message : insight_tr("Unavailable"));
        g_free(message);
    }
    if (session->value_pending_row)
    {
        gtk_tree_row_reference_free(session->value_pending_row);
        session->value_pending_row = NULL;
    }
    return FALSE;
}
