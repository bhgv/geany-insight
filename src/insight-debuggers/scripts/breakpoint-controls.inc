/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Breakpoint editing, lists and source margins.
 * RU: Редактирование точек останова, списки и поля исходника. */

/* Add or remove a breakpoint without disturbing the other line numbers. */
/* Добавляет или удаляет точку останова, сохраняя остальные номера строк. */
static gboolean debug_toggle_breakpoint(unsigned long *lines, gsize *count,
                                        gsize capacity, unsigned long line)
{
    gboolean found = FALSE;
    gsize retained = 0;
    for (gsize i = 0; i < *count; i++)
    {
        if (lines[i] == line)
            found = TRUE;
        else
            lines[retained++] = lines[i];
    }
    if (found)
    {
        *count = retained;
        return TRUE;
    }
    if (*count == capacity)
        return FALSE;
    lines[(*count)++] = line;
    return TRUE;
}

/* Mark breakpoint line numbers when the entry changes. */
/* Отмечает номера строк с точками останова при изменении поля. */
static void debug_mark_breakpoints(GtkEditable *entry, gpointer data)
{
    ScriptDebugWindow *view = data;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->source_view));
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    gtk_text_buffer_remove_tag(buffer, view->breakpoint_tag, &first, &last);
    gtk_text_buffer_remove_tag(buffer, view->conditional_point_tag, &first, &last);

    unsigned long lines[128];
    gsize count = 0;
    if (!debug_parse_breakpoints(gtk_entry_get_text(GTK_ENTRY(entry)), lines, &count))
        count = 0;

    guint line_count = (guint)gtk_text_buffer_get_line_count(buffer);
    for (gsize i = 0; i < count; i++)
    {
        if (lines[i] > line_count)
            continue;
        gtk_text_buffer_get_iter_at_line(buffer, &first, (gint)lines[i] - 1);
        last = first;
        gtk_text_iter_forward_chars(&last, 7);
        gtk_text_buffer_apply_tag(buffer, view->breakpoint_tag, &first, &last);
    }
    gchar *filename = debug_file_key(view->display_filename);
    GHashTableIter points;
    gpointer value;
    g_hash_table_iter_init(&points, view->conditional_points);
    while (g_hash_table_iter_next(&points, NULL, &value))
    {
        const DebugConditionalPoint *point = value;
        if (point->line > line_count || strcmp(point->filename, filename))
            continue;
        gtk_text_buffer_get_iter_at_line(buffer, &first, (gint)point->line - 1);
        last = first;
        gtk_text_iter_forward_chars(&last, 7);
        gtk_text_buffer_apply_tag(buffer, view->conditional_point_tag, &first, &last);
    }
    g_free(filename);
}

/* Persist valid edits immediately, including edits made before the first step. */
/* Сохраняет корректные правки сразу, в том числе до первого шага. */
static void debug_breakpoint_entry_changed(GtkEditable *entry, gpointer data)
{
    ScriptDebugWindow *view = data;
    debug_store_file_breakpoints(view, view->display_filename, gtk_entry_get_text(GTK_ENTRY(entry)));
    debug_mark_breakpoints(entry, data);
}

/* Apply a point edit transactionally so a bad expression keeps the old rule. */
/* Применяет правку точки так, чтобы ошибка в условии сохранила прежнее правило. */
static gboolean debug_set_conditional_point(ScriptDebugWindow *view, const gchar *key,
                                            DebugConditionalPoint *point, gchar *error, gsize capacity)
{
    gpointer old_key = NULL;
    gpointer old_point = NULL;
    if (g_hash_table_lookup_extended(view->conditional_points, key, &old_key, &old_point))
        g_hash_table_steal(view->conditional_points, key);
    if (point)
        g_hash_table_insert(view->conditional_points, g_strdup(key), point);

    gboolean applied = view->python ?
        debug_apply_python_observer(&view->python_observer, view->stop_condition, view->observer_source,
                                    view->conditional_points, error, capacity) :
        debug_apply_lua_observer(view->engine, view->stop_condition, view->observer_source,
                                 view->conditional_points, error, capacity);
    if (!applied)
    {
        g_hash_table_remove(view->conditional_points, key);
        if (old_key)
            g_hash_table_insert(view->conditional_points, old_key, old_point);
        return FALSE;
    }
    g_free(old_key);
    const gchar *group = old_point ? ((DebugConditionalPoint *)old_point)->saved_group : NULL;
    if (point)
    {
        point->saved_group = group ? g_strdup(group) : g_uuid_string_random();
        group = point->saved_group;
    }
    if (group)
        debug_save_conditional_point(view, key, group);
    if (old_point)
        debug_conditional_point_free(old_point);
    return TRUE;
}

