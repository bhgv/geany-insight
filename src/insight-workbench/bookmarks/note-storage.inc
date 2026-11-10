/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Project and user note storage and bookmark session state.
 * RU: Хранилища заметок проекта и пользователя, состояние сеанса закладок. */


/* EN: Return the active store for note kind.
 * RU: Возвращает активное хранилище для kind. */
static InsightNotes *notes_for(InsightNoteKind kind)
{
    if (kind == INSIGHT_BOOKMARK && wb.marks_project)
        return wb.project_marks;
    return kind == INSIGHT_CLIP && wb.clips_project ? wb.project_notes : wb.notes;
}

/* EN: Return the current generation for note kind.
 * RU: Возвращает текущую версию хранилища kind. */
static guint64 notes_generation(InsightNoteKind kind)
{
    return kind == INSIGHT_BOOKMARK ? wb.marks_generation : wb.clips_generation;
}

/* EN: Return whether the current bookmark store can be written.
 * RU: Проверяет возможность записи в текущее хранилище закладок. */
static gboolean bookmarks_writable(void)
{
    if (!wb.marks_stale && (!wb.marks_project || wb.marks_valid))
        return TRUE;
    ui_set_statusbar(TRUE, "Project bookmarks unavailable; repair or reload the store");
    return FALSE;
}

/* EN: Check whether generation still matches the active store for kind.
 * RU: Проверяет, соответствует ли generation активному хранилищу kind. */
static gboolean notes_scope_current(InsightNoteKind kind, guint64 generation)
{
    if (kind == INSIGHT_BOOKMARK)
    {
        if (generation == wb.marks_generation)
            return TRUE;
        ui_set_statusbar(TRUE, "Bookmark store changed; try the operation again");
        return FALSE;
    }
    if (kind != INSIGHT_CLIP || (generation == wb.clips_generation && (!wb.clips_project || (wb.project_notes_valid && wb.project_notes_path))))
        return TRUE;
    ui_set_statusbar(TRUE, "Template store changed; try the operation again");
    return FALSE;
}
typedef struct
{
    gchar *root, *etag;
} BookmarkSessionStore;

/* EN: Release the bookmark session store in data.
 * RU: Освобождает хранилище сеанса закладок из data. */
static void bookmark_session_store_free(gpointer data)
{
    BookmarkSessionStore *store = data;
    g_free(store->root);
    g_free(store->etag);
    g_free(store);
}
/* Capture before loading, or use the digest/etag token from our atomic write. */

/* EN: Record the bookmark store path, root and etag for the session.
 * RU: Запоминает path, root и etag хранилища закладок для сеанса. */
static void bookmark_session_remember(const gchar *path, const gchar *root, const gchar *etag)
{
    if (!wb.marks_session_stores || !path) return;
    gchar *captured = etag ? NULL : insight_notes_file_version(path, NULL);
    if (!etag) etag = captured;
    if (etag)
    {
        BookmarkSessionStore *store = g_new0(BookmarkSessionStore, 1);
        store->root = g_strdup(root);
        store->etag = g_strdup(etag);
        g_hash_table_replace(wb.marks_session_stores, g_strdup(path), store);
    }
    g_free(captured);
}

/* EN: Finalize bookmark stores tracked in the current session.
 * RU: Завершает работу с хранилищами закладок текущего сеанса. */
static void bookmark_session_finish(void)
{
    if (wb.marks_preserve || !wb.marks_session_stores) return;
    GHashTableIter iter;
    gpointer path, value;
    g_hash_table_iter_init(&iter, wb.marks_session_stores);
    while (g_hash_table_iter_next(&iter, &path, &value))
    {
        BookmarkSessionStore *store = value;
        GError *error = NULL;
        InsightNotes *notes = insight_notes_load_portable(path, store->root, &error), *clips = notes ? insight_notes_select(notes, INSIGHT_CLIP, &error) : NULL;
        if (clips && clips->items->len != notes->items->len)
            insight_notes_save_portable_checked(clips, path, store->root, store->etag, NULL, &error);
        if (error)
        {
            g_warning("Bookmarks retained in %s at exit: %s", (gchar *)path, error->message);
            g_clear_error(&error);
        }
        insight_notes_free(clips);
        insight_notes_free(notes);
    }
}

