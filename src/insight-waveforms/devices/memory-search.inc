/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Memory search and inspector windows.
 * RU: Поиск в памяти и окна инспектора.
 */

/* EN: Compile query into a matcher for complete numeric rows of page's dump.
 * Return NULL for invalid syntax. Counts are decimal, literals use the dump base.
 * RU: Компилирует query для поиска полных числовых строк дампа page.
 * Возвращает NULL при ошибке. Количество задаётся десятичным числом. */
static GRegex *memory_search_pattern(MemoryPage *page, const gchar *query)
{
    const guint bases[] = {16, 10, 2};
    guint base = bases[CLAMP(gtk_combo_box_get_active(page->base), 0, 2)];
    GRegex *lexer = g_regex_new(
        "\\G\\s*([[:xdigit:]]+|[?*])(?:\\s*\\[\\s*([0-9]+)"
        "(?:\\s*(?:\\.\\.|-)\\s*([0-9]+))?\\s*\\])?(?=\\s|$)",
        0, 0, NULL);
    GMatchInfo *match = NULL;
    g_regex_match(lexer, query, 0, &match);
    GString *pattern = g_string_new("(?m)^");
    gboolean valid = FALSE;
    gint end = 0;

    while (g_match_info_matches(match))
    {
        gchar *atom = g_match_info_fetch(match, 1);
        gchar *minimum = g_match_info_fetch(match, 2);
        gchar *maximum = g_match_info_fetch(match, 3);
        guint64 low = 1, high = 1, value = 0;
        gboolean star = atom[0] == '*';
        gboolean wildcard = star || atom[0] == '?';
        valid = (wildcard || g_ascii_string_to_unsigned(atom, base, 0, G_MAXUINT64, &value, NULL)) &&
                (!*minimum || (!star &&
                 g_ascii_string_to_unsigned(minimum, 10, 0, 65535, &low, NULL))) &&
                (!*maximum || g_ascii_string_to_unsigned(maximum, 10, low, 65535, &high, NULL));
        if (valid)
        {
            g_string_append(pattern, "(?:[0-9A-F]+    ");
            if (wildcard)
                g_string_append(pattern, "[0-9A-F]+");
            else
            {
                gchar digits[MEMORY_VALUE_TEXT_SIZE];
                memory_word_format(digits, value, base, 0);
                g_string_append_printf(pattern, "0*%s", digits);
            }
            g_string_append(pattern, "(?= |\\n)[^\\n]*\\n)");
            if (star)
                g_string_append(pattern, "*?");
            else if (*maximum)
                g_string_append_printf(pattern, "{%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT "}?", low, high);
            else if (*minimum)
                g_string_append_printf(pattern, "{%" G_GUINT64_FORMAT "}", low);
            g_match_info_fetch_pos(match, 0, NULL, &end);
        }
        g_free(maximum);
        g_free(minimum);
        g_free(atom);
        if (!valid || !g_match_info_next(match, NULL))
            break;
    }
    g_match_info_free(match);
    g_regex_unref(lexer);
    while (g_ascii_isspace(query[end]))
        ++end;
    GRegex *result = valid && !query[end] ? g_regex_new(pattern->str, 0, 0, NULL) : NULL;
    g_string_free(pattern, TRUE);
    return result;
}

/* EN: Find the next match from page's selection, wrapping once. trigger is unused.
 * RU: Ищет следующее совпадение после выделения page с переходом к началу.
 * trigger не используется. */
