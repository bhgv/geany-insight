/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Definition resolution from editor and preview context.
 * RU: Поиск определения по контексту редактора и предпросмотра. */


/* EN: Resolve word for doc at reference_line, using preferred and the context in reference_sci at reference_position.
 * RU: Ищет определение word для doc на reference_line с учётом preferred и контекста reference_sci в reference_position. */
static const InsightSymbol *word_definition(GeanyDocument *doc, const char *word, const char *preferred, gulong reference_line, ScintillaObject *reference_sci, gint reference_position)
{
    if (!wb.definitions)
        return NULL;
    if (!preferred && doc) preferred = doc->file_name;
    refresh_index();
    GeanyFiletype *reference_type = reference_sci == wb.preview ? wb.preview_filetype : (doc ? doc->file_type : NULL);
    gboolean valid_word = word && *word && !definition_keyword(reference_type, word) && !insight_languages_keyword(reference_sci, word) && (!reference_sci || (sci_get_length(reference_sci) <= 2*1024*1024 && definition_code(reference_sci, reference_position)));
    wb.populating = TRUE;
    g_ptr_array_set_size(wb.definitions, 0);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(wb.choices));
    const InsightSymbol *found[256], *chosen = NULL;
    gboolean imported = FALSE;
    gsize import_count = 0;
    gchar *scope = NULL, *mask = NULL;
    gboolean uncertain_scope = FALSE;
    gboolean cached = reference_sci == wb.preview && wb.preview_links_text && reference_position >= 0 && (gsize)reference_position <= wb.preview_links_length;
    gboolean python = python_language(reference_type);
    gboolean calls = member_calls_language(reference_type) && reference_sci && sci_get_length(reference_sci) <= 2*1024*1024;
    gint reference_begin = (calls || python) ? 0 : MAX(0, reference_position-4096);
    gchar *reference = !cached && reference_sci ? ((calls || python) ? sci_get_contents(reference_sci, sci_get_length(reference_sci)+1) : sci_get_contents_range(reference_sci, reference_begin, reference_position)) : NULL;
    gboolean member = FALSE;
    gsize reference_length = reference ? ((calls || python) ? (gsize)sci_get_length(reference_sci) : (gsize)(reference_position-reference_begin)) : 0;
    if (reference && (!g_utf8_validate(reference, reference_length, NULL) || memchr(reference, 0, reference_length))) valid_word = FALSE;
    ScopeLanguage language =
    {
        preferred, reference_type
    };
    gchar *owner = NULL;
    if (python)
    {
        const gchar *bytes = cached ? wb.preview_links_text : reference;
        gsize length = cached ? wb.preview_links_length : reference_length;
        mask = !cached && bytes ? insight_source_code_mask(bytes, length, INSIGHT_CODE_PYTHON, NULL) : NULL;
        InsightNativeSource text =
        {
            bytes, mask, length, NULL
        };
        if (cached) text = preview_python_source();
        else text.python = insight_native_python_bindings_new(wb.index, preferred, &text);
        scope = insight_native_python_scope(wb.index, preferred, &text, reference_position);
        import_count = insight_native_python_import_query(wb.index, preferred, scope, &text, reference_position, word, scope_language_eligible, &language, found, G_N_ELEMENTS(found), &imported);
        if (!imported) owner = insight_native_python_owner(wb.index, preferred, scope, reference_line, &text, reference_position, &member, scope_language_eligible, &language);
        if (!member && !imported)
        {
            gchar *binding = insight_native_python_binding_scope(wb.index, preferred, scope, word, &text);
            g_free(scope);
            scope = binding;
            if (!scope) valid_word = FALSE;
        }
        if (!cached)
            insight_native_python_bindings_free(text.python);
    }
    else if (cached)
    {
        scope = preview_function_scope(reference_line, reference_position, &uncertain_scope);
        InsightNativeSource text =
        {
            wb.preview_links_text, wb.preview_code_mask, wb.preview_links_length, NULL
        };
        if (!uncertain_scope) owner = insight_native_member_owner_source_filtered(wb.index, preferred, scope, reference_line, &text, reference_position, &member, calls && wb.preview_code_mask != NULL, scope_language_eligible, &language);
    }
    else if (calls && reference && valid_word)
    {
        mask = insight_source_code_mask(reference, reference_length, INSIGHT_CODE_C, NULL);
        InsightNativeSource text =
        {
            reference, mask, reference_length, NULL
        };
        InsightNativeScopes *scopes = insight_native_scopes_new(wb.index, preferred, &text);
        scope = insight_native_scopes_at(scopes, reference_line, (gsize)reference_position, &uncertain_scope);
        if (!uncertain_scope)
        {
            owner = insight_native_member_owner_source_filtered(wb.index, preferred, scope, reference_line, &text, reference_position, &member, mask != NULL, scope_language_eligible, &language);
        }
        insight_native_scopes_free(scopes);
    }
    else
    {
        scope = insight_native_function_scope(wb.index, preferred, reference_line);
        owner = insight_native_member_owner_calls_filtered(wb.index, preferred, scope, reference_line, reference, reference_position-reference_begin, &member, calls, scope_language_eligible, &language);
    }
    if (uncertain_scope)
    {
        valid_word = FALSE;
    }
    g_free(mask);
    g_free(reference);
    size_t count = valid_word ? (imported ? import_count : member ? insight_member_query(wb.index, owner, word, preferred, scope_language_eligible, &language, found, 256, NULL, NULL) : insight_index_find(wb.index, word, TRUE, found, 256)) : 0;
    gint selected = 0;
    gboolean compiler_choice = FALSE;
    InsightClangResult *semantic = reference_sci ? insight_clang_ui_result(reference_sci):NULL;
    if (insight_clang_result_usable(semantic) && insight_clang_dependencies_fresh(semantic->relations, preferred) && word && *word)
    {
        gchar *usr = insight_clang_symbol_at(semantic->relations, preferred, (guint)MAX(0, reference_position), (guint)reference_line, word);
        const InsightSymbol *definition = NULL;
        if (usr)
        {
            gsize n = insight_index_query(semantic->relations, usr, TRUE, NULL, "@clangdef", preferred, 0, &definition, 1);
            if (n != 1)
            {
                n = insight_index_query(wb.disk_calls, usr, TRUE, NULL, "@clangdef", NULL, 0, &definition, 1);
                if (n != 1) definition = NULL;
            }
        }
        if (definition)
        {
            const InsightSymbol *candidates[256];
            gsize n = insight_index_find(wb.index, word, TRUE, candidates, 256);
            for (gsize i = 0;i<MIN(n, 256u);i++) if (candidates[i]->line == definition->line && !strcmp(candidates[i]->path, definition->path))
            {
                found[0] = candidates[i];
                count = 1;
                compiler_choice = TRUE;
                break;
            }
        }
        if (usr && !compiler_choice)
        {
            const InsightSymbol *target = NULL;
            if (insight_index_query(semantic->relations, usr, TRUE, NULL, "@clangtarget", preferred, 0, &target, 1)>0 && target->scope && *target->scope)
            {
                static InsightSymbol resolved;
                resolved = (InsightSymbol)
                {
                    target->type, target->scope, "", target->inheritance, target->line, "", "", ""
                };
                found[0] = &resolved;
                count = 1;
                compiler_choice = TRUE;
            }
        }
        g_free(usr);
    }
    for (size_t i = 0;i<count && i<256;i++)
    {
        const InsightSymbol *tag = found[i];
        if (!definition_language(preferred, reference_type, tag->path)) continue;
        if (!compiler_choice && (python && !member && !imported ? insight_native_python_rank(wb.index, tag, preferred, scope, reference_line) : insight_native_definition_rank_at(tag, preferred, scope, reference_line))<0) continue;
        gboolean duplicate = FALSE;
        for (guint j = 0;j<wb.definitions->len;j++)
        {
            Location *old = g_ptr_array_index(wb.definitions, j);
            if (old->line == tag->line && !g_strcmp0(old->path, tag->path))
            {
                duplicate = TRUE;
                break;
            }
        }
        if (duplicate) continue;
        Location *location = g_new0(Location, 1);
        location->path = g_strdup(tag->path);
        location->line = tag->line;
        gchar *label = g_strdup_printf("%s %s%s • %s — %s:%lu", tag->type, tag->name, tag->signature, tag->scope, tag->path, tag->line);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.choices), label);
        g_free(label);
        if (!chosen || (python && !member && !imported ? insight_native_python_rank(wb.index, tag, preferred, scope, reference_line) : insight_native_definition_rank_at(tag, preferred, scope, reference_line))>(python && !member && !imported ? insight_native_python_rank(wb.index, chosen, preferred, scope, reference_line) : insight_native_definition_rank_at(chosen, preferred, scope, reference_line)))
        {
            chosen = tag;
            selected = (gint)wb.definitions->len;
        }
        g_ptr_array_add(wb.definitions, location);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(wb.choices), wb.definitions->len ? selected : -1);
    wb.populating = FALSE;
    g_free(scope);
    g_free(owner);
    return chosen;
}
