/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Source navigation, value tooltips and shortcuts.
 * RU: Навигация по исходникам, подсказки значений и клавиши. */

/* Dispatch standard debugger shortcuts in the debugger window. */
/* Обрабатывает стандартные сочетания клавиш в окне отладчика. */
static gboolean debug_handle_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    ScriptDebugWindow *view = data;
    GdkModifierType modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    if (event->keyval == GDK_KEY_Pause && modifiers == 0 && debug_is_running(view))
    {
        debug_pause(NULL, view);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_F5 && modifiers == GDK_SHIFT_MASK)
    {
        if (gtk_widget_get_sensitive(view->stop))
            debug_stop(NULL, view);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_F5 && modifiers == GDK_CONTROL_MASK)
    {
        debug_restart(NULL, view);
        return TRUE;
    }

    InsightLuaStep mode;
    if (event->keyval == GDK_KEY_F11 && modifiers == GDK_SHIFT_MASK)
        mode = INSIGHT_LUA_STEP_OUT;
    else if (modifiers == 0 && event->keyval == GDK_KEY_F11)
        mode = INSIGHT_LUA_STEP_INTO;
    else if (modifiers == 0 && event->keyval == GDK_KEY_F10)
        mode = INSIGHT_LUA_STEP_OVER;
    else if (modifiers == 0 && event->keyval == GDK_KEY_F5)
        mode = INSIGHT_LUA_CONTINUE;
    else
        return FALSE;

    if (gtk_widget_get_sensitive(view->step_controls))
        debug_window_step(view, mode);
    return TRUE;
}

/* Navigate only when the editor buffer still matches the debug snapshot. */
/* Переходит к строке, только если буфер редактора совпадает со снимком отладки. */
static void debug_go_to_source(GtkButton *button, gpointer data)
{
    (void)button;
    ScriptDebugWindow *view = data;
    GeanyDocument *document = document_find_by_id(view->document_id);
    const gchar *snapshot = view->source_snapshot;
    gsize snapshot_length = view->source_length;
    if (view->display_filename &&
        !debug_same_file(view->source_filename, view->display_filename))
    {
        snapshot = view->display_source;
        snapshot_length = view->display_length;
        document = document_find_by_filename(view->display_filename);
        if (!DOC_VALID(document) && snapshot && g_path_is_absolute(view->display_filename))
        {
            gchar *locale_path = utils_get_locale_from_utf8(view->display_filename);
            document = document_open_file(locale_path, FALSE, NULL, NULL);
            g_free(locale_path);
        }
    }
    if (!view->paused_line || !snapshot || !DOC_VALID(document))
    {
        gtk_label_set_text(GTK_LABEL(view->status), "The original buffer is no longer open.");
        return;
    }

    gint length = sci_get_length(document->editor->sci);
    gboolean unchanged = length >= 0 && (gsize)length == snapshot_length;
    if (unchanged)
    {
        gchar *text = sci_get_contents(document->editor->sci, length + 1);
        unchanged = memcmp(text, snapshot, snapshot_length) == 0;
        g_free(text);
    }
    if (!unchanged)
    {
        gtk_label_set_text(GTK_LABEL(view->status), "The buffer changed. Start a new debug session to follow it.");
        return;
    }

    navqueue_goto_line(document_get_current(), document, (gint)view->paused_line);
    document_grab_focus(document);
    gtk_window_present(GTK_WINDOW(main_widgets.window));
}

/* Build a read-only pane for source or stack information. */
/* Создаёт панель исходника или стека, доступную только для чтения. */
static GtkWidget *debug_create_view(GtkWidget *pane, gboolean first)
{
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *text = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text), TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    gtk_container_add(GTK_CONTAINER(scroll), text);
    if (first)
        gtk_paned_pack1(GTK_PANED(pane), scroll, TRUE, FALSE);
    else
        gtk_paned_pack2(GTK_PANED(pane), scroll, TRUE, FALSE);
    return text;
}

/* Recognize an ASCII byte used in a Lua identifier. */
/* Проверяет, может ли байт ASCII входить в идентификатор Lua. */
static gboolean debug_identifier_byte(gchar byte)
{
    return g_ascii_isalnum(byte) || byte == '_';
}

/* Select a visible local before looking for an upvalue of the same name. */
/* Ищет видимую локальную переменную раньше одноимённого захвата. */
static gint debug_hover_root(const InsightLuaWatchGraph *graph, const gchar *name, gsize length)
{
    const gchar *prefixes[] = {"local ", "upvalue ", "global "};
    for (guint prefix = 0; prefix < G_N_ELEMENTS(prefixes); prefix++)
    {
        gsize prefix_length = strlen(prefixes[prefix]);
        for (guint i = graph->edge_count; i > 0; i--)
        {
            const InsightLuaWatchEdge *edge = &graph->edges[i - 1];
            if (edge->from == 0 && strlen(edge->label) == prefix_length + length &&
                !strncmp(edge->label, prefixes[prefix], prefix_length) &&
                !memcmp(edge->label + prefix_length, name, length))
                return (gint)edge->to;
        }
    }
    return -1;
}

