/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Source preview language filters and definition links.
 * RU: Языковые фильтры и ссылки на определения в предпросмотре. */


/* EN: Check whether source and path can share definitions using file type from.
 * RU: Проверяет совместимость определений source и path с учётом типа from. */
static gboolean definition_language(const gchar *source, GeanyFiletype *from, const gchar *path)
{
    return insight_languages_compatible(source, from, path);
}
typedef struct
{
    const gchar *path;
    GeanyFiletype *type;
} ScopeLanguage;

/* EN: Return whether symbol matches the language filter in data.
 * RU: Проверяет, соответствует ли symbol языковому фильтру из data. */
static int scope_language_eligible(const InsightSymbol *symbol, void *data)
{
    ScopeLanguage *scope = data;
    return definition_language(scope->path, scope->type, symbol->path);
}

/* EN: Check whether type uses the Python parser.
 * RU: Проверяет, использует ли type парсер Python. */
static gboolean python_language(GeanyFiletype *type)
{
    return type && type->lang == TM_PARSER_PYTHON;
}

/* EN: Check whether type supports native member-call resolution.
 * RU: Проверяет поддержку разрешения вызовов методов для type. */
static gboolean member_calls_language(GeanyFiletype *type)
{
    return type && (type->lang == TM_PARSER_C || type->lang == TM_PARSER_CPP || type->lang == TM_PARSER_CUDA);
}

/* EN: Check whether word is a keyword or built-in literal for ft.
 * RU: Проверяет, является ли word ключевым словом или литералом для ft. */
static gboolean definition_keyword(GeanyFiletype *ft, const gchar *word)
{
    if (ft && (ft->lang == TM_PARSER_C || ft->lang == TM_PARSER_CPP || ft->lang == TM_PARSER_CUDA) && (!strcmp(word, "false") || !strcmp(word, "true") || !strcmp(word, "nullptr")))
        return TRUE;
    return highlighting_is_primary_keyword(ft, word);
}

/* EN: Check whether position in sci lies outside a comment or string.
 * RU: Проверяет, находится ли position в sci вне комментария и строки. */
static gboolean definition_code(ScintillaObject *sci, gint position)
{
    gint lexer = SSM(sci, SCI_GETLEXER, 0, 0), style = SSM(sci, SCI_GETSTYLEAT, position, 0);
    return !highlighting_is_comment_style(lexer, style) && !highlighting_is_string_style(lexer, style);
}

/* EN: Release the cached scopes and Python bindings for the preview.
 * RU: Освобождает кэш областей и привязок Python предпросмотра. */
static void preview_scopes_clear(void)
{
    g_clear_pointer(&wb.preview_python_bindings, insight_native_python_bindings_free);
    g_clear_pointer(&wb.preview_python_scopes, g_hash_table_unref);
    g_clear_pointer(&wb.preview_scopes, insight_native_scopes_free);
    g_clear_pointer(&wb.preview_scopes_path, g_free);
    wb.preview_scopes_ready = FALSE;
}

/* EN: Return the preview source with current Python bindings.
 * RU: Возвращает исходник предпросмотра с актуальными привязками Python. */
static InsightNativeSource preview_python_source(void)
{
    InsightNativeSource source =
    {
        wb.preview_links_text, wb.preview_code_mask, wb.preview_links_length, NULL
    };
    if (!wb.preview_python_bindings || wb.preview_python_binding_version != wb.index_version)
    {
        g_clear_pointer(&wb.preview_python_bindings, insight_native_python_bindings_free);
        wb.preview_python_bindings = insight_native_python_bindings_new(wb.index, wb.source_path, &source);
        wb.preview_python_binding_version = wb.index_version;
    }
    source.python = wb.preview_python_bindings;
    return source;
}

/* EN: Find the scope at line and position. Set uncertain for ambiguous scopes.
 * RU: Находит область по line и position. Устанавливает uncertain при неоднозначности. */
