/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Line movement and clipboard line commands.
 * RU: Перемещение строк и строковые операции буфера обмена. */
/* Move with Scintilla's native operation, retaining its line/marker handling.
 * Reindent only after bounded preparation, and keep the complete move in one Undo. */
/* EN: Move sci lines in direction by bounded count with native markers and one Undo; return FALSE for unsupported input.
 * RU: Перемещает строки sci в direction на ограниченный count, сохраняя маркеры и один Undo; FALSE при недопустимом вводе. */
gboolean insight_paste_move_lines(ScintillaObject *sci, gint direction, guint count)
{
    if (!allowed(sci) || (direction != -1 && direction != 1) || !count || count > 5)
        return FALSE;
    gint start = sci_get_selection_start(sci), end = sci_get_selection_end(sci);
    gint first = sci_get_line_from_position(sci, start);
    gint last = sci_get_line_from_position(sci, end);
    if (end > start && end == sci_get_position_from_line(sci, last))
        last--;
    guint lines = last - first + 1;
    if (lines > 10000)
        return FALSE;
    gint total = sci_get_line_count(sci);
    count = MIN(count, (guint)(direction < 0 ? first : total - last - 1));
    if (!count)
        return TRUE;
    gint begin = sci_get_position_from_line(sci, first);
    gint finish = last + 1 < total ? sci_get_position_from_line(sci, last + 1) : sci_get_length(sci);
    if (finish <= begin || finish - begin > 1024 * 1024)
        return FALSE;
    gchar *original = sci_get_contents_range(sci, begin, finish);
    const gchar *eol = sci_get_eol_mode(sci) == SC_EOL_CRLF ? "\r\n" :
                      sci_get_eol_mode(sci) == SC_EOL_CR ? "\r" : "\n";
    gsize length = strlen(original);
    gchar *whole = length && (original[length-1] == '\r' || original[length-1] == '\n') ?
                   g_strdup(original) : g_strconcat(original, eol, NULL);
    g_free(original);
    guint tab = SEND(sci, SCI_GETTABWIDTH, 0, 0);
    guint target = SEND(sci, SCI_GETLINEINDENTATION,
                        direction < 0 ? first - (gint)count : last + (gint)count, 0);
    gchar *prepared = insight_paste_reindent(whole, target, tab,
                                           SEND(sci, SCI_GETUSETABS, 0, 0), eol);
    g_free(whole);
    if (!prepared)
        return FALSE;
    GArray *indents = g_array_new(FALSE, FALSE, sizeof(guint));
    const gchar *p = prepared;
    while (*p)
    {
        guint columns = 0;
        while (*p == ' ' || *p == '\t')
        {
            columns += *p == '\t' ? tab - columns % tab : 1;
            p++;
        }
        g_array_append_val(indents, columns);
        while (*p && *p != '\r' && *p != '\n') p++;
        if (*p == '\r' && p[1] == '\n') p += 2;
        else if (*p) p++;
    }
    g_free(prepared);
    if (indents->len != lines)
    {
        g_array_free(indents, TRUE);
        return FALSE;
    }
    gboolean selection = start != end;
    gboolean forward = SEND(sci, SCI_GETANCHOR, 0, 0) <= sci_get_current_position(sci);
    gint caret = sci_get_current_position(sci);
    gint caret_line = sci_get_line_from_position(sci, caret);
    gint body_offset = MAX(0, caret - SEND(sci, SCI_GETLINEINDENTPOSITION, caret_line, 0));
    GeanyDocument *doc = document_for_sci(sci);
    insight_completion_cancel(doc->editor);
    SEND(sci, SCI_AUTOCCANCEL, 0, 0);
    sci_start_undo_action(sci);
    guint moved = 0;
    for (; moved < count; moved++)
    {
        guint64 generation = state(sci)->generation;
        SEND(sci, direction < 0 ? SCI_MOVESELECTEDLINESUP : SCI_MOVESELECTEDLINESDOWN, 0, 0);
        if (state(sci)->generation == generation)
            break;
    }
    /* Avoid guessing a destination if native movement stopped at a boundary. */
    if (moved == count)
    {
        first += direction * (gint)moved;
        for (guint i = 0; i < lines; i++)
            SEND(sci, SCI_SETLINEINDENTATION, first + i, g_array_index(indents, guint, i));
        if (selection)
        {
            gint a = sci_get_position_from_line(sci, first);
            gint b = first + (gint)lines < sci_get_line_count(sci) ?
                     sci_get_position_from_line(sci, first + lines) : sci_get_length(sci);
            SEND(sci, SCI_SETSEL, forward ? a : b, forward ? b : a);
        }
        else
        {
            caret_line += direction * (gint)moved;
            gint pos = MIN(SEND(sci, SCI_GETLINEENDPOSITION, caret_line, 0),
                           SEND(sci, SCI_GETLINEINDENTPOSITION, caret_line, 0) + body_offset);
            SEND(sci, SCI_SETSEL, pos, pos);
        }
    }
    sci_end_undo_action(sci);
    SEND(sci, SCI_SCROLLCARET, 0, 0);
    g_array_free(indents, TRUE);
    return TRUE;
}
/* GTK owns a copy of the validated text before any document edit occurs. */
/* EN: Execute line clipboard command for sci; return FALSE when unsupported, TRUE when handled, including read-only/empty boundaries.
 * RU: Выполняет строковую команду command для sci; FALSE при неподдерживаемом вводе, TRUE при обработке, включая пустые границы. */
