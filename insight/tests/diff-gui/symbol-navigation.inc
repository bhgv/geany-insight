/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise snapshot-local definition navigation, ambiguity and source history using production widgets.
 * RU: Проверяет определения снимков, неоднозначность и историю переходов на настоящих виджетах. */

/* EN: Return current character offset of m's source side.
 * RU: Возвращает символьное смещение курсора исходника side в m. */
static gint symbol_test_caret(Merge *m, guint side)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));
    GtkTextIter at;
    gtk_text_buffer_get_iter_at_mark(buffer, &at, gtk_text_buffer_get_insert(buffer));
    return gtk_text_iter_get_offset(&at);
}

/* EN: Select the second candidate of the active definition chooser; fail if it was not opened.
 * RU: Выбирает второго кандидата открытого диалога определений; отсутствие диалога завершает проверку. */
static gboolean symbol_test_choose(gpointer data)
{
    (void)data;
    GList *windows = gtk_window_list_toplevels();
    gboolean found = FALSE;
    for (GList *it = windows; it; it = it->next)
    {
        GtkWidget *dialog = it->data;
        if (g_strcmp0(gtk_widget_get_name(dialog), "insight-diff-definition-choices"))
            continue;
        GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GList *children = gtk_container_get_children(GTK_CONTAINER(box));
        for (GList *child = children; child; child = child->next)
        {
            if (!GTK_IS_SCROLLED_WINDOW(child->data))
                continue;
            GtkWidget *tree = gtk_bin_get_child(GTK_BIN(child->data));
            GtkTreePath *path = gtk_tree_path_new_from_indices(1, -1);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), path, NULL, FALSE);
            gtk_tree_path_free(path);
        }
        g_list_free(children);
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        found = TRUE;
    }
    g_list_free(windows);
    g_assert_true(found);
    return G_SOURCE_REMOVE;
}

/* EN: Add owned definition name/begin/end to m's side; offsets refer to UTF-8 bytes in its snapshot.
 * RU: Добавляет собственное определение name/begin/end в сторону side m; смещения — байты UTF-8 снимка. */
static void symbol_test_add(Merge *m, guint side, const gchar *name, gsize begin, gsize end)
{
    if (!m->symbols[side])
        m->symbols[side] = g_ptr_array_new_with_free_func(insight_diff_symbol_free);
    InsightDiffSymbol *symbol = g_new0(InsightDiffSymbol, 1);
    symbol->name = g_strdup(name);
    symbol->begin = begin;
    symbol->end = end;
    g_ptr_array_add(m->symbols[side], symbol);
}

/* EN: Verify local matching, UTF-8 byte conversion, ambiguity choice, history reset and whole-dollar identifiers.
 * RU: Проверяет локальные имена, байты UTF-8, выбор кандидата, сброс истории и идентификаторы с долларом. */
static void file_compare_symbol_navigation_test(void)
{
    Merge *m = merge_new(NULL);
    file_compare_layout(m);
    const gchar *source = "// я\nint work() { return 1; }\nint work() { return 2; }\nwork(); $display(work);\n";
    for (guint side = 0; side < 2; side++)
    {
        m->sources[side] = g_strdup(source);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side])), source, -1);
    }
    const gchar *first = strstr(source, "int work");
    const gchar *second = strstr(first + 1, "int work");
    const gchar *call = strstr(source, "work();");
    gint call_offset = g_utf8_pointer_to_offset(source, call);
    symbol_test_add(m, 0, "Left::work", first - source, second - source);
    symbol_test_add(m, 1, "Right::work", second - source, call - source);
    diff_symbol_jump(m, 0, call_offset, FALSE);
    gchar *query = diff_symbol_query(m, 0);
    g_assert_cmpstr(query, ==, "work");
    g_free(query);
    diff_symbol_action(m, 0, 0);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, g_utf8_pointer_to_offset(source, first));
    g_assert_cmpuint(m->symbol_history->len, ==, 1);
    diff_symbol_action(m, 0, 2);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, call_offset);
    diff_symbol_jump(m, 1, call_offset, FALSE);
    diff_symbol_action(m, 1, 0);
    g_assert_cmpint(symbol_test_caret(m, 1), ==, g_utf8_pointer_to_offset(source, second));
    diff_symbol_action(m, 1, 2);
    g_assert_cmpint(symbol_test_caret(m, 1), ==, call_offset);
    symbol_test_add(m, 0, "Other::work", second - source, call - source);
    diff_symbol_jump(m, 0, call_offset, FALSE);
    g_idle_add(symbol_test_choose, NULL);
    diff_symbol_action(m, 0, 0);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, g_utf8_pointer_to_offset(source, second));
    const gchar *dollar = strstr(source, "$display");
    diff_symbol_jump(m, 0, g_utf8_pointer_to_offset(source, dollar + 3), FALSE);
    query = diff_symbol_query(m, 0);
    g_assert_cmpstr(query, ==, "$display");
    g_free(query);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[0]));
    GtkTextIter begin, end;
    gtk_text_buffer_get_iter_at_offset(buffer, &begin, call_offset);
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_select_range(buffer, &begin, &end);
    g_assert_null(diff_symbol_query(m, 0));
    g_assert_true(identifier_char('$'));
    clear_diff(m);
    g_assert_null(m->symbol_history);
    g_assert_null(m->symbols[0]);
    gtk_widget_destroy(m->window);
    flush();
    g_clear_pointer(&view_defaults, g_key_file_unref);
    g_print("File Compare symbols: local versions, UTF-8, explicit ambiguity, back history and dollar identifiers passed.\n");
}