/* Edit the expression, hit threshold and action of a point on this line. */
/* Настраивает условие, порог срабатываний и действие точки на этой строке. */
static void debug_edit_conditional_point(ScriptDebugWindow *view, const gchar *source_file, unsigned long line)
{
    gchar *filename = debug_file_key(source_file);
    gchar *key = debug_conditional_point_key(filename, line);
    const DebugConditionalPoint *saved = g_hash_table_lookup(view->conditional_points, key);
    if (!saved && g_hash_table_size(view->conditional_points) >= 32)
    {
        gtk_label_set_text(GTK_LABEL(view->status), "The debugger supports up to 32 conditional points.");
        g_free(key);
        g_free(filename);
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Conditional Breakpoint", GTK_WINDOW(view->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Remove Rule", GTK_RESPONSE_REJECT, "_Apply", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_REJECT, saved != NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 540, -1);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gchar *location = g_strdup_printf("%s:%lu", filename, line);
    GtkWidget *label = gtk_label_new(location);
    g_free(location);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 4);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 1024);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Lua expression, e.g. event.locals.counter == 10");
    gtk_entry_set_text(GTK_ENTRY(entry), saved ? saved->expression : "");
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 4);

    GtkWidget *threshold = gtk_spin_button_new_with_range(1, 1000000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(threshold), saved ? saved->minimum_hits : 1);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Start acting after this many matching hits"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), threshold, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 4);
    GtkWidget *log_only = gtk_check_button_new_with_label("Log hits and continue");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(log_only), saved && saved->log_only);
    gtk_box_pack_start(GTK_BOX(box), log_only, FALSE, FALSE, 4);
    GtkWidget *status = gtk_label_new(NULL);
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 4);
    gtk_widget_show_all(dialog);

    for (;;)
    {
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response != GTK_RESPONSE_ACCEPT && response != GTK_RESPONSE_REJECT)
            break;
        DebugConditionalPoint *point = NULL;
        if (response == GTK_RESPONSE_ACCEPT)
        {
            point = g_new0(DebugConditionalPoint, 1);
            point->filename = g_strdup(filename);
            point->line = line;
            point->expression = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
            if (!*g_strstrip(point->expression))
            {
                g_free(point->expression);
                point->expression = g_strdup("true");
            }
            point->minimum_hits = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(threshold));
            point->log_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(log_only));
            point->disabled = saved && saved->disabled;
        }
        gchar error[1024] = "";
        if (debug_set_conditional_point(view, key, point, error, sizeof error))
        {
            debug_mark_breakpoints(GTK_EDITABLE(view->breakpoints), view);
            InsightLuaDebug *observer = view->python ? view->python_observer : view->engine;
            debug_set_text(view->observer_log, insight_lua_debug_observer_log(observer));
            break;
        }
        gchar *message = g_utf8_make_valid(*error ? error : "Cannot configure this point.", -1);
        gtk_label_set_text(GTK_LABEL(status), message);
        g_free(message);
    }
    gtk_widget_destroy(dialog);
    g_free(key);
    g_free(filename);
}

