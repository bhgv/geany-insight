/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify cached opcode visibility and captured instruction clipboard data with real GTK and GDB.
 * RU: Проверяет видимость сохранённых байтов и копирование строки инструкции с настоящими GTK и GDB. */

/* EN: Check window's opcode controls during phase 100 without executing target; tabs is unused, then resume value tests.
 * RU: Проверяет байты в окне window на фазе 100 без исполнения программы; tabs не используется, затем продолжает проверки значений. */
static gboolean opcode_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    (void)tabs;
    GtkWidget *entry = find(window, "insight-gdb-instruction-address");
    if (!entry || !gtk_widget_is_sensitive(entry))
        return G_SOURCE_CONTINUE;
    GtkWidget *toggle = find(window, "insight-gdb-show-bytes");
    GtkWidget *view = find(window, "insight-gdb-instructions");
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), TRUE);
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_place_cursor(buffer, &start);
    gchar *text = native_assembly_gui_snapshot(window);
    const gchar *first = strchr(text, '['), *last = first ? strchr(first, ']') : NULL;
    g_assert_nonnull(first);
    g_assert_nonnull(last);
    gchar *bytes = g_strndup(first + 1, last - first - 1);
    g_assert_cmpstr(bytes, !=, "??");
    g_assert_true(instruction_menu_choose(window, "Copy instruction bytes", TRUE));
    gchar *copied = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    g_assert_cmpstr(copied, ==, bytes);
    g_free(copied);
    gtk_text_buffer_place_cursor(buffer, &start);
    g_assert_true(instruction_menu_choose(window, "Copy instruction line", FALSE));
    copied = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    const gchar *address = strstr(text, "0x");
    g_assert_nonnull(address);
    const gchar *end = strchr(address, '\n');
    gchar *line = end ? g_strndup(address, end - address) : g_strdup(address);
    g_assert_cmpstr(copied, ==, line);
    g_free(copied);
    gchar *group = g_strdup_printf("[%s]", bytes);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), FALSE);
    gchar *plain = native_assembly_gui_snapshot(window);
    g_assert_null(strstr(plain, group));
    g_assert_true(has_local(window, "marker", "41"));
    g_print("C/C++ GTK: cached opcode toggle, captured bytes/line clipboard and unchanged inferior passed.\n");
    g_free(plain);
    g_free(group);
    g_free(line);
    g_free(bytes);
    g_free(text);
    phase = 101;
    return G_SOURCE_CONTINUE;
}