gboolean insight_line_clipboard(ScintillaObject *sci, InsightLineClipboard command)
{
    GeanyDocument *doc = document_for_sci(sci);
    if (!doc || SEND(sci, SCI_GETSELECTIONS, 0, 0) != 1 ||
        sci_get_selection_mode(sci) != SC_SEL_STREAM ||
        SEND(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0) ||
        SEND(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0))
        return FALSE;
    gboolean cut = command != INSIGHT_COPY_LINE && command != INSIGHT_COPY_LINE_RIGHT;
    if (cut && SEND(sci, SCI_GETREADONLY, 0, 0))
        return TRUE;
    gint caret = sci_get_current_position(sci);
    gint line = sci_get_line_from_position(sci, caret);
    gint line_start = sci_get_position_from_line(sci, line);
    gint line_end = SEND(sci, SCI_GETLINEENDPOSITION, line, 0);
    gint length = sci_get_length(sci), total = sci_get_line_count(sci);
    gint begin = sci_get_selection_start(sci), end = sci_get_selection_end(sci);
    switch (command)
    {
        case INSIGHT_COPY_LINE:
        {
            gint first = sci_get_line_from_position(sci, begin);
            gint last = sci_get_line_from_position(sci, end);
            gboolean selected = begin != end;
            if (selected && end == sci_get_position_from_line(sci, last))
                last--;
            gint a = sci_get_position_from_line(sci, first);
            gint b = last + 1 < total ? sci_get_position_from_line(sci, last + 1) : length;
            /* Already whole lines: the next Copy Line adds the following line. */
            if (selected && begin == a && end == b && last + 1 < total)
                b = last + 2 < total ? sci_get_position_from_line(sci, last + 2) : length;
            begin = a;
            end = b;
            break;
        }
        case INSIGHT_CUT_LINE:
            begin = line_start;
            end = line + 1 < total ? sci_get_position_from_line(sci, line + 1) : length;
            break;
        case INSIGHT_COPY_LINE_RIGHT:
        case INSIGHT_CUT_LINE_RIGHT:
            begin = MIN(caret, line_end);
            end = line_end;
            break;
        case INSIGHT_CUT_LINE_LEFT:
            begin = line_start;
            end = MIN(caret, line_end);
            break;
        case INSIGHT_CUT_SELECTION_OR_PASTE:
            if (begin == end)
            {
                sci_paste(sci);
                return TRUE;
            }
            break;
        default:
            return FALSE;
    }
    /* Empty boundaries leave the previous clipboard intact and create no Undo. */
    if (end <= begin)
        return TRUE;
    if (end - begin > 1024 * 1024)
    {
        ui_set_statusbar(TRUE, "Line clipboard operation exceeds 1 MiB.");
        return TRUE;
    }
    gchar *text = sci_get_contents_range(sci, begin, end);
    if (!text || strlen(text) != (gsize)(end - begin) || !g_utf8_validate(text, end - begin, NULL))
    {
        g_free(text);
        ui_set_statusbar(TRUE, "Line clipboard operation requires complete UTF-8 text without NUL bytes.");
        return TRUE;
    }
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, end - begin);
    g_free(text);
    if (cut)
    {
        insight_completion_cancel(doc->editor);
        SEND(sci, SCI_AUTOCCANCEL, 0, 0);
        sci_start_undo_action(sci);
        SEND(sci, SCI_SETSEL, begin, end);
        sci_replace_sel(sci, "");
        sci_end_undo_action(sci);
    }
    else if (command == INSIGHT_COPY_LINE)
        SEND(sci, SCI_SETSEL, begin, end);
    return TRUE;
}
/* EN: Activate clipboard operation encoded in data for the current document; item is unused, invalid documents are ignored.
 * RU: Выполняет операцию буфера из data для текущего документа; item не используется, недействительные документы пропускаются. */
