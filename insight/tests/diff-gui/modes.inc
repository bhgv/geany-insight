/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify File Compare mode protects hidden Result and retains merge history while switching layouts.
 * RU: Проверяет защиту скрытого Result и сохранение истории при переключениях File Compare. */

/* EN: Exercise production mode controls, side selection, hunk acceptance and keyboard guards on temporary files.
 * Assertions fail the fixture; all temporary files are removed after the comparison window closes.
 * RU: Проверяет режимы, выбор стороны, принятие блока и клавиатуру на временных файлах.
 * Неудачные проверки завершают фикстуру; временные файлы удаляются после закрытия окна. */
static void file_compare_modes_test(void)
{
    GError *error = NULL;
    gchar *directory = g_dir_make_tmp("file-compare-mode-XXXXXX", &error);
    g_assert_no_error(error);
    gchar *left = g_build_filename(directory, "left.txt", NULL);
    gchar *right = g_build_filename(directory, "right.txt", NULL);
    g_assert_true(g_file_set_contents(left, "old\n", -1, &error));
    g_assert_no_error(error);
    g_assert_true(g_file_set_contents(right, "new\n", -1, &error));
    g_assert_no_error(error);
    Merge *m = merge_new(NULL);
    file_compare_layout(m);
    g_assert_true(m->browse);
    g_assert_false(gtk_widget_get_visible(m->result_panel));
    g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(m->outer_pane)), ==, GTK_ORIENTATION_HORIZONTAL);
    gtk_entry_set_text(GTK_ENTRY(m->paths[0]), left);
    gtk_entry_set_text(GTK_ENTRY(m->paths[1]), right);
    compare(NULL, m);
    wait_comparison(m);
    navigate(m, 1);
    g_assert_false(gtk_widget_is_sensitive(m->take[1]));
    g_assert_false(gtk_widget_is_sensitive(m->save_button));
    g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(m->views[2])));
    GtkTreeIter row;
    g_assert_true(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(m->list), &row));
    gchar *label = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(m->list), &row, 5, &label, -1);
    g_assert_cmpstr(label, ==, "No symbol context");
    g_free(label);
    gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side), 2);
    g_assert_false(m->browse);
    g_assert_true(gtk_widget_get_visible(m->result_panel));
    take(m->take[1], m);
    assert_text(m, "new\n");
    guint position = m->position;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mode), FALSE);
    assert_text(m, "new\n");
    g_assert_cmpuint(m->position, ==, position);
    GdkEventKey event = {0};
    event.state = GDK_CONTROL_MASK;
    event.keyval = GDK_KEY_z;
    g_assert_true(key(m->window, &event, m));
    assert_text(m, "new\n");
    g_assert_cmpuint(m->position, ==, position);
    gtk_widget_grab_focus(m->views[0]);
    event.state = 0;
    event.keyval = GDK_KEY_F6;
    g_assert_true(key(m->window, &event, m));
    g_assert_cmpint(focused_pane(m), ==, 1);
    g_assert_true(key(m->window, &event, m));
    g_assert_cmpint(focused_pane(m), ==, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mode), TRUE);
    g_assert_true(gtk_widget_get_visible(m->result_panel));
    assert_text(m, "new\n");
    undo(NULL, m);
    assert_text(m, "old\n");
    gtk_widget_destroy(m->window);
    flush();
    g_clear_pointer(&view_defaults, g_key_file_unref);
    g_unlink(left);
    g_unlink(right);
    g_rmdir(directory);
    g_free(left);
    g_free(right);
    g_free(directory);
    g_print("File Compare GTK: read-only mode, explicit merge, retained Result/Undo and source-only F6 passed.\n");
}
