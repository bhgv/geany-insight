/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise the real instruction page without changing the source-level scenario's execution point.
 * RU: Проверяет настоящую страницу инструкций без изменения позиции исходного сценария. */

/* EN: Advance phases 70..72 in window's production page and return to value inspection in tabs.
 * RU: Продвигает фазы 70..72 настоящей страницы window и возвращает исследование значений в tabs. */
static gboolean native_assembly_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    GtkWidget *entry = find(window, "insight-gdb-instruction-address");
    if (!entry || !gtk_widget_is_sensitive(entry))
        return G_SOURCE_CONTINUE;
    if (phase == 70)
    {
        GtkWidget *page = values_page(entry, tabs);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), gtk_notebook_page_num(GTK_NOTEBOOK(tabs), page));
        gtk_entry_set_text(GTK_ENTRY(entry), "main");
        if (click(window, "Read instructions"))
            phase++;
    }
    else if (phase == 71)
    {
        GtkWidget *view = find(window, "insight-gdb-instructions");
        GtkTextIter start, end;
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        gboolean valid = strstr(text, "main") && strstr(text, "0x");
        g_free(text);
        if (valid)
        {
            gtk_entry_set_text(GTK_ENTRY(entry), "missing_instruction_symbol");
            if (click(window, "Read instructions"))
                phase++;
        }
    }
    else if (phase == 72 && gtk_widget_is_sensitive(entry))
    {
        GtkWidget *view = find(window, "insight-gdb-instructions");
        if (gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view))))
            return G_SOURCE_CONTINUE;
        gtk_entry_set_text(GTK_ENTRY(entry), "$pc");
        g_print("C/C++ GTK: bounded disassembly, function expression and error recovery passed.\n");
        phase = 16;
    }
    return G_SOURCE_CONTINUE;
}
