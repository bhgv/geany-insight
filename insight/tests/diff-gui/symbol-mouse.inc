/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise real GTK source mouse events together with active merge blocks and snapshot navigation.
 * RU: Проверяет настоящие события мыши GTK вместе с выбором блока сведения и навигацией по снимку. */

/* EN: Send type/button/modifiers at character offset in view through GTK, with a real pointer device; return whether consumed.
 * RU: Передаёт type/button/modifiers у символьного offset в view через GTK с настоящим устройством; возвращает обработку. */
static gboolean source_mouse_event(GtkWidget *view, gint offset, GdkEventType type, guint button, guint modifiers)
{
    GtkTextIter at;
    gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), &at, offset);
    GdkRectangle rect;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view), &at, &rect);
    gint x, y;
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT,
        rect.x + MAX(1, rect.width / 4), rect.y + rect.height / 2, &x, &y);
    GdkEvent *event = gdk_event_new(type);
    event->button.window = g_object_ref(gtk_text_view_get_window(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT));
    event->button.send_event = TRUE;
    event->button.time = GDK_CURRENT_TIME;
    event->button.button = button;
    event->button.state = modifiers;
    event->button.x = x;
    event->button.y = y;
    gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(view))));
    gboolean handled = gtk_widget_event(view, event);
    gdk_event_free(event);
    return handled;
}

/* EN: Send a primary press/release at offset in view, optionally with modifiers; normal GTK handlers remain active.
 * RU: Передаёт левое нажатие/отпускание у offset в view с modifiers; штатные обработчики GTK остаются активны. */
static void source_mouse_click(GtkWidget *view, gint offset, guint modifiers)
{
    source_mouse_event(view, offset, GDK_BUTTON_PRESS, 1, modifiers);
    source_mouse_event(view, offset, GDK_BUTTON_RELEASE, 1, modifiers);
}

/* EN: Invoke the context gesture at offset in m's left source without opening GTK's popup; return the callback result.
 * RU: Вызывает контекстный жест у offset левого исходника m без открытия меню GTK; возвращает результат обработчика. */
static gboolean source_mouse_context(Merge *m, gint offset)
{
    GtkWidget *view = m->views[0];
    GtkTextIter at;
    gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), &at, offset);
    GdkRectangle rect;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view), &at, &rect);
    gint x, y;
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT,
        rect.x + MAX(1, rect.width / 4), rect.y + rect.height / 2, &x, &y);
    GdkEventButton event = {0};
    event.type = GDK_BUTTON_PRESS;
    event.button = 3;
    event.window = gtk_text_view_get_window(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT);
    event.x = x;
    event.y = y;
    return diff_symbol_mouse(view, &event, m);
}

/* EN: Verify cursor placement, double/shift selection, click definitions and literal occurrences without editing Result.
 * RU: Проверяет курсор, двойное/Shift-выделение, определения и текстовые вхождения без изменения Result. */
