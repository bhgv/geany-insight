/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Parse captured command output using explicit or Run-time diagnostic options and expose pattern validation.
 * RU: Разбирает захваченный вывод по явным либо зафиксированным настройкам и проверяет шаблоны. */

/* EN: Parse v's output snapshot using regex or built-ins; manual forces the diagnostics tab, auto reveals only results/errors.
 * Resolve paths against launch_directory, stop at 1000 records or first regex error, never open or modify sources.
 * RU: Разбирает снимок вывода v через regex либо встроенные форматы; manual открывает вкладку, авто — лишь при результате/ошибке.
 * Пути относятся к launch_directory; предел 1000 записей или первая ошибка regex; исходники не открываются и не меняются. */
static void parse_links_using(CommandView *v, GRegex *regex, gboolean manual)
{
    if (v->closed)
        return;
    if (!v->launch_directory)
    {
        note(v, "Run a command before parsing source links");
        return;
    }
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->output));
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_offset(buffer, &start, MIN(v->output_start, gtk_text_buffer_get_char_count(buffer)));
    gtk_text_buffer_get_end_iter(buffer, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gchar **lines = g_strsplit(text, "\n", -1);
    g_free(text);
    stop_scroll(v->links_view);
    gtk_list_store_clear(v->links);
    guint count = 0;
    GError *error = NULL;
    for (guint i = 0; lines[i] && count < 1000 && !error; i++)
    {
        InsightCustomLink *link = insight_custom_source_link_pattern(lines[i], v->launch_directory, regex, &error);
        if (!link)
            continue;
        GtkTreeIter row;
        gtk_list_store_append(v->links, &row);
        gtk_list_store_set(v->links, &row, LINK_FILE, link->path, LINK_LINE, link->line,
            LINK_COLUMN, link->column, LINK_MESSAGE, link->message, -1);
        insight_custom_link_free(link);
        count++;
    }
    g_strfreev(lines);
    link_count(v);
    if (count)
    {
        GtkTreePath *first = gtk_tree_path_new_from_indices(0, -1);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(v->links_view), first, NULL, FALSE);
        gtk_tree_path_free(first);
    }
    if (error)
        note(v, error->message);
    if (manual || count || error)
        insight_compare_ui_reveal(v->links_view);
    g_clear_error(&error);
}

/* EN: Explicit Parse uses current editable pattern with the retained launch directory; invalid regex leaves old rows intact.
 * button is unused, data owns the current command view.
 * RU: Явный Parse применяет текущий шаблон с прежним каталогом запуска; неверный regex сохраняет старые строки.
 * button не используется, data содержит текущее окно команды. */
static void parse_links(GtkWidget *button, gpointer data)
{
    (void)button;
    CommandView *v = data;
    GError *error = NULL;
    GRegex *regex = insight_custom_diagnostic_regex(gtk_entry_get_text(GTK_ENTRY(v->diagnostic_pattern)), &error);
    if (error)
    {
        note(v, error->message);
        g_clear_error(&error);
        return;
    }
    parse_links_using(v, regex, TRUE);
    if (regex)
        g_regex_unref(regex);
}

/* EN: Validate the current pattern without running a command or reading sources; button is unused, data is the view.
 * RU: Проверяет текущий шаблон без запуска команды и чтения исходников; button не используется, data — окно. */
static void check_diagnostic_pattern(GtkWidget *button, gpointer data)
{
    (void)button;
    CommandView *v = data;
    GError *error = NULL;
    GRegex *regex = insight_custom_diagnostic_regex(gtk_entry_get_text(GTK_ENTRY(v->diagnostic_pattern)), &error);
    note(v, error ? error->message : regex ? "Diagnostic pattern is valid" : "Built-in GCC, MSVC and Python diagnostic formats selected");
    g_clear_error(&error);
    if (regex)
        g_regex_unref(regex);
}

/* EN: Add saved diagnostic options to page for v; controls are locked while a command runs.
 * RU: Добавляет сохраняемые настройки диагностики на page для v; элементы блокируются при запуске команды. */
static void diagnostic_options(CommandView *v, GtkWidget *page)
{
    v->parse_on_finish = gtk_check_button_new_with_label("");
    gtk_widget_set_name(v->parse_on_finish, "insight-custom-auto-parse");
    insight_i18n_key(G_OBJECT(v->parse_on_finish), "label", "Parse source links when command completes");
    gtk_box_pack_start(GTK_BOX(page), v->parse_on_finish, FALSE, FALSE, 0);
    g_ptr_array_add(v->locked, v->parse_on_finish);
    v->diagnostic_pattern = entry(page, "Custom diagnostic regex (empty: built-in formats)",
        "insight-custom-diagnostic-pattern", "Named groups: file and line required; column and message optional. No command-variable expansion.", v);
    gtk_entry_set_max_length(GTK_ENTRY(v->diagnostic_pattern), 1024);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(page), row, FALSE, FALSE, 0);
    button(row, "Check diagnostic pattern", "insight-custom-check-pattern", G_CALLBACK(check_diagnostic_pattern), v, FALSE);
}
