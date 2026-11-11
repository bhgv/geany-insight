/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: GDB MI fields, threads, stack and frame variables.
 * RU: Поля GDB MI, потоки, стек и переменные кадра. */

/* EN: Return the decoded value for name in record, ignoring quoted field-like text.
 * RU: Возвращает декодированное поле name из record, пропуская имена внутри строк. */
static gchar *native_debug_field(const gchar *record, const gchar *name)
{
    const gsize length = strlen(name);
    for (const gchar *p = record; *p; p++)
    {
        if (*p != '"')
            continue;
        const gchar *end = p + 1;
        while (*end && *end != '"')
        {
            if (*end == '\\' && end[1])
                end++;
            end++;
        }
        if (!*end)
            break;
        if (p - record >= (gssize)length + 2 && p[-1] == '=' &&
            (p[-2 - (gssize)length] == ',' || p[-2 - (gssize)length] == '{') &&
            !strncmp(p - 1 - length, name, length))
        {
            gchar *escaped = g_strndup(p + 1, end - p - 1);
            gchar *value = g_strcompress(escaped);
            g_free(escaped);
            return value;
        }
        p = end;
    }
    return NULL;
}

/* EN: Find the end of the next MI tuple in cursor, respecting strings and nesting.
 * RU: Ищет конец следующего кортежа MI из cursor с учётом строк и вложенности. */
static const gchar *native_next_tuple(const gchar **cursor, const gchar **start)
{
    guint depth = 0;
    gboolean quoted = FALSE;
    *start = NULL;
    for (const gchar *it = *cursor; *it; it++)
    {
        if (quoted && *it == '\\' && it[1])
        {
            it++;
            continue;
        }
        if (*it == '"')
            quoted = !quoted;
        if (quoted)
            continue;
        if (*it == '{' && depth++ == 0)
            *start = it;
        else if (*it == '}' && depth && --depth == 0)
        {
            *cursor = it + 1;
            return it + 1;
        }
        else if (*it == ']' && !depth)
            break;
    }
    return NULL;
}

/* EN: Return whether GDB's identifier is a nonempty decimal number.
 * RU: Проверяет, что идентификатор GDB состоит из десятичных цифр. */
static gboolean native_context_id(const gchar *value)
{
    return value && *value && strspn(value, "0123456789") == strlen(value);
}

/* EN: Fill session from MI record: threads chooses the thread picker instead of frames.
 * RU: Заполняет session из MI record: threads выбирает список потоков вместо кадров. */
static void native_fill_context(NativeDebug *session, const gchar *record, gboolean threads)
{
    const gchar *prefix = threads ? "^done,threads=[" : "^done,stack=[";
    const gchar *cursor = g_str_has_prefix(record, prefix) ? record + strlen(prefix) : "]";
    gchar *selected = threads ? (session->selected_thread ? g_strdup(session->selected_thread) :
                                  native_debug_field(record, "current-thread-id")) : NULL;
    session->filling_context = TRUE;
    if (threads)
    {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(session->threads));
        gtk_tree_store_clear(session->all_stack_rows);
        g_ptr_array_set_size(session->thread_ids, 0);
        g_hash_table_remove_all(session->thread_roots);
        session->all_stack_next = 0;
        session->all_stacks_needed = session->all_stacks_visible;
    }
    else
        gtk_list_store_clear(session->stack_rows);

    const gchar *start, *end;
    GtkTreePath *wanted = NULL;
    const gchar *desired = session->requested_frame ? session->requested_frame : session->selected_frame;
    guint count = 0;
    while (count++ < 256 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *id = native_debug_field(tuple, threads ? "id" : "level");
        gchar *function = native_debug_field(tuple, "func");
        if (native_context_id(id))
        {
            if (threads)
            {
                gchar *name = native_debug_field(tuple, "name");
                gchar *label = g_strdup_printf("#%s  %s", id, name ? name : function ? function : "");
                gchar *valid = g_utf8_make_valid(label, -1);
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(session->threads), id, valid);
                if (session->thread_ids->len < 64)
                {
                    GtkTreeIter *root = g_new(GtkTreeIter, 1);
                    gtk_tree_store_insert_with_values(session->all_stack_rows, root, NULL, -1,
                        0, valid, 5, id, -1);
                    g_hash_table_insert(session->thread_roots, g_strdup(id), root);
                    g_ptr_array_add(session->thread_ids, g_strdup(id));
                }
                g_free(valid);
                g_free(label);
                g_free(name);
            }
            else
            {
                gchar *path = native_debug_field(tuple, "fullname");
                gchar *line = native_debug_field(tuple, "line");
                gchar *display = path ? g_filename_display_name(path) : g_strdup("");
                gchar *label = g_utf8_make_valid(function ? function : "??", -1);
                GtkTreeIter row;
                gtk_list_store_insert_with_values(session->stack_rows, &row, -1,
                    0, id, 1, label, 2, display, 3, line, 4, path, -1);
                if (!g_strcmp0(id, desired))
                    wanted = gtk_tree_model_get_path(GTK_TREE_MODEL(session->stack_rows), &row);
                g_free(label);
                g_free(display);
                g_free(line);
                g_free(path);
            }
        }
        g_free(function);
        g_free(id);
        g_free(tuple);
    }
    if (threads && (!selected || !gtk_combo_box_set_active_id(GTK_COMBO_BOX(session->threads), selected)))
        gtk_combo_box_set_active(GTK_COMBO_BOX(session->threads), 0);
    if (threads)
    {
        g_free(session->selected_thread);
        session->selected_thread = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(session->threads)));
    }
    else
    {
        GtkTreePath *first = wanted ? wanted : gtk_tree_path_new_from_indices(0, -1);
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(session->stack)), first);
        gtk_tree_path_free(first);
    }
    session->filling_context = FALSE;
    g_free(selected);
}

