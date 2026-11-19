/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Resolve source mouse gestures against the exact comparison snapshot without suppressing normal GTK selection.
 * RU: Разрешает жесты мыши по точному снимку сравнения, не подавляя обычное выделение GTK. */

/* EN: Return whether at belongs to a bounded identifier beginning with a non-digit; do not move the caller's iterator.
 * RU: Возвращает принадлежность at ограниченному идентификатору с первой нецифрой; не меняет итератор вызывающего. */
static gboolean diff_symbol_mouse_identifier(GtkTextIter at)
{
    if (!identifier_char(gtk_text_iter_get_char(&at)))
        return FALSE;
    for (guint count = 0; count < 4096; count++)
    {
        GtkTextIter previous = at;
        if (!gtk_text_iter_backward_char(&previous) || !identifier_char(gtk_text_iter_get_char(&previous)))
            return !g_unichar_isdigit(gtk_text_iter_get_char(&at));
        at = previous;
    }
    return FALSE;
}

/* EN: Handle Ctrl+primary definition navigation or Ctrl+Shift+primary occurrences in source view for m in data.
 * Right clicks preserve a clicked selection or move the caret to its identifier. Reject non-text windows,
 * whitespace and unrelated modifiers. Return TRUE only when a navigation gesture was consumed.
 * RU: Обрабатывает Ctrl+левый щелчок к определению либо Ctrl+Shift+левый щелчок к вхождениям в view для m из data.
 * Правый щелчок сохраняет выделение под мышью либо ставит курсор на идентификатор. Пропускает поля,
 * пробелы и посторонние модификаторы. TRUE только при обработанном жесте навигации. */
static gboolean diff_symbol_mouse(GtkWidget *view, GdkEventButton *event, gpointer data)
{
    Merge *m = data;
    guint side = view == m->views[1];
    if (m->busy || m->changing || !m->sources[side] || event->type != GDK_BUTTON_PRESS ||
        gtk_text_view_get_window_type(GTK_TEXT_VIEW(view), event->window) != GTK_TEXT_WINDOW_TEXT)
        return FALSE;
    guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    gboolean navigation = event->button == 1 &&
        (modifiers == GDK_CONTROL_MASK || modifiers == (GDK_CONTROL_MASK | GDK_SHIFT_MASK));
    gboolean context = event->button == 3 && !modifiers;
    if (!navigation && !context)
        return FALSE;
    gint x, y, trailing;
    GtkTextIter at;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT,
        (gint)event->x, (gint)event->y, &x, &y);
    if (!gtk_text_view_get_iter_at_position(GTK_TEXT_VIEW(view), &at, &trailing, x, y) ||
        !diff_symbol_mouse_identifier(at))
        return FALSE;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter begin, end;
    if (context && gtk_text_buffer_get_selection_bounds(buffer, &begin, &end) &&
        gtk_text_iter_compare(&begin, &at) <= 0 && gtk_text_iter_compare(&at, &end) < 0)
        return FALSE;
    gtk_text_buffer_place_cursor(buffer, &at);
    gtk_widget_grab_focus(view);
    if (navigation)
        diff_symbol_action(m, side, !!(modifiers & GDK_SHIFT_MASK));
    return navigation;
}
