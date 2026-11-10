/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Relation search context and source dialect helpers.
 * RU: Контекст поиска связей и определение диалекта исходников. */


/* EN: Cancel the current usage search and clear its pending state.
 * RU: Отменяет поиск использований и очищает его состояние. */
static void stop_usage(void)
{
    g_clear_pointer(&wb.usage_existing, g_hash_table_destroy);
    wb.usage_collecting = FALSE;
    wb.usage_generation++;
    if (wb.search_worker)
    {
        g_cancellable_cancel(wb.search_worker);
        g_clear_object(&wb.search_worker);
    }
    if (wb.usage_idle)
    {
        g_source_remove(wb.usage_idle);
        wb.usage_idle = 0;
    }
    if (wb.usage_paths)
        g_ptr_array_set_size(wb.usage_paths, 0);
    g_clear_pointer(&wb.usage_name, g_free);
    g_clear_pointer(&wb.usage_usr, g_free);
}

/* EN: Invalidate searches that depend on the active source context.
 * RU: Сбрасывает результаты, зависящие от текущего контекста исходника. */
static void invalidate_search_context(void)
{
    gboolean active = wb.usage_idle || wb.search_worker;
    stop_usage();
    if (wb.matches) gtk_list_store_clear(wb.matches);
    if (wb.search_matches) gtk_list_store_clear(wb.search_matches);
    if (active) set_usage_status("Workspace changed; run the search again");
}

/* EN: Determine the source dialect for ft and store it in out.
 * RU: Определяет диалект исходника для ft и записывает его в out. */
static gboolean source_dialect(GeanyFiletype *ft, InsightCodeDialect *out)
{
    if (!ft)
        return FALSE;
    const char *name = ft->name;
    if (!g_strcmp0(name, "Python")) *out = INSIGHT_CODE_PYTHON;
    else if (!g_strcmp0(name, "Lua")) *out = INSIGHT_CODE_LUA;
    else if (!g_strcmp0(name, "JavaScript") || !g_strcmp0(name, "TypeScript")) *out = INSIGHT_CODE_JS;
    else if (!g_strcmp0(name, "Sh")) *out = INSIGHT_CODE_SHELL;
    else if (!g_strcmp0(name, "C") || !g_strcmp0(name, "C++") || !g_strcmp0(name, "C#") || !g_strcmp0(name, "Java")) *out = INSIGHT_CODE_C;
    else return FALSE;
    return TRUE;
}

/* EN: Return the function syntax style for dialect.
 * RU: Возвращает стиль синтаксиса функций для dialect. */
static InsightFunctionStyle function_style(InsightCodeDialect dialect)
{
    return dialect == INSIGHT_CODE_PYTHON ? INSIGHT_FUNCTION_PYTHON : (dialect == INSIGHT_CODE_LUA ? INSIGHT_FUNCTION_LUA : INSIGHT_FUNCTION_BRACES);
}

/* EN: Extract the line around offset at in text.
 * RU: Извлекает строку около позиции at в text. */
static gchar *reference_snippet(const char *text, gsize at)
{
    gsize begin = at, end = at;
    while (begin && at-begin<320 && text[begin-1] != '\n' && text[begin-1] != '\r') begin--;
    while (((guchar)text[begin]&0xc0) == 0x80) begin++;
    while (text[end] && end-at<480 && text[end] != '\n' && text[end] != '\r') end++;
    while (((guchar)text[end]&0xc0) == 0x80) end++;
    gchar *line = g_strndup(text+begin, end-begin);
    g_strstrip(line);
    if (g_utf8_strlen(line, -1)>180)
    {
        gchar *shortened = g_utf8_substring(line, 0, 180);
        g_free(line);
        line = g_strconcat(shortened, "…", NULL);
        g_free(shortened);
    }
    return line;
}

/* EN: Find the function enclosing the cursor in doc.
 * RU: Находит функцию вокруг курсора в doc. */
static TMTag *relation_enclosing_function(GeanyDocument *doc)
{
    if (!doc || !doc->tm_file || !doc->tm_file->tags_array || sci_get_length(doc->editor->sci)>2*1024*1024)
        return NULL;
    InsightCodeDialect dialect;
    if (!source_dialect(doc->file_type, &dialect))
        return NULL;
    gint length = sci_get_length(doc->editor->sci);
    gchar *text = sci_get_contents(doc->editor->sci, length+1), *mask = insight_source_code_mask(text, length, dialect, NULL);
    if (!mask)
    {
        g_free(text);
        return NULL;
    }
    size_t caret = (size_t)sci_get_current_position(doc->editor->sci), best = SIZE_MAX;
    TMTag *selected = NULL;
    for (guint i = 0;i<doc->tm_file->tags_array->len;i++)
    {
        TMTag *tag = g_ptr_array_index(doc->tm_file->tags_array, i);
        if (!tag || !(tag->type&(tm_tag_function_t|tm_tag_method_t)) || !tag->line || tag->line>(gulong)sci_get_line_count(doc->editor->sci)) continue;
        size_t start = (size_t)sci_get_position_from_line(doc->editor->sci, tag->line-1), end = 0, body = 0;
        if (start <= caret && insight_function_range(text, mask, length, start, function_style(dialect), &body, &end) == 1 && caret<end && end-start<best)
        {
            selected = tag;
            best = end-start;
        }
    }
    g_free(mask);
    g_free(text);
    return selected;
}