/* EN: Return the selected GDB frame's level and optionally its source coordinates.
 * RU: Возвращает номер выбранного кадра GDB и при необходимости координаты исходника. */
static gchar *native_selected_frame(NativeDebug *session, gchar **filename, gchar **line)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(session->stack));
    GtkTreeModel *model;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(selection, &model, &row))
        return NULL;
    gchar *level = NULL;
    gtk_tree_model_get(model, &row, 0, &level, -1);
    if (filename)
        gtk_tree_model_get(model, &row, 4, filename, -1);
    if (line)
        gtk_tree_model_get(model, &row, 3, line, -1);
    return level;
}

/* EN: Refresh the stack or values after a context reply from GDB in record.
 * RU: Обновляет стек или значения после ответа GDB о контексте в record. */
static void native_context_reply(NativeDebug *session, const gchar *record)
{
    NativeContextRequest request = session->context_pending;
    session->context_pending = NATIVE_CONTEXT_NONE;
    if (request == NATIVE_CONTEXT_ALL_STACK)
    {
        native_all_stacks_reply(session, record);
        return;
    }
    if (request == NATIVE_CONTEXT_THREADS || request == NATIVE_CONTEXT_STACK)
        native_fill_context(session, record, request == NATIVE_CONTEXT_THREADS);
    if (!g_str_has_prefix(record, "^done"))
    {
        g_clear_pointer(&session->requested_frame, g_free);
        session->frame_select_needed = FALSE;
        if (request == NATIVE_CONTEXT_SELECT_THREAD)
        {
            session->filling_context = TRUE;
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(session->threads), session->selected_thread);
            session->filling_context = FALSE;
        }
        return;
    }
    if (request == NATIVE_CONTEXT_SELECT_THREAD)
    {
        session->stack_needed = TRUE;
        g_free(session->selected_thread);
        session->selected_thread = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(session->threads)));
        g_clear_pointer(&session->selected_frame, g_free);
        g_clear_pointer(&session->stop_file, g_free);
        session->stop_line = 0;
    }
    if (request == NATIVE_CONTEXT_SELECT_FRAME || request == NATIVE_CONTEXT_STACK)
    {
        gchar *path = NULL, *line = NULL;
        gchar *level = native_selected_frame(session, &path, &line);
        if (level)
        {
            g_free(session->selected_frame);
            session->selected_frame = g_strdup(level);
            if (request == NATIVE_CONTEXT_STACK && session->requested_frame)
            {
                session->locals_needed = TRUE;
                session->watch_next = 0;
                session->frame_select_needed = !strcmp(level, session->requested_frame);
                if (!session->frame_select_needed)
                    g_clear_pointer(&session->requested_frame, g_free);
            }
            g_free(session->stop_file);
            session->stop_file = g_steal_pointer(&path);
            guint64 number = 0;
            session->stop_line = line && g_ascii_string_to_unsigned(line, 10, 1, G_MAXINT, &number, NULL)
                                 ? (guint)number : 0;
        }
        g_free(level);
        g_free(path);
        g_free(line);
    }
    if ((request == NATIVE_CONTEXT_SELECT_FRAME || request == NATIVE_CONTEXT_STACK) &&
        session->stop_file && !session->stop_requested)
        native_source_stopped(session);
    if (request == NATIVE_CONTEXT_SELECT_FRAME)
        g_clear_pointer(&session->requested_frame, g_free);
    if (request == NATIVE_CONTEXT_SELECT_FRAME || request == NATIVE_CONTEXT_SELECT_THREAD)
    {
        session->locals_needed = TRUE;
        session->watch_next = 0;
    }
}

/* EN: Select the GDB thread chosen in combo for the dialog in data.
 * RU: Выбирает поток GDB из combo для диалога из data. */
