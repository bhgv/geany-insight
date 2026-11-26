/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Intelligent and clipboard insertion with asynchronous destination guards.
 * RU: Умная вставка и буфер обмена с проверкой отложенного назначения. */
/* EN: Convert text EOLs for sci only for native clipboard insertion when its paste-conversion option is active.
 * RU: Преобразует EOL текста text для sci только при обычной вставке буфера с включённой конвертацией. */
static gchar *paste_plain_body(ScintillaObject *sci, const gchar *text, gboolean clipboard)
{
    if (!clipboard || !SEND(sci, SCI_GETPASTECONVERTENDINGS, 0, 0))
        return g_strdup(text);
    const gchar *eol = sci_get_eol_mode(sci) == SC_EOL_CRLF ? "\r\n" :
        sci_get_eol_mode(sci) == SC_EOL_CR ? "\r" : "\n";
    GString *body = g_string_new(NULL);
    for (const gchar *at = text; *at; at++)
    {
        if (*at == '\r' || *at == '\n')
        {
            if (*at == '\r' && at[1] == '\n') at++;
            g_string_append(body, eol);
        }
        else
            g_string_append_c(body, *at);
    }
    return g_string_free(body, FALSE);
}

/* EN: Insert bounded valid text into sci in one Undo, preserving indentation and applying the current selection policy.
 * clipboard chooses native EOL conversion; direct Clips retain exact bytes. Return FALSE for unsupported input.
 * RU: Вставляет ограниченный корректный text в sci одним Undo, сохраняя отступы и применяя политику выделения.
 * clipboard включает штатную конвертацию EOL; прямые Clips сохраняют байты. FALSE для неподдерживаемого ввода. */
static gboolean paste_plain(ScintillaObject *sci, const gchar *text, gboolean clipboard)
{
    if (!plain_allowed(sci) || !text || !*text || strlen(text) > 1024 * 1024 || !g_utf8_validate(text, -1, NULL))
        return FALSE;
    gint start = sci_get_selection_start(sci);
    gchar *body = paste_plain_body(sci, text, clipboard);
    gint end = start + strlen(body);
    GeanyDocument *doc = document_for_sci(sci);
    insight_completion_cancel(doc->editor);
    SEND(sci, SCI_AUTOCCANCEL, 0, 0);
    sci_start_undo_action(sci);
    sci_replace_sel(sci, body);
    if (always_select || paste_position == PASTE_SELECT)
        SEND(sci, SCI_SETSEL, start, end);
    else if (paste_position == PASTE_START)
        SEND(sci, SCI_SETSEL, start, start);
    SEND(sci, SCI_SCROLLCARET, 0, 0);
    sci_end_undo_action(sci);
    g_free(body);
    return TRUE;
}

/* EN: Transform whole lines for sci when eligible and insert with the shared post-paste policy; return whether handled.
 * RU: Преобразует целые строки для sci при допустимом назначении и вставляет с общей политикой; возвращает признак обработки. */
static gboolean paste_whole_lines(ScintillaObject *sci, const gchar *text)
{
    if (!eligible(sci))
        return FALSE;
    gint start = sci_get_selection_start(sci), end = sci_get_selection_end(sci);
    gint line = sci_get_line_from_position(sci, start);
    gint line_start = sci_get_position_from_line(sci, line);
    gint line_end = SEND(sci, SCI_GETLINEENDPOSITION, line, 0);
    guint indent = SEND(sci, SCI_GETLINEINDENTATION, line, 0);
    guint tab_width = SEND(sci, SCI_GETTABWIDTH, 0, 0);
    const gchar *eol = sci_get_eol_mode(sci) == SC_EOL_CRLF ? "\r\n" :
                      sci_get_eol_mode(sci) == SC_EOL_CR ? "\r" : "\n";
    gchar *body = insight_paste_reindent(text, indent, tab_width,
                                       SEND(sci, SCI_GETUSETABS, 0, 0), eol);
    if (!body)
        return FALSE;
    gboolean final_line = FALSE;
    if (start == end)
    {
        if (start == line_end && line_end > line_start)
        {
            if (line + 1 < sci_get_line_count(sci))
                start = sci_get_position_from_line(sci, line + 1);
            else
            {
                start = line_end;
                final_line = TRUE;
            }
        }
        else
            start = line_start;
        end = start;
    }
    gchar *insert = final_line ? g_strconcat(eol, body, NULL) : g_strdup(body);
    GeanyDocument *doc = document_for_sci(sci);
    insight_completion_cancel(doc->editor);
    SEND(sci, SCI_AUTOCCANCEL, 0, 0);
    sci_start_undo_action(sci);
    SEND(sci, SCI_SETSEL, start, end);
    sci_replace_sel(sci, insert);
    /* The separator added at an unterminated EOF belongs to the old line,
     * not to the pasted block. Byte offsets retain UTF-8 and CRLF boundaries. */
    gint pasted_start = start + (final_line ? strlen(eol) : 0);
    gint pasted_end = start + strlen(insert);
    if (always_select || paste_position == PASTE_SELECT)
        SEND(sci, SCI_SETSEL, pasted_start, pasted_end);
    else if (paste_position == PASTE_START)
        SEND(sci, SCI_SETSEL, pasted_start, pasted_start);

    SEND(sci, SCI_SCROLLCARET, 0, 0);
    sci_end_undo_action(sci);
    g_free(body);
    g_free(insert);
    return TRUE;
}
/* EN: Insert explicit text into sci through intelligent or policy-controlled plain insertion, otherwise let the caller fall back.
 * RU: Вставляет явный text в sci умным либо управляемым обычным способом, иначе оставляет запасной путь вызывающему. */
