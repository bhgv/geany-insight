/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Disk source loading, preview jobs and navigation history.
 * RU: Чтение исходников, предпросмотр и история переходов. */


/* EN: Read utf8, optionally decode using preferred, and honor cancel.
 * RU: Читает utf8 с учётом decode, preferred и отмены cancel. */
static char *read_disk_source_full(const char *utf8, gboolean decode, gint preferred, GCancellable *cancel)
{
    gchar *native = utils_get_locale_from_utf8(utf8);
    GFile *file = g_file_new_for_path(native);
    g_free(native);
    GFileInputStream *stream = g_file_read(file, cancel, NULL);
    g_object_unref(file);
    if (!stream)
        return NULL;
    GString *text = g_string_sized_new(16384);
    char chunk[16384];
    gboolean ok = TRUE;
    for (;;)
    {
        gssize got = g_input_stream_read(G_INPUT_STREAM(stream), chunk, sizeof chunk, cancel, NULL);
        if (got<0 || (got>0 && text->len+(gsize)got>2u*1024u*1024u))
        {
            ok = FALSE;
            break;
        }
        if (!got) break;
        g_string_append_len(text, chunk, got);
    }
    g_object_unref(stream);
    if (!ok)
    {
        g_string_free(text, TRUE);
        return NULL;
    }
    gsize length = text->len;
    gchar *buffer = g_string_free(text, FALSE);
    if (decode)
    {
        gboolean nul = FALSE;
        if (!(preferred == G_MININT ? encodings_convert_to_utf8_auto(&buffer, &length, NULL, NULL, NULL, &nul, NULL) : encodings_convert_to_utf8_worker(&buffer, &length, preferred, &nul)) || nul)
        {
            g_free(buffer);
            return NULL;
        }
    }
    if (length>2u*1024u*1024u || !g_utf8_validate(buffer, (gssize)length, NULL) || memchr(buffer, 0, length))
    {
        g_free(buffer);
        return NULL;
    }
    return buffer;
}

/* EN: Read utf8 using the default encoding preference when decode is true.
 * RU: Читает utf8 с кодировкой по умолчанию, если задан decode. */
static char *read_disk_source(const char *utf8, gboolean decode)
{
    return read_disk_source_full(utf8, decode, G_MININT, NULL);
}

/* EN: Store path, document id and line in navigation history.
 * RU: Запоминает path, id документа и line в истории переходов. */
static void remember_location(const char *path, guint id, gulong line)
{
    if (!wb.history || !line) return;
    if (wb.history_position >= 0 && wb.history_position<(gint)wb.history->len)
    {
        Location *old = g_ptr_array_index(wb.history, (guint)wb.history_position);
        if (old->id == id && old->line == line && !g_strcmp0(old->path, path)) return;
    }
    while (wb.history->len && (gint)wb.history->len>wb.history_position+1)
        g_ptr_array_remove_index(wb.history, wb.history->len-1);
    Location *next = g_new0(Location, 1);
    next->path = g_strdup(path);
    next->id = id;
    next->line = line;
    g_ptr_array_add(wb.history, next);
    if (wb.history->len>64)
        g_ptr_array_remove_index(wb.history, 0);
    wb.history_position = (gint)wb.history->len-1;
    navigation_state();
}

/* EN: Show the source context panel.
 * RU: Показывает панель контекста исходника. */
static void reveal_context(void)
{
    if (!wb.box || !wb.context_visible) return;
    if (gtk_widget_get_parent(wb.box) == main_widgets.message_window_notebook)
    {
        GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(main_widgets.window));
        if (focus)
            g_object_ref(focus);
        if (!gtk_widget_get_visible(main_widgets.message_window_notebook)) msgwin_show_hide(TRUE);
        gint page = gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.message_window_notebook), wb.box);
        if (gtk_notebook_get_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook)) != page) gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook), page);
        if (focus)
        {
            if (gtk_window_get_focus(GTK_WINDOW(main_widgets.window)) != focus && gtk_widget_get_mapped(focus) && gtk_widget_get_sensitive(focus)) gtk_widget_grab_focus(focus);
            g_object_unref(focus);
        }
    }
}
typedef struct
{
    gchar *path, *text, *digest;
    guint id;
    gint encoding, length;
    guint64 epoch, generation, text_version;
}
FilePreviewJob;
static void request_file_preview(const char *);

