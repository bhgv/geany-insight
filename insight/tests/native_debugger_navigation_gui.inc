/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise confirmed code history, explicit selection and return-to-PC with production GTK widgets and GDB.
 * RU: Проверяет историю кода, явное выделение адреса и возврат к PC на настоящих GTK-виджетах и GDB. */

/* EN: Return window's first displayed code address without padding; NULL means the snapshot contains no instruction.
 * RU: Возвращает первый показанный адрес кода window без ведущих нулей; NULL означает отсутствие инструкции. */
static gchar *navigation_gui_address(GtkWidget *window)
{
    gchar *text = native_assembly_gui_snapshot(window);
    const gchar *start = strstr(text, "0x");
    gchar *address = start ? g_strdup_printf("0x%" G_GINT64_MODIFIER "x", g_ascii_strtoull(start, NULL, 16)) : NULL;
    g_free(text);
    return address;
}

/* EN: Run phases 93..99 for window and tabs without advancing the inferior; resume shared value tests afterward.
 * RU: Выполняет фазы 93..99 для window и tabs без исполнения программы; затем возвращается к общим проверкам значений. */
static gboolean navigation_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    (void)tabs;
    GtkWidget *entry = find(window, "insight-gdb-instruction-address");
    if (!entry || !gtk_widget_is_sensitive(entry))
        return G_SOURCE_CONTINUE;
    static gchar *previous, *main_address;
    gchar *address = navigation_gui_address(window);
    const gchar *expression = gtk_entry_get_text(GTK_ENTRY(entry));
    if (phase == 93)
    {
        previous = g_strdup(address);
        gtk_entry_set_text(GTK_ENTRY(entry), "main");
        if (click(window, "Read instructions"))
            phase = 94;
    }
    else if (phase == 94 && address && g_strcmp0(address, previous))
    {
        main_address = g_strdup(address);
        GdkEventKey key = {0};
        key.type = GDK_KEY_PRESS;
        key.keyval = GDK_KEY_Left;
        key.state = GDK_MOD1_MASK;
        gboolean handled = FALSE;
        g_signal_emit_by_name(find(window, "insight-gdb-instructions"), "key-press-event", &key, &handled);
        if (handled)
            phase = 95;
    }
    else if (phase == 95 && !g_strcmp0(address, previous) && !g_strcmp0(expression, previous))
    {
        if (click(window, "Code history forward"))
            phase = 96;
    }
    else if (phase == 96 && !g_strcmp0(address, main_address) && !g_strcmp0(expression, main_address))
    {
        GtkWidget *view = find(window, "insight-gdb-instructions");
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        gchar *text = native_assembly_gui_snapshot(window);
        const gchar *marker = strstr(text, "▶ 0x");
        g_assert_nonnull(marker);
        const gchar *start = marker + strlen("▶ ");
        const gchar *end = start + 2;
        while (g_ascii_isxdigit(*end))
            end++;
        GtkTextIter first, last;
        gtk_text_buffer_get_iter_at_offset(buffer, &first, g_utf8_pointer_to_offset(text, start));
        gtk_text_buffer_get_iter_at_offset(buffer, &last, g_utf8_pointer_to_offset(text, end));
        gtk_text_buffer_select_range(buffer, &first, &last);
        g_free(text);
        if (click(window, "Open selected address as code"))
            phase = 97;
    }
    else if (phase == 97 && !g_strcmp0(address, previous) && !g_strcmp0(expression, previous))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(find(window, "insight-gdb-instructions")));
        GtkTextIter start;
        gtk_text_buffer_get_start_iter(buffer, &start);
        gtk_text_buffer_place_cursor(buffer, &start);
        g_assert_true(click(window, "Open selected address as code"));
        g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(find(window, "insight-gdb-instruction-status"))), ==,
            "Select one hexadecimal address to open as code.");
        if (click(window, "Code history back"))
            phase = 98;
    }
    else if (phase == 98 && !g_strcmp0(address, main_address))
    {
        if (click(window, "Return to program counter"))
            phase = 99;
    }
    else if (phase == 99 && !g_strcmp0(address, previous) && !g_strcmp0(expression, "$pc"))
    {
        g_assert_true(has_local(window, "marker", "41"));
        g_print("C/C++ GTK: confirmed code Back/Forward, selected address, invalid selection and return-to-PC passed.\n");
        g_clear_pointer(&previous, g_free);
        g_clear_pointer(&main_address, g_free);
        phase = 16;
    }
    g_free(address);
    return G_SOURCE_CONTINUE;
}