static gchar *preview_function_scope(gulong line, gint position, gboolean *uncertain)
{
    *uncertain = FALSE;
    if (python_language(wb.preview_filetype))
    {
        if (!wb.preview_python_scopes || wb.preview_python_version != wb.index_version)
        {
            g_clear_pointer(&wb.preview_python_scopes, g_hash_table_unref);
            wb.preview_python_scopes = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
            wb.preview_python_version = wb.index_version;
        }
        gpointer cached = NULL;
        if (g_hash_table_lookup_extended(wb.preview_python_scopes, GUINT_TO_POINTER((guint)line), NULL, &cached))
            return *(gchar *)cached ? g_strdup(cached) : NULL;
        InsightNativeSource source =
        {
            wb.preview_links_text, wb.preview_code_mask, wb.preview_links_length, NULL
        };
        gchar *scope = insight_native_python_scope(wb.index, wb.source_path, &source, (gsize)position);
        g_hash_table_insert(wb.preview_python_scopes, GUINT_TO_POINTER((guint)line), g_strdup(scope ? scope : ""));
        return scope;
    }
    if (!member_calls_language(wb.preview_filetype))
        return insight_native_function_scope(wb.index, wb.source_path, line);
    if (!wb.preview_scopes_ready || wb.preview_scopes_version != wb.index_version || g_strcmp0(wb.preview_scopes_path, wb.source_path))
    {
        preview_scopes_clear();
        InsightNativeSource source =
        {
            wb.preview_links_text, wb.preview_code_mask, wb.preview_links_length, NULL
        };
        wb.preview_scopes = insight_native_scopes_new(wb.index, wb.source_path, &source);
        wb.preview_scopes_path = g_strdup(wb.source_path);
        wb.preview_scopes_version = wb.index_version;
        wb.preview_scopes_ready = TRUE;
    }
    return insight_native_scopes_at(wb.preview_scopes, line, (gsize)position, uncertain);
}

/* EN: Check whether word at position can link to a definition.
 * RU: Проверяет, можно ли перейти от word в position к определению. */
static gboolean preview_resolvable(const gchar *word, gint position)
{
    if (insight_languages_keyword(wb.preview, word) || definition_keyword(wb.preview_filetype, word) || !definition_code(wb.preview, position))
        return FALSE;
    GPtrArray *tags = g_hash_table_lookup(wb.preview_candidates, word);
    if (!tags && !python_language(wb.preview_filetype))
        return FALSE;
    const gchar *lexical = member_calls_language(wb.preview_filetype) ? wb.preview_code_mask : NULL;
    if (lexical && position >= 0 && (gsize)position<wb.preview_links_length && lexical[position] != wb.preview_links_text[position])
        return FALSE;
    gboolean scoped = FALSE, member = FALSE;
    gint at = position;
    while (at>0 && g_ascii_isspace(lexical ? lexical[at-1] : sci_get_char_at(wb.preview, at-1))) at--;
    gchar before = at>0 ? (lexical ? lexical[at-1] : sci_get_char_at(wb.preview, at-1)) : 0, previous = at>1 ? (lexical ? lexical[at-2] : sci_get_char_at(wb.preview, at-2)) : 0;
    gboolean qualified = before == '.' || (at > 1 && ((previous == '-' && before == '>') || (previous == ':' && before == ':')));
    for (guint i = 0; tags && i < tags->len; i++)
    {
        const InsightSymbol *tag = g_ptr_array_index(tags, i);
        if (!strcmp(tag->kind, "l") || !strcmp(tag->kind, "z"))
            scoped = TRUE;
    }
    gulong line = (gulong)SSM(wb.preview, SCI_LINEFROMPOSITION, position, 0)+1;
    gboolean uncertain_scope = FALSE;
    gchar *scope = (scoped || qualified || python_language(wb.preview_filetype)) ? preview_function_scope(line, position, &uncertain_scope) : NULL, *owner = NULL;
    if (uncertain_scope)
    {
        g_free(scope);
        return FALSE;
    }
    if (python_language(wb.preview_filetype))
    {
        InsightNativeSource text = preview_python_source();
        gboolean imported = FALSE;
        const InsightSymbol *hit;
        ScopeLanguage language =
        {
            wb.source_path, wb.preview_filetype
        };
        gsize count = insight_native_python_import_query(wb.index, wb.source_path, scope, &text, position, word, scope_language_eligible, &language, &hit, 1, &imported);
        if (imported || !tags)
        {
            g_free(scope);
            return count>0;
        }
    }
    if (qualified)
    {
        member = TRUE;
        if (wb.preview_links_text)
        {
            InsightNativeSource reference =
            {
                wb.preview_links_text, wb.preview_code_mask, wb.preview_links_length, NULL
            };
            if (python_language(wb.preview_filetype))
                reference = preview_python_source();
            ScopeLanguage language =
            {
                wb.source_path, wb.preview_filetype
            };
            owner = python_language(wb.preview_filetype) ? insight_native_python_owner(wb.index, wb.source_path, scope, line, &reference, position, &member, scope_language_eligible, &language) : insight_native_member_owner_source_filtered(wb.index, wb.source_path, scope, line, &reference, position, &member, member_calls_language(wb.preview_filetype) && wb.preview_code_mask != NULL, scope_language_eligible, &language);
        }
    }
    gboolean found = FALSE;
    if (!member && python_language(wb.preview_filetype))
    {
        InsightNativeSource text = preview_python_source();
        gchar *binding = insight_native_python_binding_scope(wb.index, wb.source_path, scope, word, &text);
        g_free(scope);
        scope = binding;
        if (!scope)
        {
            g_free(owner);
            return FALSE;
        }
    }
    if (member)
    {
        ScopeLanguage language =
        {
            wb.source_path, wb.preview_filetype
        };
        const InsightSymbol *hit;
        found = insight_member_query(wb.index, owner, word, wb.source_path, scope_language_eligible, &language, &hit, 1, NULL, NULL)>0;
    }
    else
        for (guint i = 0; i < tags->len; i++)
    {
        const InsightSymbol *tag = g_ptr_array_index(tags, i);
        if ((python_language(wb.preview_filetype) ? insight_native_python_rank(wb.index, tag, wb.source_path, scope, line) : insight_native_definition_rank_at(tag, wb.source_path, scope, line)) >= 0)
        {
            found = TRUE;
            break;
        }
    }
    g_free(scope);
    g_free(owner);
    return found;
}