/* EN: Save the active note store for kind and return success.
 * RU: Сохраняет хранилище kind и возвращает признак успеха. */
static gboolean notes_save_kind(InsightNoteKind kind)
{
    gboolean marks = kind == INSIGHT_BOOKMARK && wb.marks_project;
    gboolean project = kind == INSIGHT_CLIP && wb.clips_project;
    const gchar *path = marks ? wb.project_marks_path : project ? wb.project_notes_path : wb.notes_path;
    if (kind == INSIGHT_BOOKMARK && !bookmarks_writable())
        return FALSE;
    if (!path || (project && !wb.project_notes_valid))
    {
        ui_set_statusbar(TRUE, "Open a project with a valid template store first");
        return FALSE;
    }
    gchar *directory = g_path_get_dirname(path);
    GError *error = NULL;
    gboolean saved = FALSE;
    gchar *etag = NULL;
    if (g_mkdir_with_parents(directory, 0700)) ui_set_statusbar(TRUE, "Notes directory could not be created");
    else
    {
        saved = insight_notes_save_portable_checked(notes_for(kind), path, marks ? wb.project_marks_root : NULL, NULL, &etag, &error);
        if (saved && !project) bookmark_session_remember(path, marks ? wb.project_marks_root : NULL, etag);
        if (!saved) ui_set_statusbar(TRUE, "Notes not saved: %s", error ? error->message : "write failed");
    }
    g_clear_error(&error);
    g_free(etag);
    g_free(directory);
    if (kind == INSIGHT_BOOKMARK)
        wb.marks_save_failed = !saved;
    return saved;
}

/* EN: Save the active bookmark store.
 * RU: Сохраняет активное хранилище закладок. */
static gboolean notes_save(void)
{
    return notes_save_kind(INSIGHT_BOOKMARK);
}

/* EN: Copy the active store for kind and report a failure via error.
 * RU: Копирует хранилище kind и сообщает об ошибке через error. */
static InsightNotes *notes_clone(InsightNoteKind kind, GError **error)
{
    InsightNotes *source = notes_for(kind), *out = insight_notes_new();
    for (guint i = 0;i<source->items->len;i++)
    {
        InsightNote *note = g_ptr_array_index(source->items, i);
        if (!insight_notes_set(out, note->kind, note->name, note->path, note->line, note->text, error) || (note->kind == INSIGHT_CLIP && !insight_notes_set_languages(out, note->name, (const gchar *const *)note->languages, error)))
        {
            insight_notes_free(out);
            return NULL;
        }
        if (note->anchor && !insight_notes_set_anchor(out, note->name, note->anchor, error))
        {
            insight_notes_free(out);
            return NULL;
        }
    }
    return out;
}
static gboolean notes_commit(InsightNotes *, InsightNoteKind);
typedef struct
{
    guint doc;
    gint handle;
}
BookmarkHandle;
static gchar *selected_note(InsightNoteKind);
static GPtrArray *selected_notes(InsightNoteKind);
static void note_selected(GtkTreeSelection *, gpointer);

/* EN: Return the active document language profile for clips.
 * RU: Возвращает языковой профиль активного документа для фрагментов. */
static gchar *clip_current_language(void)
{
    GeanyDocument *doc = document_get_current();
    return doc ? insight_languages_profile_name(doc->editor->sci, doc->file_name, doc->file_type) : g_strdup("");
}
static void notes_filter(GtkEditable *, gpointer);
static void bookmark_list_reload(GtkWidget *, gpointer);