static void file_compare_symbol_mouse_test(void)
{
    const gchar *before = "// я\nint work() {\n return 1;\n}\n\nint caller() { return work() + 1; }\n";
    const gchar *after = "// я\nint work() {\n return 2;\n}\n\nint caller() { return work() + 2; }\n";
    gchar *directory = g_dir_make_tmp("symbol-mouse-XXXXXX", NULL);
    gchar *left = g_build_filename(directory, "left.cpp", NULL);
    gchar *right = g_build_filename(directory, "right.cpp", NULL);
    g_assert_true(g_file_set_contents(left, before, -1, NULL));
    g_assert_true(g_file_set_contents(right, after, -1, NULL));
    Merge *m = merge_new(NULL);
    file_compare_layout(m);
    gtk_entry_set_text(GTK_ENTRY(m->paths[0]), left);
    gtk_entry_set_text(GTK_ENTRY(m->paths[1]), right);
    compare(NULL, m);
    wait_comparison(m);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mode), TRUE);
    flush();
    const gchar *definition = strstr(before, "int work");
    const gchar *caller = strstr(before, "int caller");
    const gchar *call = strstr(caller, "work()");
    gint call_offset = g_utf8_pointer_to_offset(before, call);
    gint definition_offset = g_utf8_pointer_to_offset(before, definition);
    for (guint side = 0; side < 2; side++)
        symbol_test_add(m, side, "work", definition - before, caller - before);
    guint history = m->history->len;
    source_mouse_click(m->views[0], call_offset + 2, 0);
    g_assert_cmpint(selected(m), ==, 1);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, call_offset + 2);
    g_assert_true(gtk_widget_get_sensitive(m->take[1]));
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[0]));
    g_assert_false(gtk_text_buffer_get_has_selection(buffer));
    source_mouse_click(m->views[0], call_offset, 0);
    source_mouse_click(m->views[0], call_offset + 4, GDK_SHIFT_MASK);
    GtkTextIter begin, end;
    g_assert_true(gtk_text_buffer_get_selection_bounds(buffer, &begin, &end));
    gchar *word = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
    g_assert_cmpstr(word, ==, "work");
    g_free(word);
    source_mouse_click(m->views[0], call_offset + 1, 0);
    source_mouse_event(m->views[0], call_offset + 1, GDK_2BUTTON_PRESS, 1, 0);
    source_mouse_event(m->views[0], call_offset + 1, GDK_BUTTON_RELEASE, 1, 0);
    g_assert_true(gtk_text_buffer_get_selection_bounds(buffer, &begin, &end));
    word = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
    g_assert_cmpstr(word, ==, "work");
    g_free(word);
    output_key(m->views[0], GDK_KEY_F12, 0);
    g_assert_cmpint(symbol_test_caret(m, 0), ==, definition_offset);
    diff_symbol_action(m, 0, 2);
    source_mouse_click(m->views[1], call_offset + 1, GDK_CONTROL_MASK);
    g_assert_cmpint(symbol_test_caret(m, 1), ==, definition_offset);
    output_key(m->views[1], GDK_KEY_Left, GDK_CONTROL_MASK | GDK_MOD1_MASK);
    g_assert_cmpint(symbol_test_caret(m, 1), ==, call_offset + 1);
    SearchResponse response = {m, 2, 1, GTK_RESPONSE_CANCEL, FALSE};
    g_idle_add(search_response, &response);
    source_mouse_click(m->views[0], call_offset + 1, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(m->find_entry)), ==, "work");
    guint jumps = m->symbol_history ? m->symbol_history->len : 0;
    source_mouse_click(m->views[0], call_offset + 4, GDK_CONTROL_MASK);
    g_assert_cmpuint(m->symbol_history ? m->symbol_history->len : 0, ==, jumps);
    gint literal = g_utf8_pointer_to_offset(before, strstr(caller, "+ 1") + 2);
    source_mouse_click(m->views[0], literal, GDK_CONTROL_MASK);
    g_assert_cmpuint(m->symbol_history ? m->symbol_history->len : 0, ==, jumps);
    m->busy = TRUE;
    source_mouse_click(m->views[0], call_offset + 1, GDK_CONTROL_MASK);
    m->busy = FALSE;
    g_assert_cmpuint(m->symbol_history ? m->symbol_history->len : 0, ==, jumps);
    gtk_text_buffer_get_iter_at_offset(buffer, &begin, call_offset);
    gtk_text_buffer_get_iter_at_offset(buffer, &end, call_offset + 4);
    gtk_text_buffer_select_range(buffer, &begin, &end);
    g_assert_false(source_mouse_context(m, call_offset + 1));
    g_assert_true(gtk_text_buffer_get_selection_bounds(buffer, &begin, &end));
    g_assert_cmpint(gtk_text_iter_get_offset(&begin), ==, call_offset);
    g_assert_cmpint(gtk_text_iter_get_offset(&end), ==, call_offset + 4);
    g_assert_false(source_mouse_context(m, definition_offset + 5));
    g_assert_cmpint(symbol_test_caret(m, 0), ==, definition_offset + 5);
    g_assert_cmpuint(m->history->len, ==, history);
    assert_text(m, before);
    take(m->take[1], m);
    gchar *result_text = contents(result(m));
    g_assert_nonnull(strstr(result_text, "return work() + 2"));
    g_assert_nonnull(strstr(result_text, "return 1;"));
    g_free(result_text);
    undo(NULL, m);
    assert_text(m, before);
    gtk_widget_destroy(m->window);
    flush();
    g_clear_pointer(&view_defaults, g_key_file_unref);
    g_unlink(left);
    g_unlink(right);
    g_rmdir(directory);
    g_free(left);
    g_free(right);
    g_free(directory);
    g_print("File Compare mouse: real GTK caret, Shift/double selection, local F12/Ctrl-click, back, occurrences, active merge block and Undo passed.\n");
}