/* EN: Resolve and paint links for one idle slice. data is unused.
 * RU: Разрешает и подсвечивает ссылки за один проход. data не используется. */
static gboolean preview_links_step(gpointer data)
{
    (void)data;
    gint64 deadline = g_get_monotonic_time()+6000;
    gsize count = insight_index_count(wb.index);
    while (wb.preview_names_position<count)
    {
        const InsightSymbol *symbol = insight_index_at(wb.index, wb.preview_names_position++);
        if (symbol && definition_language(wb.source_path, wb.preview_filetype, symbol->path) && !definition_keyword(wb.preview_filetype, symbol->name))
        {
            GPtrArray *tags = g_hash_table_lookup(wb.preview_candidates, symbol->name);
            if (!tags)
            {
                tags = g_ptr_array_new();
                g_hash_table_insert(wb.preview_candidates, g_strdup(symbol->name), tags);
            }
            g_ptr_array_add(tags, (gpointer)symbol);
        }
        if (g_get_monotonic_time() >= deadline)
            return G_SOURCE_CONTINUE;
    }
    SSM(wb.preview, SCI_SETINDICATORCURRENT, INSIGHT_LINK, 0);
    gint length = (gint)wb.preview_links_length;
    while (wb.preview_links_position<length)
    {
        gint position = wb.preview_links_position;
        if (position >= wb.preview_styled_position)
        {
            wb.preview_styled_position = MIN(length, position+16384);
            SSM(wb.preview, SCI_COLOURISE, position, wb.preview_styled_position);
        }
        gint end = (gint)SSM(wb.preview, SCI_WORDENDPOSITION, position, TRUE);
        if (end <= position)
            wb.preview_links_position = (gint)SSM(wb.preview, SCI_POSITIONAFTER, position, 0);
        else
        {
            gint start = (gint)SSM(wb.preview, SCI_WORDSTARTPOSITION, position, TRUE);
            if (start >= 0 && end <= length && end-start <= 1024)
            {
                gchar *name = g_strndup(wb.preview_links_text+start, (gsize)(end-start));
                if (preview_resolvable(name, start))
                {
                    SSM(wb.preview, SCI_SETINDICATORCURRENT, INSIGHT_LINK, 0);
                    SSM(wb.preview, SCI_INDICATORFILLRANGE, start, end-start);
                }
                g_free(name);
            }
            wb.preview_links_position = end;
        }
        if (wb.preview_links_position <= position)
            wb.preview_links_position = position+1;
        if (g_get_monotonic_time() >= deadline)
            return G_SOURCE_CONTINUE;
    }
    wb.preview_links_idle = 0;
    return G_SOURCE_REMOVE;
}