/* EN: Cancel outstanding preview work and invalidate its result.
 * RU: Отменяет задачи предпросмотра и помечает результат устаревшим. */
static void stop_file_preview(void)
{
    wb.preview_generation++;
    if (wb.preview_worker)
    {
        g_cancellable_cancel(wb.preview_worker);
        g_clear_object(&wb.preview_worker);
    }
}

/* EN: Release the preview job in data.
 * RU: Освобождает задачу предпросмотра из data. */
static void file_preview_free(gpointer data)
{
    FilePreviewJob *j = data;
    g_free(j->path);
    g_free(j->text);
    g_free(j->digest);
    g_free(j);
}

/* EN: Load the preview in data for task. cancel stops reading. source is unused.
 * RU: Читает предпросмотр из data для task. cancel отменяет чтение. source не используется. */
static void file_preview_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    FilePreviewJob *j = data;
    if (!j->id && !g_cancellable_is_cancelled(cancel)) j->text = read_disk_source_full(j->path, TRUE, j->encoding, cancel);
    if (j->text && !g_cancellable_is_cancelled(cancel) && g_utf8_validate(j->text, -1, NULL) && (!j->id || !memchr(j->text, 0, (gsize)j->length))) j->digest = source_digest(j->text);
    g_task_return_boolean(task, TRUE);
}

/* EN: Publish result if the preview is current. source and data are unused.
 * RU: Публикует result, если предпросмотр актуален. source и data не используются. */
static void file_preview_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    FilePreviewJob *j = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    if (!wb.alive || j->epoch != workbench_epoch || j->generation != wb.preview_generation) return;
    g_clear_object(&wb.preview_worker);
    if (!ok) return;
    GeanyDocument *doc = document_find_by_filename(j->path);
    if ((doc && (doc->id != j->id || selection_state(doc->editor->sci)->text_version != j->text_version)) || (!doc && j->id))
    {
        request_file_preview(j->path);
        return;
    }
    if (!j->digest)
    {
        render_source("", NULL, 1, NULL);
        gtk_label_set_text(GTK_LABEL(wb.title), "Context unavailable: requires a decodable text source up to 2 MiB");
        g_clear_pointer(&wb.source_path, g_free);
        wb.source_id = 0;
        wb.source_line = 0;
        preview_links();
        navigation_state();
        return;
    }
    if (wb.source_line) remember_location(wb.source_path, wb.source_id, (gulong)sci_get_current_line(wb.preview)+1);
    render_source(j->text, doc ? doc->file_type : preview_filetype(j->path), 1, j->path);
    g_free(wb.source_digest);
    wb.source_digest = g_strdup(j->digest);
    g_free(wb.source_path);
    wb.source_path = g_strdup(j->path);
    wb.source_id = doc ? doc->id : 0;
    wb.source_line = 1;
    preview_links();
    gchar *label = g_strdup_printf("Context — %s:1", j->path);
    gtk_label_set_text(GTK_LABEL(wb.title), label);
    g_free(label);
    remember_location(wb.source_path, wb.source_id, 1);
    navigation_state();
}

/* EN: Schedule a preview of path.
 * RU: Планирует предпросмотр path. */
