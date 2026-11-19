/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify exact cross-version definition matching and two-pane navigation history.
 * RU: Проверяет точное сопоставление определений версий и историю переходов между панелями. */

/* EN: Invalidate comparison m in data before accepting an ambiguous target; the stale jump must be rejected.
 * RU: Делает сравнение m из data устаревшим перед выбором неоднозначной цели; переход должен быть отклонён. */
static gboolean counterpart_stale(gpointer data)
{
    Merge *m = data;
    m->history_serial++;
    return symbol_test_choose(NULL);
}

/* EN: Exercise shifted UTF-8 definitions, scoped names, cross-pane Back/Forward, duplicate choice and missing/stale targets.
 * RU: Проверяет сдвинутые UTF-8 определения, полные имена, возврат между панелями, дубликаты и отсутствующие/устаревшие цели. */
static void file_compare_symbol_counterpart_test(void)
{
    Merge *m = merge_new(NULL);
    file_compare_layout(m);
    const gchar *left = "// я\nint work() { return 1; }\n";
    const gchar *right = "// extra\n// я\nint work() { return 2; }\nint work() { return 3; }\n";
    const gchar *sources[] = {left, right};
    for (guint side = 0; side < 2; side++)
    {
        m->sources[side] = g_strdup(sources[side]);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side])), sources[side], -1);
    }
    const gchar *first = strstr(right, "int work");
    const gchar *second = strstr(first + 1, "int work");
    symbol_test_add(m, 0, "Box::work", strstr(left, "int work") - left, strlen(left));
    symbol_test_add(m, 1, "Other::work", first - right, second - right);
    symbol_test_add(m, 1, "Box::work", second - right, strlen(right));
    gint origin = g_utf8_pointer_to_offset(left, strstr(left, "return"));
    gint target = g_utf8_pointer_to_offset(right, second);
    guint history = m->history->len;
    gchar *initial = contents(result(m));
    diff_symbol_jump(m, 0, origin, FALSE);
    output_key(m->views[0], GDK_KEY_F12, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
    g_assert_cmpint(symbol_test_caret(m, 1), ==, target);
    g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(m->line_side)), ==, 1);
    g_assert_cmpuint(m->symbol_history->len, ==, 1);
    diff_symbol_action(m, 1, 2);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, origin);
    g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(m->line_side)), ==, 0);
    diff_symbol_action(m, 0, 4);
    g_assert_cmpint(symbol_test_caret(m, 1), ==, target);
    diff_symbol_action(m, 1, 6);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, g_utf8_pointer_to_offset(left, strstr(left, "int work")));
    /* Exact names exclude Other::work; duplicate full names require a choice. */
    symbol_test_add(m, 1, "Box::work", first - right, second - right);
    diff_symbol_jump(m, 0, origin, FALSE);
    g_idle_add(symbol_test_choose, NULL);
    diff_symbol_action(m, 0, 6);
    g_assert_cmpint(symbol_test_caret(m, 1), ==, g_utf8_pointer_to_offset(right, first));
    diff_symbol_jump(m, 0, origin, FALSE);
    guint jumps = m->symbol_history->len;
    gint previous = symbol_test_caret(m, 1);
    g_idle_add(counterpart_stale, m);
    diff_symbol_action(m, 0, 6);
    g_assert_cmpint(symbol_test_caret(m, 1), ==, previous);
    g_assert_cmpuint(m->symbol_history->len, ==, jumps);
    g_ptr_array_set_size(m->symbols[1], 0);
    symbol_test_add(m, 1, "Other::work", first - right, second - right);
    diff_symbol_action(m, 0, 6);
    g_assert_cmpuint(m->symbol_history->len, ==, jumps);
    diff_symbol_jump(m, 0, 0, FALSE);
    diff_symbol_action(m, 0, 6);
    g_assert_cmpuint(m->symbol_history->len, ==, jumps);
    diff_symbol_jump(m, 0, origin, FALSE);
    m->busy = TRUE;
    diff_symbol_action(m, 0, 6);
    m->busy = FALSE;
    g_assert_cmpuint(m->symbol_history->len, ==, jumps);
    g_assert_cmpuint(m->history->len, ==, history);
    assert_text(m, initial);
    g_free(initial);
    gtk_widget_destroy(m->window);
    flush();
    g_clear_pointer(&view_defaults, g_key_file_unref);
    g_print("File Compare counterpart: exact scopes, UTF-8 shifts, both directions, Back/Forward, duplicate choice, stale/missing guards and unchanged Result passed.\n");
}
