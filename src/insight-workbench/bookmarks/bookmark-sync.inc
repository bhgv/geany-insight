/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Synchronize bookmark records with Scintilla markers and symbols.
 * RU: Синхронизация закладок с маркерами и символами Scintilla. */


/* EN: Return trimmed text from line in sci.
 * RU: Возвращает обрезанный текст строки line в sci. */
static gchar *bookmark_line_text(ScintillaObject *sci, gint line)
{
    gchar *text = sci_get_line(sci, line);
    gsize n = strlen(text);
    while (n && (text[n-1] == '\r' || text[n-1] == '\n')) text[--n] = 0;
    return text;
}

/* EN: Check whether doc has usable native tags.
 * RU: Проверяет наличие пригодных тегов в doc. */
static gboolean bookmark_tags_ready(GeanyDocument *doc)
{
    return doc->priv && (!doc->file_type || doc->file_type->lang == TM_PARSER_NONE || doc->priv->tag_snapshot_generation == doc->priv->tag_parse_generation);
}

/* EN: Prepare source tags for doc before restoring bookmarks.
 * RU: Подготавливает теги doc перед восстановлением закладок. */
static void bookmark_prepare_tags(GeanyDocument *doc)
{
    if (!doc || !doc->is_valid || sci_get_length(doc->editor->sci)>2*1024*1024 || bookmark_tags_ready(doc)) return;
    if (doc->priv->tag_list_update_source)
    {
        g_source_remove(doc->priv->tag_list_update_source);
        doc->priv->tag_list_update_source = 0;
    }
    document_update_tags(doc);
}

/* EN: Collect source symbols from doc for bookmark matching.
 * RU: Собирает символы doc для сопоставления закладок. */
static GArray *bookmark_symbols(GeanyDocument *doc)
{
    if (!bookmark_tags_ready(doc))
        return NULL;
    if (doc->tm_file && doc->tm_file->tags_array && doc->tm_file->tags_array->len>250000)
        return NULL;
    GArray *symbols = g_array_new(FALSE, FALSE, sizeof(InsightBookmarkSymbol));
    if (doc->tm_file && doc->tm_file->tags_array && doc->tm_file->tags_array->len <= 250000) for (guint i = 0;i<doc->tm_file->tags_array->len;i++)
    {
        TMTag *tag = g_ptr_array_index(doc->tm_file->tags_array, i);
        guint kind = tag->type&(tm_tag_function_t|tm_tag_method_t) ? INSIGHT_BOOKMARK_FUNCTION : tag->type&(tm_tag_class_t|tm_tag_struct_t|tm_tag_interface_t) ? INSIGHT_BOOKMARK_CLASS : 0;
        if (kind && tag->name && tag->line)
        {
            InsightBookmarkSymbol symbol =
            {
                tag->name, tag->scope ? tag->scope : "", kind, tag->line
            };
            g_array_append_val(symbols, symbol);
        }
    }
    return symbols;
}

/* EN: Check whether name is already present in symbols.
 * RU: Проверяет наличие name в symbols. */
static gboolean bookmark_capture_symbol(const gchar *name, GArray *symbols)
{
    if (!symbols)
        return FALSE;
    const InsightNote *note = insight_notes_get(notes_for(INSIGHT_BOOKMARK), INSIGHT_BOOKMARK, name);
    if (!note)
        return FALSE;
    InsightBookmarkAnchor previous =
    {
        note->anchor ? g_strdup(note->anchor->name) : NULL, note->anchor ? g_strdup(note->anchor->scope) : NULL, note->anchor ? note->anchor->kind : 0, note->anchor ? note->anchor->offset : 0
    };
    insight_notes_capture_anchor(notes_for(INSIGHT_BOOKMARK), name, (const InsightBookmarkSymbol *)symbols->data, symbols->len, NULL);
    note = insight_notes_get(notes_for(INSIGHT_BOOKMARK), INSIGHT_BOOKMARK, name);
    gboolean changed = g_strcmp0(previous.name, note->anchor ? note->anchor->name : NULL) || g_strcmp0(previous.scope, note->anchor ? note->anchor->scope : NULL) || previous.kind != (note->anchor ? note->anchor->kind : 0) || previous.offset != (note->anchor ? note->anchor->offset : 0);
    g_free(previous.name);
    g_free(previous.scope);
    return changed;
}

/* EN: Restore stored bookmarks in the editor for doc.
 * RU: Восстанавливает сохранённые закладки в редакторе doc. */
static void bookmark_restore_document(GeanyDocument *doc)
{
    if (wb.marks_stale || (wb.marks_project && !wb.marks_valid)) return;
    if (!doc || !doc->is_valid || !doc->file_name || !wb.notes || !wb.bookmark_handles || sci_get_length(doc->editor->sci)>2*1024*1024) return;
    gchar *text = NULL;
    GArray *symbols = bookmark_symbols(doc);
    for (guint i = 0;i<notes_for(INSIGHT_BOOKMARK)->items->len;i++)
    {
        InsightNote *note = g_ptr_array_index(notes_for(INSIGHT_BOOKMARK)->items, i);
        if (note->kind != INSIGHT_BOOKMARK || g_strcmp0(note->path, doc->file_name)) continue;
        BookmarkHandle *handle = g_hash_table_lookup(wb.bookmark_handles, note->name);
        if (handle && handle->doc == doc->id) continue;
        if (!text) text = sci_get_contents(doc->editor->sci, sci_get_length(doc->editor->sci)+1);
        guint64 line = 0;
        gboolean ambiguous = FALSE;
        if ((note->anchor && !symbols) || !insight_bookmark_resolve_symbols(note, text, strlen(text), symbols ? (const InsightBookmarkSymbol *)symbols->data : NULL, symbols ? symbols->len : 0, &line, &ambiguous) || !line || line>(guint64)sci_get_line_count(doc->editor->sci)) continue;
        handle = g_new0(BookmarkHandle, 1);
        handle->doc = doc->id;
        handle->handle = (gint)SSM(doc->editor->sci, SCI_MARKERADD, (uptr_t)(line-1), 1);
        g_hash_table_replace(wb.bookmark_handles, g_strdup(note->name), handle);
    }
    g_free(text);
    if (symbols)
        g_array_unref(symbols);
}

