/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Synchronize GDB breakpoints from every entry point and edit their conditions and state.
 * RU: Синхронизирует точки GDB из всех способов установки и редактирует условия и состояние. */

/* EN: Return a canonical key for filename and one-based line, normalizing Windows spelling.
 * RU: Возвращает канонический ключ filename и строки line, нормализуя запись пути Windows. */
static gchar *native_point_key(const gchar *filename, guint line)
{
    gchar *canonical = insight_canonicalize_filename(filename, NULL);
#ifdef G_OS_WIN32
    g_strdelimit(canonical, "\\", '/');
    gchar *folded = g_utf8_casefold(canonical, -1);
    g_free(canonical);
    canonical = folded;
#endif
    gchar *key = g_strdup_printf("%s:%u", canonical, line);
    g_free(canonical);
    return key;
}

/* EN: Validate GDB number, accepting a decimal breakpoint or its decimal sub-location.
 * RU: Проверяет number GDB: десятичный номер точки либо её десятичной подпозиции. */
static gboolean native_point_number(const gchar *number)
{
    if (native_context_id(number))
        return TRUE;
    const gchar *dot = number ? strchr(number, '.') : NULL;
    if (!dot || !native_context_id(dot + 1))
        return FALSE;
    gchar *parent = g_strndup(number, dot - number);
    gboolean valid = native_context_id(parent);
    g_free(parent);
    return valid;
}

/* EN: Toggle filename:line in dialog data using GDB's confirmed breakpoint identifier.
 * RU: Переключает filename:line в диалоге data по подтверждённому идентификатору GDB. */
static void native_debug_toggle(const gchar *filename, guint line, gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (session->pending || session->writing || session->editor_point_pending)
        return;
    gchar *location = g_strdup_printf("%s:%u", filename, line);
    gchar *key = native_point_key(filename, line);
    const gchar *number = g_hash_table_lookup(session->editor_points, key);
    g_free(key);
    session->editor_point_pending = location;
    gchar *escaped = g_strescape(location, NULL);
    gchar *command = number ? g_strdup_printf("-break-delete %s", number) :
                             g_strdup_printf("-break-insert -f -- \"%s\"", escaped);
    native_debug_send(dialog, command);
    g_free(command);
    g_free(escaped);
}

/* EN: Add tuple to session; inherit condition and counters from parent for sub-locations.
 * RU: Добавляет tuple в session; для подпозиций наследует условие и счётчики parent. */
static void native_point_tuple(NativeDebug *session, const gchar *tuple, const gchar *parent)
{
    gchar *number = native_debug_field(tuple, "number");
    if (!native_point_number(number))
    {
        g_free(number);
        return;
    }
    gchar *function = native_debug_field(tuple, "func");
    gchar *file = native_debug_field(tuple, "fullname");
    gchar *line = native_debug_field(tuple, "line");
    gchar *original = native_debug_field(tuple, "original-location");
    gchar *enabled = native_debug_field(tuple, "enabled");
    gchar *condition = native_debug_field(tuple, "cond");
    gchar *hits = native_debug_field(tuple, "times");
    gchar *ignore = native_debug_field(tuple, "ignore");
    gchar *what = native_debug_field(tuple, "what");
    gchar *kind = native_debug_field(tuple, "type");
    if (strstr(tuple, "locations=["))
    {
        g_clear_pointer(&function, g_free);
        g_clear_pointer(&file, g_free);
        g_clear_pointer(&line, g_free);
    }
    if (parent)
    {
        if (!kind)
            kind = native_debug_field(parent, "type");
        if (!condition)
            condition = native_debug_field(parent, "cond");
        if (!hits)
            hits = native_debug_field(parent, "times");
        if (!ignore)
            ignore = native_debug_field(parent, "ignore");
    }
    if (!hits)
        hits = g_strdup("0");
    if (!ignore)
        ignore = g_strdup("0");
    gchar *exception = native_catch_description(tuple);
    gchar *location = exception ? exception : file && line ? g_strdup_printf("%s:%s", file, line) :
                                    g_strdup(original ? original : what ? what : number);
    gtk_list_store_insert_with_values(session->point_rows, NULL, -1,
        0, location, 1, number, 2, enabled, 3, condition, 4, hits, 5, ignore,
        6, file, 7, line, 8, kind, 9, function, -1);
    if (file && native_context_id(line))
    {
        guint64 row = g_ascii_strtoull(line, NULL, 10);
        if (row && row <= G_MAXINT)
        {
            gchar *owner = g_strdup(number);
            gchar *dot = strchr(owner, '.');
            if (dot)
                *dot = '\0';
            g_hash_table_replace(session->editor_points, native_point_key(file, row), owner);
        }
    }
    g_free(location);
    g_free(what);
    g_free(kind);
    g_free(ignore);
    g_free(hits);
    g_free(condition);
    g_free(enabled);
    g_free(original);
    g_free(line);
    g_free(function);
    g_free(file);
    g_free(number);
}