static void native_select_thread(GtkComboBox *combo, gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    const gchar *id = gtk_combo_box_get_active_id(combo);
    if (session->filling_context || !session->stopped || session->pending || session->writing ||
        !native_context_id(id))
        return;
    gchar *command = g_strdup_printf("-thread-select %s", id);
    session->context_pending = NATIVE_CONTEXT_SELECT_THREAD;
    native_debug_send(dialog, command);
    g_free(command);
}

/* EN: Select the GDB frame in selection for the dialog in data.
 * RU: Выбирает кадр GDB из selection для диалога из data. */
static void native_select_frame(GtkTreeSelection *selection, gpointer data)
{
    (void)selection;
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (session->filling_context || !session->stopped || session->pending || session->writing)
        return;
    gchar *level = native_selected_frame(session, NULL, NULL);
    if (native_context_id(level))
    {
        gchar *command = g_strdup_printf("-stack-select-frame %s", level);
        session->context_pending = NATIVE_CONTEXT_SELECT_FRAME;
        native_debug_send(dialog, command);
        g_free(command);
    }
    g_free(level);
}

/* EN: Open the activated row's source location in the editor.
 * RU: Открывает исходник активированной строки стека в редакторе. */
static void native_open_frame(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column,
                              gpointer data)
{
    (void)column;
    (void)data;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter(model, &row, path))
        return;
    gchar *filename = NULL, *line = NULL;
    gtk_tree_model_get(model, &row, 4, &filename, 3, &line, -1);
    guint64 number;
    if (filename && line && g_ascii_string_to_unsigned(line, 10, 1, G_MAXINT, &number, NULL))
    {
        gchar *native = utils_get_locale_from_utf8(filename);
        diff_source_open(native, (guint)number);
        g_free(native);
    }
    g_free(filename);
    g_free(line);
}

/* EN: Read GDB MI variable tuples from record into session's table and frame cache.
 * RU: Читает кортежи переменных GDB MI из record в таблицу и кэш кадра session. */
static void native_debug_locals(NativeDebug *session, const gchar *record)
{
    g_ptr_array_set_size(session->locals, 0);
    gtk_list_store_clear(session->local_rows);
    const gchar *list = g_str_has_prefix(record, "^done,variables=[") ?
                        record + strlen("^done,variables=[") : NULL;
    if (!list)
        return;

    const gchar *start = NULL;
    gboolean quoted = FALSE;
    guint depth = 0;
    for (const gchar *it = list; *it && session->locals->len < 256; it++)
    {
        if (quoted && *it == '\\' && it[1])
        {
            it++;
            continue;
        }
        if (*it == '"')
            quoted = !quoted;
        if (quoted)
            continue;
        if (*it == '{')
        {
            if (depth++ == 0)
                start = it;
        }
        else if (*it == '}' && depth && --depth == 0)
        {
            gchar *tuple = g_strndup(start, it - start + 1);
            gchar *name = native_debug_field(tuple, "name");
            gchar *value = native_debug_field(tuple, "value");
            gchar *argument = native_debug_field(tuple, "arg");
            if (name)
            {
                NativeWatch *local = g_new0(NativeWatch, 1);
                local->expression = g_utf8_make_valid(name, -1);
                local->value = g_utf8_make_valid(value ? value : insight_tr("Unavailable"), -1);
                gtk_list_store_insert_with_values(session->local_rows, NULL, -1,
                    0, local->expression, 1, local->value,
                    2, insight_tr(!g_strcmp0(argument, "1") ? "Argument" : "Local variable"), -1);
                g_ptr_array_add(session->locals, local);
            }
            g_free(argument);
            g_free(value);
            g_free(name);
            g_free(tuple);
        }
        else if (*it == ']' && depth == 0)
            break;
    }
}

/* EN: Return a frame-local hover tooltip for filename and name in the active GDB session.
 * RU: Возвращает подсказку для имени name из текущего кадра GDB в файле filename. */
gchar *insight_debug_value_tooltip(const gchar *filename, const gchar *name)
{
    InsightDebugSession *active = active_debug_session();
    GtkWidget *dialog = active ? active->window : NULL;
    NativeDebug *session = dialog ? g_object_get_data(G_OBJECT(dialog), "native-debug") : NULL;
    if (!session || session->closed || !session->stopped || session->locals_pending || session->locals_needed ||
        !filename || !session->stop_file || utils_filenamecmp(filename, session->stop_file))
        return NULL;
    for (guint i = 0; i < session->locals->len; i++)
    {
        NativeWatch *local = session->locals->pdata[i];
        if (!strcmp(name, local->expression))
            return g_strdup_printf("%s = %s", local->expression, local->value);
    }
    return NULL;
}
