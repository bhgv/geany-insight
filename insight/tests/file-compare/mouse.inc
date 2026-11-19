/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify mouse navigation through real GTK events in the installed comparison window.
 * RU: Проверяет навигацию мышью настоящими событиями GTK в установленном окне сравнения. */

/* EN: Click character offset of view with modifiers, using its text window and actual pointer device.
 * RU: Щёлкает символьный offset в view с modifiers через текстовое окно и настоящее устройство указателя. */
static void native_source_click(GtkWidget *view, gint offset, guint modifiers)
{
    GtkTextIter at;
    gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), &at, offset);
    GdkRectangle rect;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view), &at, &rect);
    gint x, y;
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT,
        rect.x + MAX(1, rect.width / 4), rect.y + rect.height / 2, &x, &y);
    for (guint release = 0; release < 2; release++)
    {
        GdkEvent *event = gdk_event_new(release ? GDK_BUTTON_RELEASE : GDK_BUTTON_PRESS);
        event->button.window = g_object_ref(gtk_text_view_get_window(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT));
        event->button.send_event = TRUE;
        event->button.time = GDK_CURRENT_TIME;
        event->button.button = 1;
        event->button.state = modifiers;
        event->button.x = x;
        event->button.y = y;
        gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(view))));
        gtk_main_do_event(event);
        gdk_event_free(event);
    }
}

/* EN: Verify a normal source click, F12 and Ctrl-click against each captured C++ version in window, retaining Result bytes.
 * RU: Проверяет обычный щелчок, F12 и Ctrl-щелчок по каждому снимку C++ в window, сохраняя байты Result. */
static void native_compare_mouse(GtkWidget *window)
{
    for (guint side = 0; side < 2; side++)
    {
        GtkWidget *view = find(window, side ? "insight-diff-right" : "insight-diff-left");
        gchar *source = text(view);
        const gchar *call = strstr(source, side ? "renamed_alpha(9)" : "alpha(7)");
        g_assert_nonnull(call);
        gint offset = g_utf8_pointer_to_offset(source, call) + 2;
        native_source_click(view, offset, side ? GDK_CONTROL_MASK : 0);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_mark(buffer, &at, gtk_text_buffer_get_insert(buffer));
        if (!side)
        {
            g_assert_cmpint(gtk_text_iter_get_offset(&at), ==, offset);
            g_assert_false(gtk_text_buffer_get_has_selection(buffer));
            GdkEventKey key = {0};
            key.keyval = GDK_KEY_F12;
            gboolean handled = FALSE;
            g_signal_emit_by_name(view, "key-press-event", &key, &handled);
            g_assert_true(handled);
        }
        gtk_text_buffer_get_iter_at_mark(buffer, &at, gtk_text_buffer_get_insert(buffer));
        g_assert_cmpint(gtk_text_iter_get_line(&at), ==, 2);
        click(window, side ? "insight-diff-symbol-1-2" : "insight-diff-symbol-0-2");
        gtk_text_buffer_get_iter_at_mark(buffer, &at, gtk_text_buffer_get_insert(buffer));
        g_assert_cmpint(gtk_text_iter_get_offset(&at), ==, offset);
        g_free(source);
    }
    gchar *result = text(find(window, "insight-diff-result"));
    g_assert_cmpstr(result, ==, left_code);
    g_free(result);
}
