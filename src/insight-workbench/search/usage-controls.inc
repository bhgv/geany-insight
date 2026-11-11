/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Usage search commands and result navigation.
 * RU: Команды поиска использований и навигация по результатам. */


/* EN: Start a usage search using widget and its associated data.
 * RU: Запускает поиск использований с параметрами widget и data. */
static void usage_start(GtkWidget *widget, gpointer data)
{
    (void)data;
    stop_usage();
    /* Explicit searches must reveal their results, even when the last view was
  * a source route. Background tracking must leave that independent graph open. */
    if (widget && wb.relation_stack)
    {
        if (!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)), "routes")) gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "list");
        relation_view_update();
    }
    wb.usage_region = reference_region();
    gint search_region = gtk_combo_box_get_active(GTK_COMBO_BOX(wb.search_region));
    wb.project_region = search_region == 1 ? INSIGHT_REGION_SKIP_COMMENTS : search_region == 2 ? INSIGHT_REGION_COMMENTS : search_region == 3 ? INSIGHT_REGION_CODE : INSIGHT_REGION_ALL;
    const char *query = gtk_entry_get_text(GTK_ENTRY(wb.usage_entry));
    size_t total;
    gint mode = gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));
    wb.usage_mode = mode;
    wb.usage_target = mode == 3 ? wb.search_matches : wb.matches;
    if (mode == 8)
    {
        if (!insight_clang_available())
        {
            set_usage_status("This build has no bundled Clang backend");
            return;
        }
        GeanyDocument *doc = document_get_current();
        InsightClangResult *semantic = DOC_VALID(doc) ? insight_clang_ui_result(doc->editor->sci):NULL;
        if (insight_clang_result_usable(semantic))
            wb.usage_usr = insight_clang_symbol_at(semantic->relations, doc->file_name, (guint)sci_get_current_position(doc->editor->sci), (guint)sci_get_current_line(doc->editor->sci)+1, query);
    }
    if (mode == 11 || mode == 12)
    {
        hdl_references(query, mode == 12);
        return;
    }
    if (mode == 9 || mode == 10)
    {
        semantic_call_tree(query, mode == 10);
        return;
    }
    if (mode == 6)
    {
        relation_calls(query);
        return;
    }
    if (mode == 1 || mode == 2 || mode == 4 || mode == 5)
    {
        refresh_index();
        RelationJob *j = g_new0(RelationJob, 1);
        j->index = insight_index_ref(wb.index);
        j->query = g_strdup(query);
        j->mode = mode;
        j->transitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_transitive));
        j->epoch = workbench_epoch;
        j->generation = wb.usage_generation;
        j->index_version = wb.index_version;
        GeanyDocument *active = document_get_current();
        if (active) j->language = insight_language_filter_new(active->file_name, active->file_type);
        wb.search_worker = g_cancellable_new();
        GTask *task = g_task_new(NULL, wb.search_worker, relation_job_done, NULL);
        g_task_set_task_data(task, j, relation_job_free);
        g_task_run_in_thread(task, relation_job_run);
        g_object_unref(task);
        set_usage_status("Searching symbol relations…");
        return;
    }
    wb.usage_flags = (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_case)) ? INSIGHT_SEARCH_CASE : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_word)) ? INSIGHT_SEARCH_WORD : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.project_regex)) ? INSIGHT_SEARCH_REGEX : 0);
    GError *query_error = NULL;
    wb.usage_keywords = mode == 3 && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.search_keywords));
    wb.usage_context = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(wb.search_context));
    if (wb.usage_keywords && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.search_variations)))
        wb.usage_flags |= INSIGHT_KEYWORD_VARIATIONS;
    if (mode == 3 && !(wb.usage_keywords ? insight_keyword_collect("", 0, query, wb.usage_flags, wb.usage_context, NULL, 0, &total, &query_error) : insight_context_collect("", 0, query, wb.usage_flags, NULL, 0, &total, &query_error)))
    {
        set_usage_status(query_error ? query_error->message : "Invalid text search query");
        g_clear_error(&query_error);
        return;
    }
    if (mode != 3 && !insight_occurrences_find("", 0, query, strlen(query), 0, NULL, 0, &total))
    {
        set_usage_status("Enter one UTF-8 identifier");
        return;
    }
    if (mode == 3)
        insight_search_links_clear(wb.search_links);
    wb.usage_existing = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, symbol_iter_queue_free);
    wb.usage_collecting = TRUE;
    wb.usage_iter_valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(wb.usage_target), &wb.usage_iter);
    if (mode == 3)
    {
        gtk_entry_set_text(GTK_ENTRY(wb.project_query), query);
        insight_search_history_record(wb.search_history, query);
        save_search_settings();
        if (widget) focus_search_results(NULL, NULL);
    }
    wb.usage_name = g_strdup(query);
    wb.usage_position = wb.usage_shown = wb.usage_skipped = 0;
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    gint scope = mode == 3 ? gtk_combo_box_get_active(GTK_COMBO_BOX(wb.search_scope)) : (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer)) ? 2 : 0);
    GPtrArray *paths = mode == 3 && scope == 3 ? wb.search_files : wb.files;
    if (scope == 0 || scope == 3)
    {
        for (guint i = 0;i<paths->len && wb.usage_paths->len<10000;i++)
        {
            const char *path = g_ptr_array_index(paths, i);
            GeanyDocument *active = document_get_current();
            if (active && !insight_languages_search_path(active->file_name, active->file_type, path)) continue;
            if (g_hash_table_contains(seen, path) || (mode == 3 && !search_path_matches(path))) continue;
            Location *next = g_new0(Location, 1);
            next->path = g_strdup(path);
            g_ptr_array_add(wb.usage_paths, next);
            g_hash_table_add(seen, next->path);
        }
    }
    for (guint i = 0;i<documents_array->len && wb.usage_paths->len<10000;i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        if (!doc->is_valid || (scope == 2 && doc != document_get_current()) || (mode == 3 && (scope == 0 || scope == 3)) || (mode == 3 && doc->file_name && !search_path_matches(doc->file_name))) continue;
        GeanyDocument *active = document_get_current();
        if (active && doc->file_name && !insight_languages_search_path(active->file_name, active->file_type, doc->file_name)) continue;
        if (doc->file_name && g_hash_table_contains(seen, doc->file_name)) continue;
        Location *next = g_new0(Location, 1);
        next->path = g_strdup(doc->file_name);
        next->id = doc->id;
        g_ptr_array_add(wb.usage_paths, next);
        if (next->path)
            g_hash_table_add(seen, next->path);
    }
    g_hash_table_destroy(seen);
    wb.usage_idle = g_idle_add(usage_step, NULL);
}

