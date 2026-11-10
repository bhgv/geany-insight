/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Bookmark list rows, source opening and selection.
 * RU: Строки списка закладок, открытие исходника и выбор. */


/* EN: Open note at its stored source location and report success.
 * RU: Открывает note в сохранённой позиции и сообщает об успехе. */
static gboolean bookmark_open_note(const InsightNote *note)
{
    if (!note)
        return FALSE;
    gchar *path = g_strdup(note->path), *name = g_strdup(note->name);
    GeanyDocument *old = document_get_current(), *doc = document_find_by_filename(path);
    if (!doc)
    {
        gchar *native = utils_get_locale_from_utf8(path);
        doc = document_open_file(native, FALSE, NULL, NULL);
        g_free(native);
    }
    g_free(path);
    if (!doc)
    {
        g_free(name);
        return FALSE;
    }
    /* Opening can emit document events; look up the note again afterwards. */
    note = insight_notes_get(notes_for(INSIGHT_BOOKMARK), INSIGHT_BOOKMARK, name);
    BookmarkHandle *handle = g_hash_table_lookup(wb.bookmark_handles, name);
    g_free(name);
    gint line = handle && handle->doc == doc->id ? (gint)SSM(doc->editor->sci, SCI_MARKERLINEFROMHANDLE, handle->handle, 0) : -1;
    if (line<0 && note && sci_get_length(doc->editor->sci) <= 2*1024*1024)
    {
        bookmark_prepare_tags(doc);
        GArray *symbols = bookmark_symbols(doc);
        gchar *text = sci_get_contents(doc->editor->sci, sci_get_length(doc->editor->sci)+1);
        guint64 resolved = 0;
        gboolean ambiguous = FALSE;
        if (insight_bookmark_resolve_symbols(note, text, strlen(text), symbols ? (const InsightBookmarkSymbol *)symbols->data : NULL, symbols ? symbols->len : 0, &resolved, &ambiguous)) line = (gint)resolved-1;
        g_free(text);
        if (symbols)
            g_array_unref(symbols);
    }
    if (line<0)
    {
        ui_set_statusbar(TRUE, "Bookmark source is unavailable or its line is ambiguous");
        return FALSE;
    }
    navqueue_goto_line(old, doc, line+1);
    SSM(doc->editor->sci, SCI_VERTICALCENTRECARET, 0, 0);
    document_grab_focus(doc);
    return TRUE;
}

/* EN: Add the bookmark for path and line with text, doc, handle and name. Use seen to suppress duplicates.
 * RU: Добавляет закладку path и line с text, doc, handle и name. seen исключает дубли. */
static void bookmark_list_add(const gchar *path, guint64 line, const gchar *text, guint doc, gint handle, const gchar *name, GHashTable *seen)
{
    gchar *key = g_strdup_printf("%s\001%"G_GUINT64_FORMAT, path, line);
    if (g_hash_table_contains(seen, key))
    {
        g_free(key);
        return;
    }
    g_hash_table_add(seen, key);
    gchar *base = g_path_get_basename(path), *label = g_strdup_printf("%s:%"G_GUINT64_FORMAT"> %s", base, line, text);
    g_free(base);
    if (insight_lookup_text_match(label, gtk_entry_get_text(GTK_ENTRY(wb.bookmark_search))))
    {
        GtkTreeIter it;
        gtk_list_store_append(wb.bookmark_list, &it);
        gtk_list_store_set(wb.bookmark_list, &it, 0, label, 1, path, 2, line, 3, doc, 4, handle, 5, name, -1);
    }
    g_free(label);
}

/* EN: Rebuild the visible bookmark list. widget and data are unused.
 * RU: Перестраивает видимый список закладок. widget и data не используются. */
static void bookmark_list_reload(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    if (!wb.bookmark_list) return;
    gtk_list_store_clear(wb.bookmark_list);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0;i<documents_array->len;i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        if (!doc->is_valid) continue;
        for (gint line = (gint)SSM(doc->editor->sci, SCI_MARKERNEXT, 0, 2);line >= 0;line = (gint)SSM(doc->editor->sci, SCI_MARKERNEXT, line+1, 2))
        {
            gchar *text = bookmark_line_text(doc->editor->sci, line), *path = doc->file_name ? g_strdup(doc->file_name) : g_strdup_printf("Untitled-%u", doc->id);
            gint marker = -1;
            for (gint index = 0;;index++)
            {
                gint number = (gint)SSM(doc->editor->sci, SCI_MARKERNUMBERFROMLINE, line, index);
                if (number<0) break;
                if (number == 1)
                {
                    marker = (gint)SSM(doc->editor->sci, SCI_MARKERHANDLEFROMLINE, line, index);
                    break;
                }
            }
            bookmark_list_add(path, (guint64)line+1, text, doc->id, marker, NULL, seen);
            g_free(text);
            g_free(path);
        }
    }
    for (guint i = 0;i<notes_for(INSIGHT_BOOKMARK)->items->len;i++)
    {
        InsightNote *note = g_ptr_array_index(notes_for(INSIGHT_BOOKMARK)->items, i);
        if (note->kind == INSIGHT_BOOKMARK) bookmark_list_add(note->path, note->line, note->text, 0, -1, note->name, seen);
    }
    g_hash_table_destroy(seen);
}

/* EN: Update the bookmark view from selection. data is unused.
 * RU: Обновляет представление закладок по selection. data не используется. */
static void bookmark_list_selected(GtkTreeSelection *selection, gpointer data)
{
    (void)data;
    GtkTreeModel *model;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(selection, &model, &it)) return;
    gchar *path = NULL, *name = NULL;
    guint id = 0;
    guint64 line = 0;
    gint marker = -1;
    gtk_tree_model_get(model, &it, 1, &path, 2, &line, 3, &id, 4, &marker, 5, &name, -1);
    if (name) bookmark_open_note(insight_notes_get(notes_for(INSIGHT_BOOKMARK), INSIGHT_BOOKMARK, name));
    else
    {
        GeanyDocument *old = document_get_current(), *doc = id ? document_find_by_id(id) : document_find_by_filename(path);
        if (!doc && !id)
        {
            gchar *native = utils_get_locale_from_utf8(path);
            doc = document_open_file(native, FALSE, NULL, NULL);
            g_free(native);
        }
        if (doc && marker >= 0)
        {
            gint moved = (gint)SSM(doc->editor->sci, SCI_MARKERLINEFROMHANDLE, marker, 0);
            line = moved >= 0 ? (guint64)moved+1 : 0;
        }
        if (doc && line && line <= (guint64)sci_get_line_count(doc->editor->sci))
        {
            navqueue_goto_line(old, doc, (gint)line);
            SSM(doc->editor->sci, SCI_VERTICALCENTRECARET, 0, 0);
            document_grab_focus(doc);
        }
    }
    g_free(path);
    g_free(name);
}