/* EN: Refresh the preview links after source or index changes.
 * RU: Обновляет ссылки предпросмотра после изменения исходника или индекса. */
static void preview_links(void)
{
    if (!wb.preview) return;
    refresh_index();
    gchar *text = sci_get_contents(wb.preview, sci_get_length(wb.preview)+1);
    if (wb.preview_links_valid && wb.preview_links_text && !strcmp(text, wb.preview_links_text) && !g_strcmp0(wb.source_path, wb.preview_links_path) && wb.preview_filetype == wb.preview_links_filetype && wb.index_version == wb.preview_links_version)
    {
        g_free(text);
        return;
    }
    if (wb.preview_links_idle)
    {
        g_source_remove(wb.preview_links_idle);
        wb.preview_links_idle = 0;
    }
    g_free(wb.preview_links_path);
    wb.preview_links_path = g_strdup(wb.source_path);
    wb.preview_links_filetype = wb.preview_filetype;
    wb.preview_links_version = wb.index_version;
    wb.preview_links_valid = TRUE;
    g_clear_pointer(&wb.preview_candidates, g_hash_table_destroy);
    wb.preview_candidates = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
    wb.preview_names_position = 0;
    SSM(wb.preview, SCI_INDICSETSTYLE, INSIGHT_LINK, INDIC_PLAIN);
    SSM(wb.preview, SCI_INDICSETFORE, INSIGHT_LINK, 0xcc5500);
    SSM(wb.preview, SCI_SETINDICATORCURRENT, INSIGHT_LINK, 0);
    SSM(wb.preview, SCI_INDICATORCLEARRANGE, 0, sci_get_length(wb.preview));
    preview_scopes_clear();
    gboolean calls = member_calls_language(wb.preview_filetype), python = python_language(wb.preview_filetype);
    if (!wb.preview_links_text || strcmp(wb.preview_links_text, text) || calls != wb.preview_calls || python != wb.preview_python)
    {
        g_clear_pointer(&wb.preview_code_mask, g_free);
        if (calls || python)
            wb.preview_code_mask = insight_source_code_mask(text, strlen(text), python ? INSIGHT_CODE_PYTHON : INSIGHT_CODE_C, NULL);
    }
    g_free(wb.preview_links_text);
    wb.preview_links_text = text;
    wb.preview_links_length = strlen(text);
    wb.preview_calls = calls;
    wb.preview_python = python;
    wb.preview_links_position = 0;
    wb.preview_styled_position = 0;
    wb.preview_links_idle = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, preview_links_step, NULL, NULL);
}

/* EN: Update the cursor for event. widget and data are unused.
 * RU: Обновляет курсор по event. widget и data не используются. */
static gboolean preview_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    (void)widget;
    (void)data;
    gint position = (gint)SSM(wb.preview, SCI_POSITIONFROMPOINTCLOSE, (uptr_t)event->x, (sptr_t)event->y);
    /* EN: The embedded Scintilla hand cursor has value 8.
     * RU: Встроенный курсор-рука Scintilla имеет значение 8. */
    SSM(wb.preview, SCI_SETCURSOR, position >= 0 && SSM(wb.preview, SCI_INDICATORVALUEAT, INSIGHT_LINK, position) ? 8 : SC_CURSORNORMAL, 0);
    return FALSE;
}

/* EN: Refresh selection decoration in sci for notification. message and data are unused.
 * RU: Обновляет подсветку выделения в sci по notification. message и data не используются. */
static void preview_notify(ScintillaObject *sci, gint message, SCNotification *notification, gpointer data)
{
    (void)message;
    (void)data;
    if (notification->nmhdr.code == SCN_UPDATEUI || notification->nmhdr.code == SCN_PAINTED) decorate_selection(sci);
}