/* EN: Clear the tracked Scintilla marker handles.
 * RU: Очищает сохранённые идентификаторы маркеров Scintilla. */
static void bookmark_clear_handles(void)
{
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, wb.bookmark_handles);
    while (g_hash_table_iter_next(&it, NULL, &value))
    {
        BookmarkHandle *handle = value;
        GeanyDocument *doc = document_find_by_id(handle->doc);
        if (doc && doc->is_valid) SSM(doc->editor->sci, SCI_MARKERDELETEHANDLE, handle->handle, 0);
    }
    g_hash_table_remove_all(wb.bookmark_handles);
}

/* EN: Capture current bookmark markers in doc.
 * RU: Сохраняет текущие маркеры закладок в doc. */
static gboolean bookmark_capture_document(GeanyDocument *doc)
{
    if (!doc || !doc->is_valid || !doc->file_name)
        return FALSE;
    GArray *symbols = bookmark_symbols(doc);
    gboolean changed = FALSE;
    GHashTable *covered = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (gint i = (gint)notes_for(INSIGHT_BOOKMARK)->items->len-1;i >= 0;i--)
    {
        InsightNote *note = g_ptr_array_index(notes_for(INSIGHT_BOOKMARK)->items, (guint)i);
        if (note->kind != INSIGHT_BOOKMARK || g_strcmp0(note->path, doc->file_name)) continue;
        BookmarkHandle *handle = g_hash_table_lookup(wb.bookmark_handles, note->name);
        if (!handle || handle->doc != doc->id) continue;
        gint line = (gint)SSM(doc->editor->sci, SCI_MARKERLINEFROMHANDLE, handle->handle, 0);
        if (line<0)
        {
            gchar *name = g_strdup(note->name);
            insight_notes_remove(notes_for(INSIGHT_BOOKMARK), INSIGHT_BOOKMARK, name);
            g_hash_table_remove(wb.bookmark_handles, name);
            g_free(name);
            changed = TRUE;
            continue;
        }
        g_hash_table_add(covered, GINT_TO_POINTER(line+1));
        gchar *text = bookmark_line_text(doc->editor->sci, line);
        if (strlen(text) <= 65536 && (note->line != (guint64)line+1 || g_strcmp0(note->text, text)))
        {
            note->line = (guint64)line+1;
            g_free(note->text);
            note->text = g_strdup(text);
            changed = TRUE;
        }
        g_free(text);
        changed = bookmark_capture_symbol(note->name, symbols) || changed;
    }
    for (gint line = (gint)SSM(doc->editor->sci, SCI_MARKERNEXT, 0, 2);line >= 0;line = (gint)SSM(doc->editor->sci, SCI_MARKERNEXT, line+1, 2))
    {
        if (g_hash_table_contains(covered, GINT_TO_POINTER(line+1))) continue;
        gchar *text = bookmark_line_text(doc->editor->sci, line), *name = g_strdup_printf("LineBookmark-%u-%d-%"G_GINT64_FORMAT, doc->id, line, g_get_real_time());
        GError *error = NULL;
        if (insight_notes_set(notes_for(INSIGHT_BOOKMARK), INSIGHT_BOOKMARK, name, doc->file_name, (guint64)line+1, text, &error))
        {
            BookmarkHandle *handle = g_new0(BookmarkHandle, 1);
            handle->doc = doc->id;
            handle->handle = (gint)SSM(doc->editor->sci, SCI_MARKERADD, line, 1);
            g_hash_table_replace(wb.bookmark_handles, g_strdup(name), handle);
            bookmark_capture_symbol(name, symbols);
            changed = TRUE;
        }
        else ui_set_statusbar(TRUE, "Bookmark not persisted: %s", error ? error->message : "invalid line");
        g_clear_error(&error);
        g_free(name);
        g_free(text);
    }
    g_hash_table_destroy(covered);
    if (symbols)
        g_array_unref(symbols);
    return changed;
}

/* EN: Synchronize open editors and refresh bookmark views.
 * RU: Синхронизирует открытые редакторы и обновляет списки закладок. */
void insight_workbench_bookmarks_changed(void)
{
    if (!wb.alive || !wb.notes || !wb.bookmark_handles || wb.bookmark_syncing || wb.marks_stale || (wb.marks_project && !wb.marks_valid)) return;
    wb.bookmark_syncing = TRUE;
    gboolean changed = FALSE;
    for (guint i = 0;i<documents_array->len;i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        bookmark_restore_document(doc);
        changed = bookmark_capture_document(doc) || changed;
    }
    if (changed)
    {
        notes_save();
        notes_filter(NULL, GINT_TO_POINTER(INSIGHT_BOOKMARK));
    }
    bookmark_list_reload(NULL, NULL);
    wb.bookmark_syncing = FALSE;
}

/* EN: Run deferred bookmark synchronization. data is unused.
 * RU: Выполняет отложенную синхронизацию. data не используется. */
static gboolean bookmark_sync_idle(gpointer data)
{
    (void)data;
    wb.bookmark_sync_timer = 0;
    insight_workbench_bookmarks_changed();
    return G_SOURCE_REMOVE;
}