static void clipboard_command(GtkMenuItem *item, gpointer data)
{
    (void)item;
    GeanyDocument *doc = document_get_current();
    if (doc && doc->is_valid)
    {
        insight_line_clipboard(doc->editor->sci, GPOINTER_TO_INT(data));
        gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
    }
}
/* EN: Execute movement or Paste Line encoded in data; item is unused, unsupported destinations are unchanged.
 * RU: Выполняет перемещение либо вставку строки из data; item не используется, неподдерживаемое назначение не меняется. */
static void line_command(GtkMenuItem *item, gpointer data)
{
    (void)item;
    GeanyDocument *doc = document_get_current();
    if (!doc || !doc->is_valid)
        return;
    ScintillaObject *sci = doc->editor->sci;
    if (SEND(sci, SCI_GETREADONLY, 0, 0) || SEND(sci, SCI_GETSELECTIONS, 0, 0) != 1 ||
        sci_get_selection_mode(sci) != SC_SEL_STREAM ||
        SEND(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0) ||
        SEND(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0))
        return;
    gint command = GPOINTER_TO_INT(data);
    if (!command)
    {
        gint line = sci_get_line_from_position(sci, sci_get_current_position(sci));
        gint pos = sci_get_position_from_line(sci, line);
        SEND(sci, SCI_SETSEL, pos, pos);
        sci_paste(sci);
    }
    else
    {
        gint direction = command < 0 ? -1 : 1;
        guint count = ABS(command);
        if (!insight_paste_move_lines(sci, direction, count))
        {
            sci_start_undo_action(sci);
            for (guint i = 0; i < count; i++)
                SEND(sci, direction < 0 ? SCI_MOVESELECTEDLINESUP : SCI_MOVESELECTEDLINESDOWN, 0, 0);
            sci_end_undo_action(sci);
        }
    }
    gtk_widget_grab_focus(GTK_WIDGET(sci));
}
/* EN: Append line movement and clipboard commands to menu; invalid menu triggers a GLib precondition warning.
 * RU: Добавляет строковые команды и буфер в menu; неверное меню вызывает предупреждение предусловия GLib. */
void insight_paste_line_menu(GtkWidget *menu)
{
    g_return_if_fail(GTK_IS_MENU_SHELL(menu));
    GtkWidget *parent = gtk_menu_item_new_with_label("Line operations");
    gtk_widget_set_name(parent, "insight-line-operations");
    GtkWidget *sub = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), sub);
    const gchar *labels[] = {"Paste Line", "Drag Line Up", "Drag Line Down", "Drag Line Up More", "Drag Line Down More"};
    const gchar *ids[] = {"paste-line", "drag-line-up", "drag-line-down", "drag-line-up-more", "drag-line-down-more"};
    const gint commands[] = {0, -1, 1, -5, 5};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *entry = gtk_menu_item_new_with_label(labels[i]);
        gchar *name = g_strconcat("insight-", ids[i], NULL);
        gtk_widget_set_name(entry, name);
        g_free(name);
        if (i > 2) gtk_widget_set_tooltip_text(entry, "Move up to five lines, stopping at the document boundary");
        g_signal_connect(entry, "activate", G_CALLBACK(line_command), GINT_TO_POINTER(commands[i]));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub), entry);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(sub), gtk_separator_menu_item_new());
    const gchar *clipboard_labels[] = {"Copy Line", "Cut Line", "Copy Line Right", "Cut Line Left", "Cut Line Right", "Cut Selection or Paste"};
    const gchar *clipboard_ids[] = {"copy-line", "cut-line", "copy-line-right", "cut-line-left", "cut-line-right", "cut-selection-or-paste"};
    for (guint i = 0; i < G_N_ELEMENTS(clipboard_labels); i++)
    {
        GtkWidget *entry = gtk_menu_item_new_with_label(clipboard_labels[i]);
        gchar *name = g_strconcat("insight-", clipboard_ids[i], NULL);
        gtk_widget_set_name(entry, name);
        g_free(name);
        g_signal_connect(entry, "activate", G_CALLBACK(clipboard_command), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub), entry);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent);
    gtk_widget_show_all(parent);
}