static void memory_search_next(GtkWidget *trigger, MemoryPage *page)
{
    (void)trigger;
    const gchar *query = gtk_entry_get_text(page->query);
    GtkTextIter start, end, origin, match_start, match_end;
    gtk_text_buffer_get_bounds(page->buffer, &start, &end);
    if (!gtk_text_buffer_get_selection_bounds(page->buffer, NULL, &origin))
        origin = start;
    gboolean found = FALSE;
    const gchar *status = insight_tr("No match");
    if (!*query)
        status = insight_tr("Enter a search pattern");
    else if (!gtk_toggle_button_get_active(page->numeric_search))
    {
        found = gtk_text_iter_forward_search(&origin, query, GTK_TEXT_SEARCH_TEXT_ONLY,
                                             &match_start, &match_end, NULL) ||
                gtk_text_iter_forward_search(&start, query, GTK_TEXT_SEARCH_TEXT_ONLY,
                                             &match_start, &match_end, NULL);
    }
    else
    {
        GRegex *regex = memory_search_pattern(page, query);
        if (!regex)
            status = insight_tr("Invalid numeric pattern");
        else
        {
            gchar *dump = gtk_text_buffer_get_text(page->buffer, &start, &end, FALSE);
            const gchar *position = g_utf8_offset_to_pointer(dump, gtk_text_iter_get_offset(&origin));
            GMatchInfo *match = NULL;
            found = g_regex_match_full(regex, dump, -1, position - dump,
                                       G_REGEX_MATCH_NOTEMPTY, &match, NULL);
            if (!found)
            {
                g_match_info_free(match);
                found = g_regex_match_full(regex, dump, -1, 0,
                                           G_REGEX_MATCH_NOTEMPTY, &match, NULL);
            }
            if (found)
            {
                gint first, last;
                g_match_info_fetch_pos(match, 0, &first, &last);
                gtk_text_buffer_get_iter_at_offset(page->buffer, &match_start,
                                                  g_utf8_pointer_to_offset(dump, dump + first));
                gtk_text_buffer_get_iter_at_offset(page->buffer, &match_end,
                                                  g_utf8_pointer_to_offset(dump, dump + last));
            }
            g_match_info_free(match);
            g_free(dump);
            g_regex_unref(regex);
        }
    }
    gtk_label_set_text(GTK_LABEL(page->search_status), found ? "" : status);
    if (found)
    {
        gtk_notebook_set_current_page(page->views, 0);
        gtk_text_buffer_select_range(page->buffer, &match_start, &match_end);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(page->dump_view), &match_start, 0.1, FALSE, 0, 0);
    }
}

/* EN: Create an SRAM tab using the completed report.
 * RU: Создаёт вкладку SRAM из завершённого отчёта report. */