/* Resolve a raw identifier or dotted field path without evaluating Lua code. */
/* Находит идентификатор или цепочку полей без выполнения Lua-кода. */
static gint debug_hover_value(const InsightLuaWatchGraph *graph, const gchar *path, gsize length)
{
    if (!length || length > 192 || (!g_ascii_isalpha(*path) && *path != '_'))
        return -1;

    const gchar *end = path + length;
    const gchar *next = memchr(path, '.', length);
    gint node = debug_hover_root(graph, path, next ? (gsize)(next - path) : length);
    while (node >= 0 && next)
    {
        const gchar *field = next + 1;
        next = memchr(field, '.', (gsize)(end - field));
        gsize field_length = (gsize)((next ? next : end) - field);
        if (!field_length || (!g_ascii_isalpha(*field) && *field != '_'))
            return -1;

        gint child = -1;
        for (guint i = 0; i < graph->edge_count; i++)
        {
            const InsightLuaWatchEdge *edge = &graph->edges[i];
            if (edge->from == (guint)node && strlen(edge->label) == field_length + 2 &&
                edge->label[0] == '"' && edge->label[field_length + 1] == '"' &&
                !memcmp(edge->label + 1, field, field_length))
            {
                child = (gint)edge->to;
                break;
            }
        }
        node = child;
    }
    return node;
}

/* Resolve literal Python subscriptions using only the paused watch graph. */
/* Разрешает индексы и строковые ключи Python по снимку графа. */
static gint debug_hover_python_items(const InsightLuaWatchGraph *graph, gint node,
                                    const gchar *source, gsize length, gsize *end)
{
    while (*end < length && source[*end] == '[')
    {
        gsize cursor = *end + 1;
        if (cursor == length)
            break;

        gchar quote = source[cursor];
        gboolean string_key = quote == '\'' || quote == '"';
        gsize key_start = cursor + (string_key ? 1 : 0);
        cursor = key_start;
        if (string_key)
        {
            while (cursor < length && source[cursor] != quote &&
                   source[cursor] >= ' ' && source[cursor] <= '~' &&
                   source[cursor] != '\\' && source[cursor] != '"')
                cursor++;
        }
        else
        {
            while (cursor < length && g_ascii_isdigit(source[cursor]))
                cursor++;
        }

        gsize key_length = cursor - key_start;
        if (key_length > 32 || (!string_key && (!key_length ||
            (key_length > 1 && source[key_start] == '0'))))
            break;
        if (string_key && (cursor == length || source[cursor++] != quote))
            break;
        if (cursor == length || source[cursor] != ']')
            break;

        gchar opening = string_key ? '"' : '[';
        gchar closing = string_key ? '"' : ']';
        gint child = -1;
        for (guint i = 0; i < graph->edge_count; i++)
        {
            const InsightLuaWatchEdge *edge = &graph->edges[i];
            if (edge->from == (guint)node && strlen(edge->label) == key_length + 2 &&
                edge->label[0] == opening && edge->label[key_length + 1] == closing &&
                !memcmp(edge->label + 1, source + key_start, key_length))
            {
                child = (gint)edge->to;
                break;
            }
        }
        if (child < 0)
            break;
        node = child;
        *end = cursor + 1;
    }
    return node;
}

/* Show a paused value over an identifier in the numbered source view. */
/* Показывает значение на паузе при наведении на идентификатор исходника. */
static gboolean debug_source_tooltip(GtkWidget *widget, gint x, gint y, gboolean keyboard,
                                     GtkTooltip *tooltip, gpointer data)
{
    ScriptDebugWindow *view = data;
    if (keyboard || !view->watch_graph || !view->source_code_mask || !view->paused_line)
        return FALSE;

    GtkTextView *source_view = GTK_TEXT_VIEW(widget);
    gint buffer_x, buffer_y;
    gtk_text_view_window_to_buffer_coords(source_view, GTK_TEXT_WINDOW_WIDGET, x, y,
                                          &buffer_x, &buffer_y);
    GtkTextIter location;
    if (!gtk_text_view_get_iter_at_location(source_view, &location, buffer_x, buffer_y))
        return FALSE;

    guint line = (guint)gtk_text_iter_get_line(&location);
    if (line >= view->source_line_offsets->len)
        return FALSE;
    gchar number[24];
    gint prefix_length = g_snprintf(number, sizeof number, "%5u  ", line + 1);
    gint byte_in_line = gtk_text_iter_get_line_index(&location) - prefix_length;
    if (byte_in_line < 0)
        return FALSE;

    gsize line_start = g_array_index(view->source_line_offsets, gsize, line);
    gsize hover = line_start + (gsize)byte_in_line;
    const gchar *mask = view->source_code_mask;
    gsize source_length = view->display_length;
    if (hover >= source_length || !debug_identifier_byte(mask[hover]))
        return FALSE;

    gsize start = hover;
    gsize end = hover + 1;
    while (start > line_start && debug_identifier_byte(mask[start - 1]))
        start--;
    while (end < source_length && debug_identifier_byte(mask[end]))
        end++;
    if (view->python && start > line_start && mask[start - 1] == '.')
        return FALSE;
    while (start > line_start + 1 && mask[start - 1] == '.' &&
           debug_identifier_byte(mask[start - 2]))
    {
        start -= 2;
        while (start > line_start && debug_identifier_byte(mask[start - 1]))
            start--;
    }

    gint node = debug_hover_value(view->watch_graph, mask + start, end - start);
    if (node < 0)
        return FALSE;

    if (view->python)
        node = debug_hover_python_items(view->watch_graph, node, view->display_source, source_length, &end);

    gchar *text = g_strdup_printf("%.*s = %s", (gint)(end - start), view->display_source + start,
                                  view->watch_graph->nodes[node].value);
    gchar *valid = g_utf8_make_valid(text, -1);
    gtk_tooltip_set_text(tooltip, valid);
    g_free(valid);
    g_free(text);
    return TRUE;
}

