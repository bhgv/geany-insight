/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify per-definition change review using real comparison hunks and immutable source previews.
 * RU: Проверяет просмотр изменений определения на настоящих блоках сравнения и неизменных предпросмотрах. */
typedef struct { Merge *m; gint response; gboolean stale, close_parent; } ScopeResponse;

/* EN: Exercise the active scope dialog for response; assert local stepping/copy/preview, then accept, cancel or retire it.
 * RU: Проверяет открытый диалог области response: переходы, копирование и предпросмотр; принимает, отменяет либо закрывает его. */
static gboolean scope_response(gpointer data)
{
    ScopeResponse *response = data;
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *dialog = NULL;
    for (GList *it = windows; it; it = it->next)
        if (!g_strcmp0(gtk_widget_get_name(it->data), "insight-diff-scope-changes"))
            dialog = it->data;
    g_list_free(windows);
    g_assert_nonnull(dialog);
    GtkWidget *tree = named(dialog, "insight-diff-scope-list");
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 2);
    GtkWidget *previous = named(dialog, "insight-diff-scope-previous");
    GtkWidget *next = named(dialog, "insight-diff-scope-next");
    g_assert_false(gtk_widget_get_sensitive(previous));
    gtk_button_clicked(GTK_BUTTON(named(dialog, "insight-diff-scope-copy")));
    gchar *copied = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    g_assert_nonnull(strstr(copied, "int a = 1"));
    g_assert_nonnull(strstr(copied, "int a = 2"));
    g_free(copied);
    gtk_button_clicked(GTK_BUTTON(next));
    g_assert_false(gtk_widget_get_sensitive(next));
    g_assert_true(gtk_widget_get_sensitive(previous));
    GtkTreeIter row;
    guint local;
    g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), NULL, &row));
    gtk_tree_model_get(model, &row, 0, &local, -1);
    g_assert_cmpuint(local, ==, 1);
    for (guint side = 0; side < 2; side++)
    {
        GtkWidget *view = named(dialog, side ? "insight-diff-scope-right" : "insight-diff-scope-left");
        g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(view)));
        gchar *text = contents(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)));
        g_assert_cmpstr(text, ==, response->m->sources[side]);
        g_free(text);
    }
    if (response->close_parent)
        gtk_widget_destroy(response->m->window);
    else
    {
        if (response->stale)
            response->m->history_serial++;
        gtk_dialog_response(GTK_DIALOG(dialog), response->response);
    }
    return G_SOURCE_REMOVE;
}

/* EN: Verify scoped hunks exclude outside changes; preview/cancel/stale acceptance preserve Result and main selection.
 * RU: Проверяет исключение внешних блоков; предпросмотр/отмена/устаревшее подтверждение сохраняют Result и основной выбор. */
static void file_compare_symbol_changes_test(void)
{
    gchar *directory = g_dir_make_tmp("scope-changes-XXXXXX", NULL);
    gchar *left = g_build_filename(directory, "left.cpp", NULL);
    gchar *right = g_build_filename(directory, "right.cpp", NULL);
    const gchar *before = "int global = 1;\n\nint work() {\n int a = 1;\n\n return a + 1;\n}\n";
    const gchar *after = "int global = 9;\n\nint work() {\n int a = 2;\n\n return a + 2;\n}\n";
    g_assert_true(g_file_set_contents(left, before, -1, NULL));
    g_assert_true(g_file_set_contents(right, after, -1, NULL));
    Merge *m = merge_new(NULL);
    file_compare_layout(m);
    gtk_entry_set_text(GTK_ENTRY(m->paths[0]), left);
    gtk_entry_set_text(GTK_ENTRY(m->paths[1]), right);
    compare(NULL, m);
    wait_comparison(m);
    g_assert_cmpuint(m->diff.count, ==, 3);
    gsize begin = strstr(before, "int work") - before;
    symbol_test_add(m, 0, "work", begin, strlen(before));
    diff_symbol_jump(m, 0, begin, FALSE);
    gint original_selection = selected(m);
    ScopeResponse response = {m, GTK_RESPONSE_CANCEL, FALSE, FALSE};
    g_idle_add(scope_response, &response);
    diff_scope_current(m, 0);
    g_assert_cmpint(selected(m), ==, original_selection);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, begin);
    response.response = GTK_RESPONSE_ACCEPT;
    g_idle_add(scope_response, &response);
    diff_scope_current(m, 0);
    g_assert_cmpint(selected(m), ==, 2);
    assert_text(m, before);
    g_assert_cmpuint(m->symbol_history->len, ==, 1);
    diff_symbol_action(m, 0, 2);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, begin);
    response.stale = TRUE;
    g_idle_add(scope_response, &response);
    diff_scope_current(m, 0);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, begin);
    assert_text(m, before);
    response.stale = FALSE;
    response.close_parent = TRUE;
    gboolean gone = FALSE;
    g_object_weak_ref(G_OBJECT(m->window), finalized, &gone);
    g_idle_add(scope_response, &response);
    diff_scope_current(m, 0);
    flush();
    g_assert_true(gone);
    g_clear_pointer(&view_defaults, g_key_file_unref);
    g_unlink(left);
    g_unlink(right);
    g_rmdir(directory);
    g_free(left);
    g_free(right);
    g_free(directory);
    g_print("Definition changes: scoped rows, paired snapshots, copy, stepping, cancel/stale guards and parent close passed.\n");
}
