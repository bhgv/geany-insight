/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise symbol-browser filtering, insertion boundaries, preview and stale-comparison protection.
 * RU: Проверяет фильтры списка символов, границы вставок, предпросмотр и защиту устаревшего сравнения. */
typedef struct { Merge *m; gboolean stale; } SymbolBrowserResponse;

/* EN: Filter the active browser for response's comparison; verify empty states and accept or invalidate the snapshot.
 * RU: Фильтрует открытый список сравнения response; проверяет пустую выдачу, принимает либо инвалидирует снимок. */
static gboolean symbol_browser_response(gpointer data)
{
    SymbolBrowserResponse *response = data;
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *dialog = NULL;
    for (GList *it = windows; it; it = it->next)
        if (!g_strcmp0(gtk_widget_get_name(it->data), "insight-diff-symbol-browser"))
            dialog = it->data;
    g_list_free(windows);
    g_assert_nonnull(dialog);
    GtkWidget *tree = named(dialog, "insight-diff-symbol-list");
    GtkWidget *entry = named(dialog, "insight-diff-symbol-filter");
    GtkWidget *changed = named(dialog, "insight-diff-symbol-changed");
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 2);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(changed), TRUE);
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 1);
    gtk_entry_set_text(GTK_ENTRY(entry), "MISSING");
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 0);
    g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT)));
    gtk_entry_set_text(GTK_ENTRY(entry), "wOrK");
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 1);
    GtkWidget *preview = named(dialog, "insight-diff-symbol-preview");
    gchar *text = contents(gtk_text_view_get_buffer(GTK_TEXT_VIEW(preview)));
    g_assert_cmpstr(text, ==, response->m->sources[0]);
    g_free(text);
    g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(preview)));
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model), 0, GTK_SORT_DESCENDING);
    if (response->stale)
        response->m->history_serial++;
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* EN: Verify changed-range filtering and preview navigation while leaving Result/history unchanged on stale acceptance.
 * RU: Проверяет фильтр изменённых диапазонов и навигацию с предпросмотром, сохраняя Result/историю при устаревшем выборе. */
static void file_compare_symbol_browser_test(void)
{
    Merge *m = merge_new(NULL);
    file_compare_layout(m);
    const gchar *source = "int quiet() {}\nint work() {}\n";
    m->sources[0] = g_strdup(source);
    m->sources[1] = g_strdup(source);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[1])), source, -1);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[0])), source, -1);
    gsize boundary = strstr(source, "int work") - source;
    symbol_test_add(m, 0, "quiet", 0, boundary);
    symbol_test_add(m, 0, "work", boundary, strlen(source));
    m->diff.count = 1;
    m->diff.hunks = g_new0(InsightDiffHunk, 1);
    m->diff.hunks[0].a_begin = boundary;
    m->diff.hunks[0].a_end = boundary;
    g_assert_false(diff_symbol_changed(m, 0, 0, boundary));
    g_assert_true(diff_symbol_changed(m, 0, boundary, strlen(source)));
    diff_symbol_jump(m, 0, 0, FALSE);
    SymbolBrowserResponse response = {m, FALSE};
    g_idle_add(symbol_browser_response, &response);
    diff_symbol_browser(m, 0);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, boundary);
    g_assert_cmpuint(m->symbol_history->len, ==, 1);
    diff_symbol_action(m, 0, 2);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, 0);
    diff_symbol_action(m, 0, 4);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, boundary);
    diff_symbol_action(m, 0, 2);
    diff_symbol_jump(m, 0, 1, TRUE);
    g_assert_null(m->symbol_future);
    response.stale = TRUE;
    g_idle_add(symbol_browser_response, &response);
    diff_symbol_browser(m, 0);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, 1);
    assert_text(m, "");
    /* Synthetic hunks have no Result marks; release them before the normal close path. */
    insight_diff_clear(&m->diff);
    gtk_widget_destroy(m->window);
    flush();
    g_clear_pointer(&view_defaults, g_key_file_unref);
    g_print("File Compare browser: changed insertion boundaries, name filtering, empty state, preview, forward history and stale guard passed.\n");
}