/* Number source lines using the selected interpreter's newline rules. */
/* Нумерует строки по правилам перевода строки выбранного интерпретатора. */
static gchar *debug_number_source(const gchar *source, gsize length, GArray *line_offsets, gboolean python)
{
    GString *numbered = g_string_sized_new(length + 128);
    const gchar *start = source;
    for (guint line = 1; ; line++)
    {
        const gchar *end = strpbrk(start, "\r\n");
        gsize offset = (gsize)(start - source);
        g_array_append_val(line_offsets, offset);
        g_string_append_printf(numbered, "%5u  ", line);
        if (!end)
        {
            g_string_append(numbered, start);
            break;
        }
        g_string_append_len(numbered, start, end - start);
        g_string_append_c(numbered, '\n');
        start = end + 1;
        if ((*end == '\r' && *start == '\n') || (!python && *end == '\n' && *start == '\r'))
            start++;
    }
    return g_string_free(numbered, FALSE);
}

/* Choose a source file without changing the debugged entry point. */
/* Выбирает файл Python, не меняя точку входа отладки. */
/* EN: Select a script in a modal child of parent with title. root is the initial
 * directory and python selects the filter. Return an owned path or NULL on cancel.
 * RU: Выбирает скрипт в модальном окне parent с заголовком title. root задаёт
 * начальный каталог, python выбирает фильтр. Возвращает новый путь или NULL. */
static gchar *debug_choose_script_file(GtkWindow *parent, const gchar *title, const gchar *root, gboolean python)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new(title, parent, GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, python ? "Python files" : "Lua files");
    if (python)
    {
        gtk_file_filter_add_pattern(filter, "*.[pP][yY]");
        gtk_file_filter_add_pattern(filter, "*.[pP][yY][wW]");
    }
    else
        gtk_file_filter_add_pattern(filter, "*.[lL][uU][aA]");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    if (root && g_path_is_absolute(root))
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), root);
    gchar *filename = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT ?
        gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)) : NULL;
    gtk_widget_destroy(dialog);
    return filename;
}

/* Edit breakpoints in a file before execution has reached it. */
/* Задаёт точки останова в файле до первого перехода к нему. */
static void debug_breakpoints_in_file(GtkButton *button, gpointer data)
{
    (void)button;
    ScriptDebugWindow *view = data;
    if (debug_is_running(view))
        return;

    gchar *locale_filename = debug_choose_script_file(GTK_WINDOW(view->window),
        "Select a file for breakpoints", view->project_root, view->python);
    if (!locale_filename)
        return;
    gchar *filename = utils_get_utf8_from_locale(locale_filename);
    g_free(locale_filename);
    if (!filename)
        return;

    gboolean displayed = debug_same_file(view->display_filename, filename);
    gchar *key = debug_file_key(filename);
    const gchar *saved = displayed ? gtk_entry_get_text(GTK_ENTRY(view->breakpoints)) :
        g_hash_table_lookup(view->file_breakpoints, key);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Breakpoints in File", GTK_WINDOW(view->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Apply", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 2048);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Breakpoint lines, or empty to clear");
    gtk_entry_set_text(GTK_ENTRY(entry), saved ? saved : "");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(filename);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
        unsigned long lines[128];
        gsize count = 0;
        if (debug_parse_breakpoints(text, lines, &count))
        {
            debug_store_file_breakpoints(view, filename, text);
            if (displayed)
                gtk_entry_set_text(GTK_ENTRY(view->breakpoints), text);
        }
        else
            gtk_label_set_text(GTK_LABEL(view->status), "Invalid breakpoint line list");
    }
    gtk_widget_destroy(dialog);
    g_free(key);
    g_free(filename);
}