/* EN: Cancel the current search. widget and data are unused.
 * RU: Отменяет текущий поиск. widget и data не используются. */
static void usage_cancel(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    stop_usage();
    if (wb.relation_tree)
        insight_relation_tree_cancel(wb.relation_tree);
    set_usage_status("Search cancelled; partial results retained");
}

/* EN: Show the source for the selected usage.
 * RU: Показывает исходник для выбранного использования selection. */
static void usage_selected(GtkTreeSelection *selection, gpointer data)
{
    (void)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    if (wb.search_links && model == GTK_TREE_MODEL(wb.search_matches))
    {
        GtkTreePath *row = gtk_tree_model_get_path(model, &iter);
        insight_search_links_refresh(wb.search_links, row);
        gtk_tree_path_free(row);
    }
    gchar *path, *digest;
    gulong line, start, end;
    guint id;
    gtk_tree_model_get(model, &iter, USAGE_PATH, &path, USAGE_LINE, &line, USAGE_START, &start, USAGE_END, &end, USAGE_ID, &id, USAGE_DIGEST, &digest, -1);
    if (!path && !id && !line)
    {
        wb.source_line = 0;
        navigation_state();
        g_free(digest);
        return;
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow), FALSE);
    if (show_location(path, id, line, TRUE))
    {
        gchar *text = sci_get_contents(wb.preview, sci_get_length(wb.preview)+1), *actual = g_compute_checksum_for_string(G_CHECKSUM_SHA256, text, -1);
        if (!digest)
        {
            /* Definitions and members navigate by current tag location. */
        }
        else if (!g_strcmp0(actual, digest))
        {
            SSM(wb.preview, SCI_SETSEL, end, start);
            decorate_match(wb.preview, start, end);
            SSM(wb.preview, SCI_VERTICALCENTRECARET, 0, 0);
        }
        else
        {
            decorate_match(wb.preview, 0, 0);
            wb.source_line = 0;
            navigation_state();
            ui_set_statusbar(TRUE, "Source changed after search; run the usage search again");
        }
        g_free(actual);
        g_free(text);
    }
    g_free(path);
    g_free(digest);
}

/* EN: Open the usage at path in view. column and data are unused.
 * RU: Открывает использование по path в view. column и data не используются. */
static void usage_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data)
{
    (void)column;
    (void)data;
    if (view == GTK_TREE_VIEW(wb.search_view) && insight_search_links_open(wb.search_links, path)) return;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
    gtk_tree_selection_select_path(selection, path);
    usage_selected(selection, NULL);
    if (wb.source_line) open_context(NULL, NULL);
}