/* EN: Rebuild session's complete breakpoint snapshot from a successful -break-list record.
 * RU: Перестраивает полный снимок точек session по успешному ответу record на -break-list. */
static void native_points_snapshot(NativeDebug *session, const gchar *record)
{
    const gchar *body = strstr(record, "body=[");
    if (!g_str_has_prefix(record, "^done") || !body)
        return;
    gtk_list_store_clear(session->point_rows);
    g_hash_table_remove_all(session->editor_points);
    g_ptr_array_set_size(session->point_markers, 0);
    const gchar *cursor = body + strlen("body=[");
    const gchar *start, *end;
    guint count = 0;
    gchar *parent = NULL;
    while (count++ < 4096 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *number = native_debug_field(tuple, "number");
        if (native_context_id(number))
        {
            g_free(parent);
            parent = g_strdup(tuple);
        }
        native_point_tuple(session, tuple, native_context_id(number) ? NULL : parent);
        const gchar *nested = strstr(tuple, "locations=[");
        if (nested)
        {
            nested += strlen("locations=[");
            const gchar *child_start, *child_end;
            while (count++ < 4096 && (child_end = native_next_tuple(&nested, &child_start)))
            {
                gchar *child = g_strndup(child_start, child_end - child_start);
                native_point_tuple(session, child, tuple);
                g_free(child);
            }
        }
        g_free(number);
        g_free(tuple);
    }
    g_free(parent);
    native_source_points(session);
}

/* EN: Apply record to session while preserving the selected breakpoint by GDB number.
 * RU: Применяет record к session, сохраняя выбранную точку по номеру GDB. */
static void native_points_refresh(NativeDebug *session, const gchar *record)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(session->points_view));
    GtkTreeModel *model = GTK_TREE_MODEL(session->point_rows);
    GtkTreeIter row;
    gchar *selected = NULL;
    if (gtk_tree_selection_get_selected(selection, NULL, &row))
        gtk_tree_model_get(model, &row, 1, &selected, -1);
    native_points_snapshot(session, record);
    for (gboolean valid = selected && gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *number = NULL;
        gtk_tree_model_get(model, &row, 1, &number, -1);
        gboolean matches = !g_strcmp0(number, selected);
        g_free(number);
        if (matches)
        {
            gtk_tree_selection_select_iter(selection, &row);
            break;
        }
    }
    g_free(selected);
}

/* EN: Track edits in session from record, including CLI edits, then request a canonical snapshot.
 * RU: Отслеживает изменения session из record, включая консоль, и запрашивает полный снимок. */
static void native_debug_point_reply(NativeDebug *session, const gchar *record)
{
    if (g_str_has_prefix(record, "=breakpoint-") || g_str_has_prefix(record, "^done,bkpt="))
        session->points_needed = TRUE;
    if (*record != '^')
        return;
    if (session->points_pending)
    {
        session->points_pending = FALSE;
        native_points_refresh(session, record);
        native_profile_points_snapshot(session, record);
    }
    if (session->editor_point_pending)
    {
        gchar *location = g_steal_pointer(&session->editor_point_pending);
        session->points_needed = TRUE;
        ui_set_statusbar(FALSE, "%s %s", insight_tr(g_str_has_prefix(record, "^done") ?
            "Source breakpoint updated." : "Could not change the source breakpoint."), location);
        g_free(location);
    }
}

/* EN: Return the selected row's GDB identifier in session, or NULL when no row is selected.
 * RU: Возвращает идентификатор GDB выбранной строки session либо NULL, если выбора нет. */
static gchar *native_point_selected(NativeDebug *session, GtkTreeIter *row)
{
    GtkTreeModel *model = NULL;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(session->points_view));
    if (!gtk_tree_selection_get_selected(selection, &model, row))
        return NULL;
    gchar *number = NULL;
    gtk_tree_model_get(model, row, 1, &number, -1);
    return number;
}

/* EN: Edit the selected breakpoint in dialog data according to button's action, after validation.
 * RU: Редактирует выбранную точку диалога data по действию button после проверки параметров. */