/* Add a row with a stable file-and-line identity to the point selector. */
/* Добавляет строку списка с однозначным ключом файла и номера строки. */
static void debug_add_point_row(GtkComboBoxText *choices, const gchar *filename, unsigned long line,
                                const gchar *kind, gboolean disabled)
{
    gchar *key = debug_conditional_point_key(filename, line);
    gchar *label = g_strdup_printf("%s:%lu  [%s%s]", filename, line, kind, disabled ? ", off" : "");
    gchar *valid = g_utf8_make_valid(label, -1);
    gtk_combo_box_text_append(choices, key, valid);
    g_free(valid);
    g_free(label);
    g_free(key);
}

/* Open a point in the editor without changing the paused frame. */
/* Открывает строку в редакторе без смены кадра на паузе. */
static void debug_open_point_source(ScriptDebugWindow *view, const gchar *filename, gint line)
{
    GeanyDocument *document = g_path_is_absolute(filename) ? document_find_by_filename(filename) :
        document_find_by_id(view->document_id);
    if (!DOC_VALID(document) && g_path_is_absolute(filename))
    {
        gchar *local = utils_get_locale_from_utf8(filename);
        if (local)
            document = document_open_file(local, FALSE, NULL, NULL);
        g_free(local);
    }
    if (!DOC_VALID(document) || line > sci_get_line_count(document->editor->sci))
    {
        gtk_label_set_text(GTK_LABEL(view->status), "Breakpoint source line is unavailable.");
        return;
    }
    navqueue_goto_line(document_get_current(), document, line);
    document_grab_focus(document);
    gtk_window_present(GTK_WINDOW(main_widgets.window));
}

/* Manage ordinary, conditional and logging breakpoints in one list. */
/* Управляет обычными, условными и журнальными точками в одном списке. */
static void debug_list_points(GtkButton *button, gpointer data)
{
    (void)button;
    ScriptDebugWindow *view = data;
    if (debug_is_running(view))
        return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Breakpoints", GTK_WINDOW(view->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE,
        "_Enable / Disable", GTK_RESPONSE_APPLY, "_Edit Rule", GTK_RESPONSE_REJECT,
        "_Open Source", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 650, -1);
    GtkComboBoxText *choices = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       GTK_WIDGET(choices), TRUE, TRUE, 8);
    GHashTableIter iter;
    gpointer filename, text;
    g_hash_table_iter_init(&iter, view->file_breakpoints);
    while (g_hash_table_iter_next(&iter, &filename, &text))
    {
        unsigned long lines[128];
        gsize count = 0;
        if (!debug_parse_breakpoints(text, lines, &count))
            continue;
        for (gsize i = 0; i < count; i++)
        {
            gchar *key = debug_conditional_point_key(filename, lines[i]);
            if (!g_hash_table_contains(view->conditional_points, key))
                debug_add_point_row(choices, filename, lines[i], "Stop",
                                    g_hash_table_contains(view->disabled_plain_points, key));
            g_free(key);
        }
    }
    gpointer value;
    g_hash_table_iter_init(&iter, view->conditional_points);
    while (g_hash_table_iter_next(&iter, NULL, &value))
    {
        const DebugConditionalPoint *point = value;
        debug_add_point_row(choices, point->filename, point->line,
                            point->log_only ? "Log" : "Conditional", point->disabled);
    }

    gtk_combo_box_set_active(GTK_COMBO_BOX(choices), 0);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gchar *selected = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(choices)));
    gtk_widget_destroy(dialog);
    if (!selected)
        return;

    gchar *separator = strchr(selected, ':');
    if (separator)
    {
        unsigned long line = (unsigned long)g_ascii_strtoull(selected, NULL, 10);
        if (response == GTK_RESPONSE_ACCEPT)
            debug_open_point_source(view, separator + 1, (gint)line);
        else if (response == GTK_RESPONSE_REJECT)
            debug_edit_conditional_point(view, separator + 1, line);
        else if (response == GTK_RESPONSE_APPLY)
        {
            const DebugConditionalPoint *point = g_hash_table_lookup(view->conditional_points, selected);
            if (point)
            {
                DebugConditionalPoint *edited = g_new0(DebugConditionalPoint, 1);
                edited->filename = g_strdup(point->filename);
                edited->expression = g_strdup(point->expression);
                edited->line = point->line;
                edited->minimum_hits = point->minimum_hits;
                edited->log_only = point->log_only;
                edited->disabled = !point->disabled;
                gchar error[1024] = "";
                if (!debug_set_conditional_point(view, selected, edited, error, sizeof error))
                {
                    gchar *message = g_utf8_make_valid(*error ? error : "Cannot update breakpoint", -1);
                    gtk_label_set_text(GTK_LABEL(view->status), message);
                    g_free(message);
                }
            }
            else if (!g_hash_table_remove(view->disabled_plain_points, selected))
                g_hash_table_add(view->disabled_plain_points, g_strdup(selected));
            debug_mark_breakpoints(GTK_EDITABLE(view->breakpoints), view);
        }
    }
    g_free(selected);
}