gboolean insight_paste_text(ScintillaObject *sci, const gchar *text)
{
    if (paste_whole_lines(sci, text)) return TRUE;
    return position_policy() && paste_plain(sci, text, FALSE);
}

/* EN: Finish retained request r with text or native fallback only for the unchanged destination/preferences; release r.
 * RU: Завершает удерживаемый r текстом text либо штатной вставкой native только при неизменном назначении; освобождает r. */
static void finish_clipboard(Request *r, const gchar *text, gboolean native)
{
    GeanyDocument *doc = document_for_sci(r->sci);
    gboolean unchanged = doc && doc->is_valid && doc->id == r->id && document_get_current() == doc &&
        state(r->sci)->generation == r->generation &&
        SEND(r->sci, SCI_GETANCHOR, 0, 0) == r->anchor &&
        sci_get_current_position(r->sci) == r->caret && plain_allowed(r->sci) &&
        doc->file_type == r->filetype && preference_generation == r->preferences && allowed(r->sci) == r->smart;
    if (unchanged && (native || (text && *text)))
    {
        if (native || (!paste_whole_lines(r->sci, text) && !paste_plain(r->sci, text, TRUE)))
            SEND(r->sci, SCI_PASTE, 0, 0);
    }
    else if (!unchanged)
    {
        if (g_getenv("GEANY_PASTE_DEBUG"))
            g_printerr("paste cancelled: id=%u/%u generation=%"G_GUINT64_FORMAT"/%"G_GUINT64_FORMAT" anchor=%d/%ld caret=%d/%d eligible=%d\n",
                r->id, doc ? doc->id : 0, r->generation, state(r->sci)->generation,
                r->anchor, (long)SEND(r->sci, SCI_GETANCHOR, 0, 0), r->caret,
                sci_get_current_position(r->sci), eligible(r->sci));
        ui_set_statusbar(FALSE, "Paste cancelled: destination changed while reading the clipboard.");
    }
    g_object_unref(r->sci);
    g_free(r);
}
/* EN: Read raw UTF-8 selection bytes without GTK get_text EOL normalization; retry the MIME target when missing.
 * clipboard is the source, selection is borrowed, data owns Request until completion or retry; unsupported shapes use native Paste.
 * RU: Читает исходные байты UTF-8 без нормализации EOL get_text GTK; при отсутствии пробует MIME.
 * clipboard — источник, selection заимствован, data владеет Request до завершения/повтора; особые формы вставляются штатно. */
static void clipboard_contents_ready(GtkClipboard *clipboard, GtkSelectionData *selection, gpointer data)
{
    gint length = gtk_selection_data_get_length(selection);
    if (length < 0)
    {
        GdkAtom target = gtk_selection_data_get_target(selection);
        if (target == gdk_atom_intern_static_string("UTF8_STRING"))
            gtk_clipboard_request_contents(clipboard, gdk_atom_intern_static_string("text/plain;charset=utf-8"),
                clipboard_contents_ready, data);
        else
            finish_clipboard(data, NULL, TRUE);
        return;
    }
    const guchar *bytes = gtk_selection_data_get_data(selection);
#ifdef G_OS_WIN32
    /* Native Windows clipboard text may include one terminating NUL. */
    if (length > 0 && bytes[length - 1] == 0)
        length--;
#endif
    gboolean native = length > 1024 * 1024 ||
        (length > 0 && (!bytes || memchr(bytes, 0, length) ||
            !g_utf8_validate((const gchar *)bytes, length, NULL)));
    gchar *text = !native && length > 0 ? g_strndup((const gchar *)bytes, length) : NULL;
    finish_clipboard(data, text, native);
    g_free(text);
}

/* EN: Read raw UTF-8 GTK clipboard contents for retained Request data on the main loop.
 * RU: Читает исходное содержимое UTF-8 буфера GTK для удерживаемого Request data в основном цикле. */
static gboolean request_clipboard(gpointer data)
{
    gtk_clipboard_request_contents(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
        gdk_atom_intern_static_string("UTF8_STRING"), clipboard_contents_ready, data);
    return G_SOURCE_REMOVE;
}
/* EN: Capture sci for asynchronous guarded Paste when intelligent handling or a post-paste policy is active.
 * RU: Фиксирует sci для отложенной защищённой вставки при умном режиме либо активной политике после вставки. */
gboolean insight_paste_clipboard(ScintillaObject *sci)
{
    if (!plain_allowed(sci) || (!eligible(sci) && !position_policy()))
        return FALSE;
    GeanyDocument *doc = document_for_sci(sci);
    Request *r = g_new0(Request, 1);
    r->sci = g_object_ref(sci);
    r->id = doc->id;
    r->generation = state(sci)->generation;
    r->anchor = SEND(sci, SCI_GETANCHOR, 0, 0);
    r->caret = sci_get_current_position(sci);
    r->preferences = preference_generation;
    r->filetype = doc->file_type;
    r->smart = allowed(sci);
    g_idle_add(request_clipboard, r);
    return TRUE;
}
