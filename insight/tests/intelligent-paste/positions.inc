/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Real Clips integration checks for shared paste selection policies.
 * RU: Проверки настоящих Clips для общих правил выделения после вставки. */
/* EN: Select the actual saved paste position value and assert its radio state.
 * RU: Выбирает настоящий сохраняемый режим value и проверяет переключатель. */
static void position(gint value)
{
    gchar *id = g_strdup_printf("insight-paste-position-%d", value);
    GtkWidget *item = named(window, id);
    g_assert_nonnull(item);
    g_free(id);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
    g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)));
}
/* EN: Assert exact anchor and caret byte offsets in the current document.
 * RU: Проверяет точные байтовые позиции anchor и caret текущего документа. */
static void selection_is(gint anchor, gint caret)
{
    g_assert_cmpint(SEND(doc->editor->sci, SCI_GETANCHOR, 0, 0), ==, anchor);
    g_assert_cmpint(sci_get_current_position(doc->editor->sci), ==, caret);
}
/* EN: Capture text into a real Clip and paste into the restored destination, preserving selection, EOL and read-only state.
 * RU: Сохраняет text настоящим Clip и вставляет в восстановленное назначение с прежними выделением, EOL и доступом записи. */
static void paste_clip(const gchar *text)
{
    ScintillaObject *sci = doc->editor->sci;
    gchar *destination = sci_get_contents(sci, sci_get_length(sci) + 2);
    gint anchor = SEND(sci, SCI_GETANCHOR, 0, 0), caret = sci_get_current_position(sci);
    gint eol = SEND(sci, SCI_GETEOLMODE, 0, 0);
    gboolean readonly = SEND(sci, SCI_GETREADONLY, 0, 0);
    SEND(sci, SCI_SETREADONLY, FALSE, 0);
    reset(text, 0);
    SEND(sci, SCI_SETSEL, 0, strlen(text));
    GtkWidget *query = named(window, "insight-clips-query");
    g_assert_nonnull(query);
    gtk_entry_set_text(GTK_ENTRY(query), "Postpastefixture");
    GtkWidget *copy = named(window, "insight-clip-copy");
    g_assert_nonnull(copy);
    gtk_button_clicked(GTK_BUTTON(copy));
    reset(destination, caret);
    g_free(destination);
    SEND(sci, SCI_SETSEL, anchor, caret);
    SEND(sci, SCI_SETEOLMODE, eol, 0);
    SEND(sci, SCI_SETREADONLY, readonly, 0);
    GtkWidget *view = named(window, "insight-clips");
    g_assert_nonnull(view);
    GtkTreePath *path = gtk_tree_path_new_from_indices(0, -1);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), path);
    gtk_tree_path_free(path);
    GtkWidget *button = named(window, "insight-clip-paste");
    g_assert_nonnull(button);
    gtk_button_clicked(GTK_BUTTON(button));
}
/* EN: Verify intelligent and plain Clip position policies, UTF-8, reverse ranges, one Undo and persistence; abort on mismatch.
 * RU: Проверяет позиции умных и обычных Clips, UTF-8, обратные диапазоны, один Undo и сохранение; ошибка прерывает тест. */
static void check_paste_position(void)
{
    /* The same synchronous insertion is shared by clipboard, Paste Line and Clips. */
    for (gint i = 0; i < 3; i++)
    {
        position(i);
        reset("  target();\n", 5);
        paste_clip("    Привет();\n");
        const gchar *insert = "  Привет();\n";
        text_is("  Привет();\n  target();\n");
        selection_is(i == 0 ? strlen(insert) : 0, i == 1 ? 0 : strlen(insert));
        undo();
        text_is("  target();\n");
        g_assert_false(SEND(doc->editor->sci, SCI_CANUNDO, 0, 0));
    }
    position(2);
    reset("  old();", 8);
    SEND(doc->editor->sci, SCI_SETEOLMODE, SC_EOL_CRLF, 0);
    paste_clip(" new();\n");
    text_is("  old();\r\n  new();\r\n");
    selection_is(10, strlen("  old();\r\n  new();\r\n"));
    gchar *selected = sci_get_selection_contents(doc->editor->sci);
    g_assert_cmpstr(selected, ==, "  new();\r\n");
    g_free(selected);
    undo();
    text_is("  old();");
    position(1);
    reset("  old();\n  rest();\n", 0);
    SEND(doc->editor->sci, SCI_SETSEL, 9, 0);
    paste_clip(" new();\n");
    text_is("  new();\n  rest();\n");
    selection_is(0, 0);
    undo();
    text_is("  old();\n  rest();\n");
    position(2);
    reset("  target();\n", 5);
    paste_clip("fragment");
    text_is("  tarfragmentget();\n");
    selection_is(5, 13);
    reset("  target();\n", 5);
    SEND(doc->editor->sci, SCI_SETREADONLY, TRUE, 0);
    paste_clip("line\n");
    text_is("  target();\n");
    SEND(doc->editor->sci, SCI_SETREADONLY, FALSE, 0);
    gchar *path = g_build_filename(profile, "insight-paste.conf", NULL);
    GKeyFile *key = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(key, path, 0, NULL));
    g_assert_cmpint(g_key_file_get_integer(key, "typing", "paste-position", NULL), ==, 2);
    g_assert_true(g_key_file_get_boolean(key, "typing", "intelligent-paste", NULL));
    g_free(path);
    g_key_file_unref(key);
    g_print("Post-paste position: three modes, UTF-8, CRLF, unterminated EOF separator, reverse replacement, single Undo, guards and persistence passed\n");
}