/* EN: Toggle one-based line in filename in the Lua or Python session in data.
 * RU: Переключает точку на строке line файла filename в сеансе Lua или Python из data. */
static void debug_script_toggle(const gchar *filename, guint line, gpointer data)
{
    ScriptDebugWindow *view = data;
    gchar *key = debug_file_key(filename);
    gchar *display = debug_file_key(view->display_filename);
    gboolean current = !g_strcmp0(key, display);
    g_free(display);
    const gchar *text = current ? gtk_entry_get_text(GTK_ENTRY(view->breakpoints)) :
                                  g_hash_table_lookup(view->file_breakpoints, key);
    unsigned long lines[128];
    gsize count = 0;
    if (!debug_parse_breakpoints(text ? text : "", lines, &count))
    {
        gtk_label_set_text(GTK_LABEL(view->status), "Correct the breakpoint list before adding a line.");
        g_free(key);
        return;
    }

    gsize previous_count = count;
    if (!debug_toggle_breakpoint(lines, &count, G_N_ELEMENTS(lines), line))
    {
        gtk_label_set_text(GTK_LABEL(view->status), "The debugger supports up to 128 breakpoints.");
        g_free(key);
        return;
    }

    GString *list = g_string_new(NULL);
    for (gsize i = 0; i < count; i++)
    {
        if (i)
            g_string_append(list, ", ");
        g_string_append_printf(list, "%lu", lines[i]);
    }
    if (current)
        gtk_entry_set_text(GTK_ENTRY(view->breakpoints), list->str);
    else
        debug_store_file_breakpoints(view, filename, list->str);
    const gchar *message = count < previous_count ? "Source breakpoint removed." : "Source breakpoint added.";
    gtk_label_set_text(GTK_LABEL(view->status), insight_tr(message));
    ui_set_statusbar(FALSE, "%s", insight_tr(message));
    g_string_free(list, TRUE);
    g_free(key);
}

/* Toggle a breakpoint when the source line number is clicked. */
/* Переключает точку останова при щелчке по номеру строки. */
static gboolean debug_click_line_number(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    if (event->type != GDK_BUTTON_PRESS || event->button != 1)
        return FALSE;

    ScriptDebugWindow *view = data;
    if (debug_is_running(view))
        return FALSE;

    GtkTextView *source = GTK_TEXT_VIEW(widget);
    GtkTextWindowType window_type = gtk_text_view_get_window_type(source, event->window);
    if (window_type != GTK_TEXT_WINDOW_TEXT)
        return FALSE;
    gint x, y;
    gtk_text_view_window_to_buffer_coords(source, window_type, (gint)event->x, (gint)event->y, &x, &y);
    GtkTextIter location;
    if (!gtk_text_view_get_iter_at_location(source, &location, x, y) ||
        gtk_text_iter_get_line_offset(&location) >= 7)
        return FALSE;

    guint line = (guint)gtk_text_iter_get_line(&location) + 1;
    if (event->state & GDK_SHIFT_MASK)
    {
        debug_edit_conditional_point(view, view->display_filename, line);
        return TRUE;
    }

    debug_script_toggle(view->display_filename, line, view);
    return TRUE;
}