/* EN: Show calls matching query in the relation panel.
 * RU: Показывает вызовы по query в панели связей. */
static void relation_calls(const char *query)
{
    gtk_list_store_clear(wb.matches);
    GeanyDocument *doc = document_get_current();
    InsightCodeDialect dialect;
    if (!doc || sci_get_length(doc->editor->sci)>2*1024*1024 || !source_dialect(doc->file_type, &dialect))
    {
        set_usage_status("Calls requires a supported function language and a current buffer up to 2 MiB");
        return;
    }
    document_update_tags(doc);
    if (!doc->tm_file || !doc->tm_file->tags_array)
    {
        set_usage_status("No parsed function definitions");
        return;
    }
    TMTag *chosen = NULL;
    guint count = 0;
    for (guint i = 0;i<doc->tm_file->tags_array->len;i++)
    {
        TMTag *tag = g_ptr_array_index(doc->tm_file->tags_array, i);
        if (tag && (tag->type&(tm_tag_function_t|tm_tag_method_t)) && tag->name && !strcmp(tag->name, query))
        {
            chosen = tag;
            count++;
        }
    }
    if (count>1)
    {
        TMTag *current = relation_enclosing_function(doc);
        if (current && !strcmp(current->name, query)) chosen = current;
        else
        {
            set_usage_status("Several definitions: place the caret inside the desired function");
            return;
        }
    }
    if (!chosen || !chosen->line || chosen->line>(gulong)sci_get_line_count(doc->editor->sci))
    {
        set_usage_status("Function definition not found in the current buffer");
        return;
    }
    gint length = sci_get_length(doc->editor->sci);
    gchar *text = sci_get_contents(doc->editor->sci, length+1), *mask = insight_source_code_mask(text, length, dialect, NULL);
    size_t start = (size_t)sci_get_position_from_line(doc->editor->sci, chosen->line-1), end = 0, body = 0;
    if (!mask || insight_function_range(text, mask, length, start, function_style(dialect), &body, &end) != 1)
    {
        set_usage_status("No complete function body");
        g_free(mask);
        g_free(text);
        return;
    }
    gchar *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, text, -1);
    size_t cursor = body, a = 0, b = 0;
    guint shown = 0;
    while (shown<512 && insight_next_call(mask, end, &cursor, &a, &b) == 1)
    {
        gchar *name = g_strndup(text+a, b-a), *snippet = reference_snippet(text, a);
        GtkTreeIter iter;
        gint line = sci_get_line_from_position(doc->editor->sci, (gint)a);
        gtk_list_store_append(wb.matches, &iter);
        gtk_list_store_set(wb.matches, &iter, USAGE_DISPLAY, doc->file_name ? doc->file_name : "untitled", USAGE_PATH, doc->file_name, USAGE_LINE, (gulong)line+1, USAGE_START, (gulong)a, USAGE_END, (gulong)b, USAGE_ID, doc->id, USAGE_DIGEST, digest, USAGE_SYMBOL, name, USAGE_SNIPPET, snippet, -1);
        g_free(name);
        g_free(snippet);
        shown++;
    }
    gchar *status = g_strdup_printf("%u lexical call sites%s • current function; overloads and types are not resolved", shown, shown == 512 ? " (limit reached)" : "");
    set_usage_status(status);
    g_free(status);
    g_free(digest);
    g_free(mask);
    g_free(text);
}

/* EN: Return the active source region used for reference searches.
 * RU: Возвращает область исходника для поиска ссылок. */
static InsightSourceRegion reference_region(void)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code)))
        return INSIGHT_REGION_CODE;
    gint mode = gtk_combo_box_get_active(GTK_COMBO_BOX(wb.usage_comments));
    return mode == 1 ? INSIGHT_REGION_SKIP_COMMENTS : mode == 2 ? INSIGHT_REGION_COMMENTS : INSIGHT_REGION_ALL;
}
typedef struct
{
    gchar *path, *query, *text, *digest, *cached_stamp, *cached_language, *clang_root, *clang_resources, *usr;
    InsightIndex *cached_calls;
    InsightOccurrence *found;
    GPtrArray *snippets;
    gulong *columns;
    gsize total, apply_position;
    guint id, flags, context, capacity;
    gint mode, encoding;
    InsightCodeDialect dialect;
    gboolean keywords, have_dialect, valid, partial;
    InsightSourceRegion region;
    guint64 generation, epoch;
}
SearchJob;