static void request_file_preview(const char *path)
{
    stop_file_preview();
    FilePreviewJob *j = g_new0(FilePreviewJob, 1);
    j->path = g_strdup(path);
    j->encoding = file_prefs.default_open_encoding;
    j->epoch = workbench_epoch;
    j->generation = wb.preview_generation;
    GeanyDocument *doc = document_find_by_filename(path);
    if (doc)
    {
        j->id = doc->id;
        j->text_version = selection_state(doc->editor->sci)->text_version;
        j->length = sci_get_length(doc->editor->sci);
        if (j->length >= 0 && j->length <= 2*1024*1024) j->text = sci_get_contents(doc->editor->sci, j->length+1);
    }
    wb.preview_worker = g_cancellable_new();
    GTask *task = g_task_new(NULL, wb.preview_worker, file_preview_done, NULL);
    g_task_set_task_data(task, j, file_preview_free);
    g_task_run_in_thread(task, file_preview_run);
    g_object_unref(task);
}

/* EN: Show path and line for document id. remember adds the location to history.
 * RU: Показывает path и line для документа id. remember добавляет переход в историю. */
static gboolean show_location(const char *path, guint id, gulong line, gboolean remember)
{
    stop_file_preview();
    if (wb.context_toggle && !wb.context_visible) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(wb.context_toggle), TRUE);
    GeanyDocument *doc = id ? document_find_by_id(id) : (path ? document_find_by_filename(path) : NULL);
    char *text = NULL;
    GeanyFiletype *ft = NULL;
    if (doc)
    {
        gint length = sci_get_length(doc->editor->sci);
        if (length >= 0 && length <= 2*1024*1024) text = sci_get_contents(doc->editor->sci, length+1);
        if (text && (!g_utf8_validate(text, length, NULL) || memchr(text, 0, (gsize)length)))
            g_clear_pointer(&text, g_free);
        ft = doc->file_type;
    }
    else if (path)
    {
        text = read_disk_source(path, TRUE);
        ft = preview_filetype(path);
    }
    if (!text)
    {
        render_source("", NULL, 1, NULL);
        gtk_label_set_text(GTK_LABEL(wb.title), "Context unavailable: requires a decodable text source up to 2 MiB");
        g_clear_pointer(&wb.source_path, g_free);
        wb.source_id = 0;
        wb.source_line = 0;
        preview_links();
        navigation_state();
        return FALSE;
    }
    gchar *owned_path = g_strdup(path ? path : (doc ? doc->file_name : NULL));
    size_t begin, end, target_begin, target_end;
    unsigned long first;
    gboolean valid = insight_context_range(text, strlen(text), line, 0, &begin, &end, &first, &target_begin, &target_end);
    if (remember && valid && wb.source_line) remember_location(wb.source_path, wb.source_id, (gulong)sci_get_current_line(wb.preview)+1);
    render_source(text, ft, valid ? line : 1, path ? path : (doc ? doc->file_name : NULL));
    g_free(wb.source_digest);
    wb.source_digest = source_digest(text);
    g_free(text);
    if (!valid) line = 0;
    g_free(wb.source_path);
    wb.source_path = owned_path;
    wb.source_id = doc ? doc->id : 0;
    wb.source_line = line;
    preview_links();
    gchar *label = g_strdup_printf("Context — %s:%lu%s", owned_path ? owned_path : "untitled", line, line ? "" : " • definition line changed; reindex");
    gtk_label_set_text(GTK_LABEL(wb.title), label);
    g_free(label);
    if (remember) remember_location(wb.source_path, wb.source_id, line);
    navigation_state();
    return valid;
}

/* EN: Navigate to the definition selected in combo. data is unused.
 * RU: Переходит к определению из combo. data не используется. */
static void definition_changed(GtkComboBox *combo, gpointer data)
{
    (void)data;
    if (wb.populating || !wb.definitions) return;
    gint active = gtk_combo_box_get_active(combo);
    if (active<0 || active >= (gint)wb.definitions->len) return;
    Location *location = g_ptr_array_index(wb.definitions, (guint)active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow), FALSE);
    show_location(location->path, location->id, location->line, TRUE);
}