static GtkWidget *memory_page_new(const gchar *report)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    MemoryPage *page = g_new0(MemoryPage, 1);
    page->lines = g_strsplit(report, "\n", -1);
    gboolean read_only = g_str_has_suffix(page->lines[0], " RO");
    g_object_set_data_full(G_OBJECT(box), "memory-page", page, memory_page_free);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    GtkWidget *base = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(base), "Hex (16)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(base), "Dec (10)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(base), "Bin (2)");
    page->base = GTK_COMBO_BOX(base);
    gtk_combo_box_set_active(page->base, 0);
    gtk_box_pack_start(GTK_BOX(bar), base, FALSE, FALSE, 0);

    GtkWidget *width = gtk_combo_box_text_new();
    const gchar *width_labels[] = {insight_tr("Native"), "8 bit", "16 bit", "32 bit", "64 bit"};
    for (guint i = 0; i < G_N_ELEMENTS(width_labels); ++i)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(width), width_labels[i]);
    page->width = GTK_COMBO_BOX(width);
    gtk_combo_box_set_active(page->width, 0);
    gtk_box_pack_start(GTK_BOX(bar), width, FALSE, FALSE, 0);

    GtkWidget *endian = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(endian), "Little Endian");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(endian), "Big Endian");
    page->endian = GTK_COMBO_BOX(endian);
    gtk_combo_box_set_active(page->endian, 0);
    gtk_box_pack_start(GTK_BOX(bar), endian, FALSE, FALSE, 0);

    GtkWidget *encoding = gtk_combo_box_text_new();
    const gchar *encodings[] = {insight_tr("No text"), "ASCII", "Unicode", "UTF-8"};
    for (guint i = 0; i < G_N_ELEMENTS(encodings); ++i)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(encoding), encodings[i]);
    page->encoding = GTK_COMBO_BOX(encoding);
    gtk_combo_box_set_active(page->encoding, MEMORY_TEXT_NONE);
    gtk_box_pack_start(GTK_BOX(bar), encoding, FALSE, FALSE, 0);

    GtkWidget *filter = gtk_check_button_new_with_label(insight_tr("Changed words only"));
    page->changes_only = GTK_TOGGLE_BUTTON(filter);
    gtk_box_pack_start(GTK_BOX(bar), filter, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(filter, read_only);

    GtkWidget *calculator_toggle = gtk_toggle_button_new_with_label(insight_tr("Calculator"));
    gtk_box_pack_start(GTK_BOX(bar), calculator_toggle, FALSE, FALSE, 0);
    memory_calculator_add(GTK_BOX(box), page);
    g_signal_connect(calculator_toggle, "toggled", G_CALLBACK(memory_calculator_toggle), page);

    GtkWidget *search = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(box), search, FALSE, FALSE, 0);
    page->query = GTK_ENTRY(gtk_search_entry_new());
    gtk_entry_set_placeholder_text(page->query, insight_tr("Search dump"));
    gtk_entry_set_max_length(page->query, 256);
    gtk_box_pack_start(GTK_BOX(search), GTK_WIDGET(page->query), TRUE, TRUE, 0);
    page->numeric_search = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label(insight_tr("Numeric")));
    gtk_box_pack_start(GTK_BOX(search), GTK_WIDGET(page->numeric_search), FALSE, FALSE, 0);
    GtkWidget *next = gtk_button_new_with_label(insight_tr("Find next"));
    gtk_box_pack_start(GTK_BOX(search), next, FALSE, FALSE, 0);
    page->search_status = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(search), page->search_status, FALSE, FALSE, 0);
    g_signal_connect(next, "clicked", G_CALLBACK(memory_search_next), page);
    g_signal_connect(page->query, "activate", G_CALLBACK(memory_search_next), page);

    GtkWidget *notebook = gtk_notebook_new();
    page->views = GTK_NOTEBOOK(notebook);
    gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE, 0);
    page->buffer = gtk_text_buffer_new(NULL);
    page->changed = gtk_text_buffer_create_tag(page->buffer, NULL, "weight", PANGO_WEIGHT_BOLD, NULL);
    GtkWidget *dump = memory_page_scroll(page->buffer);
    page->dump_view = gtk_bin_get_child(GTK_BIN(dump));
    g_signal_connect(page->dump_view, "button-press-event", G_CALLBACK(memory_calculator_pick), page);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), dump, gtk_label_new(insight_tr("Dump")));
    g_object_unref(page->buffer);

    if (!read_only)
    {
        GtkTextBuffer *history = gtk_text_buffer_new(NULL);
        GString *entries = g_string_new(NULL);
        for (guint i = 1; page->lines[i]; ++i)
        {
            if (g_str_has_prefix(page->lines[i], "CHANGES ") || g_str_has_prefix(page->lines[i], "C "))
                g_string_append_printf(entries, "%s\n", page->lines[i]);
        }
        gtk_text_buffer_set_text(history, entries->str, -1);
        g_string_free(entries, TRUE);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), memory_page_scroll(history),
                                 gtk_label_new(insight_tr("Recent writes")));
        g_object_unref(history);
    }

    g_signal_connect(base, "changed", G_CALLBACK(memory_page_refresh), page);
    g_signal_connect(base, "changed", G_CALLBACK(memory_calculator_update), page);
    g_signal_connect(width, "changed", G_CALLBACK(memory_page_refresh), page);
    g_signal_connect(endian, "changed", G_CALLBACK(memory_page_refresh), page);
    g_signal_connect(encoding, "changed", G_CALLBACK(memory_page_refresh), page);
    g_signal_connect(filter, "toggled", G_CALLBACK(memory_page_refresh), page);
    memory_page_refresh(NULL, page);
    return box;
}

/* EN: Open run's completed memory reports in a dialog owned by parent.
 * RU: Открывает завершённые отчёты памяти run в диалоге окна parent. */
static void simulation_memory_show(WaveSimulation *run, GtkWindow *parent)
{
    if (!run->memory_reports)
        return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(insight_tr("Test device memory"), parent,
        GTK_DIALOG_DESTROY_WITH_PARENT, insight_tr("_Close"), GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 800, 540);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       notebook, TRUE, TRUE, 0);
    for (guint i = 0; i < run->memory_reports->len; ++i)
    {
        const gchar *report = g_ptr_array_index(run->memory_reports, i);
        gchar model[16];
        if (sscanf(report, "GEANY-MEMORY 1 %15s", model) != 1)
            continue;
        GtkWidget *page = memory_page_new(report);
        gchar *name = g_strdup_printf("%s %u", model, i + 1);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, gtk_label_new(name));
        g_free(name);
    }
    gtk_widget_show_all(dialog);
}