static void native_point_edit(GtkButton *button, gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (!session->ready || session->pending || session->writing || session->running)
        return;
    GtkTreeIter row;
    gchar *number = native_point_selected(session, &row);
    if (!native_point_number(number))
    {
        g_free(number);
        return;
    }
    g_object_ref(dialog);
    const gchar *action = g_object_get_data(G_OBJECT(button), "point-action");
    gchar *dot = strchr(number, '.');
    if (dot && strcmp(action, "toggle"))
        *dot = '\0';
    gchar *command = NULL;
    if (!strcmp(action, "delete"))
        command = g_strdup_printf("-break-delete %s", number);
    else if (!strcmp(action, "toggle"))
    {
        gchar *enabled = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(session->point_rows), &row, 2, &enabled, -1);
        command = g_strdup_printf("-break-%s %s", !g_strcmp0(enabled, "y") ? "disable" : "enable", number);
        g_free(enabled);
    }
    else
    {
        gboolean condition = !strcmp(action, "condition");
        gchar *current = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(session->point_rows), &row, condition ? 3 : 5, &current, -1);
        GtkWidget *editor = gtk_dialog_new_with_buttons(insight_tr(condition ? "Breakpoint condition" : "Ignore first hits"),
            GTK_WINDOW(dialog), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT, NULL);
        GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(editor));
        GtkWidget *hint = gtk_label_new(insight_tr(condition ?
            "C/C++ expression; leave empty for an unconditional breakpoint." :
            "Number of hits to skip before stopping (zero stops on the next hit)."));
        gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
        gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 8);
        GtkWidget *entry = gtk_entry_new();
        gtk_widget_set_name(entry, "insight-gdb-breakpoint-rule");
        gtk_entry_set_text(GTK_ENTRY(entry), current ? current : "");
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
        gtk_dialog_set_default_response(GTK_DIALOG(editor), GTK_RESPONSE_ACCEPT);
        gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 8);
        gtk_widget_show_all(editor);
        if (gtk_dialog_run(GTK_DIALOG(editor)) == GTK_RESPONSE_ACCEPT && !session->closed)
        {
            const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
            if (condition)
            {
                gchar *escaped = g_strescape(text, NULL);
                command = *text ? g_strdup_printf("-break-condition %s \"%s\"", number, escaped) :
                                  g_strdup_printf("-break-condition %s", number);
                g_free(escaped);
            }
            else if (native_context_id(text))
            {
                guint64 count;
                if (g_ascii_string_to_unsigned(text, 10, 0, G_MAXINT, &count, NULL))
                    command = g_strdup_printf("-break-after %s %" G_GUINT64_FORMAT, number, count);
            }
            if (!command)
                ui_set_statusbar(FALSE, "%s", insight_tr("Enter a nonnegative whole number."));
        }
        gtk_widget_destroy(editor);
        g_free(current);
    }
    if (command && !session->closed)
    {
        session->points_needed = TRUE;
        native_debug_send(dialog, command);
    }
    g_free(command);
    g_free(number);
    g_object_unref(dialog);
}

/* EN: Open the breakpoint source at path in view; column and data are unused.
 * RU: Открывает исходник точки path из view; column и data не используются. */
static void native_point_open(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data)
{
    (void)column;
    (void)data;
    GtkTreeIter row;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    if (!gtk_tree_model_get_iter(model, &row, path))
        return;
    gchar *file = NULL, *line = NULL;
    gtk_tree_model_get(model, &row, 6, &file, 7, &line, -1);
    guint64 number;
    if (file && line && g_ascii_string_to_unsigned(line, 10, 1, G_MAXINT, &number, NULL))
    {
        gchar *native = utils_get_locale_from_utf8(file);
        diff_source_open(native, number);
        g_free(native);
    }
    g_free(line);
    g_free(file);
}

/* EN: Create session's editable breakpoint list and connect its actions to dialog.
 * RU: Создаёт редактируемый список точек session и связывает действия с dialog. */
static GtkWidget *native_points_widget(NativeDebug *session, GtkWidget *dialog)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    session->point_rows = gtk_list_store_new(10, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    session->points_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(session->point_rows));
    gtk_widget_set_name(session->points_view, "insight-gdb-breakpoints");
    const gchar *titles[] = {
        "Function or file:line", "Number", "Enabled", "Type", "Condition",
        "Hit count", "Ignore first hits", "Function"
    };
    const guint columns[] = {0, 1, 2, 8, 3, 4, 5, 9};
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
    {
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            insight_tr(titles[i]), gtk_cell_renderer_text_new(), "text", columns[i], NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(session->points_view), column);
    }
    g_signal_connect(session->points_view, "row-activated", G_CALLBACK(native_point_open), NULL);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), session->points_view);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    session->point_controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *edits = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    const gchar *labels[] = {"Enable / Disable", "Delete Breakpoint", "Breakpoint condition", "Ignore first hits"};
    const gchar *actions[] = {"toggle", "delete", "condition", "ignore"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        g_object_set_data(G_OBJECT(button), "point-action", (gpointer)actions[i]);
        g_signal_connect(button, "clicked", G_CALLBACK(native_point_edit), dialog);
        gtk_box_pack_start(GTK_BOX(edits), button, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(session->point_controls), edits, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(session->point_controls), native_catch_widget(session), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), session->point_controls, FALSE, FALSE, 0);
    GtkWidget *hint = gtk_label_new(insight_tr(
        "Sub-locations require an enabled parent. Condition, ignored hits and Delete apply to the whole breakpoint."));
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_pack_start(GTK_BOX(page), hint, FALSE, FALSE, 0);
    return page;
}
