/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Retain completed memory reports and show them after simulation.
 * RU: Сохраняет завершённые отчёты памяти и показывает их после симуляции. */

#include "memory-values.h"
#include <inttypes.h>

enum { MEMORY_COMPARE = MEMORY_NOT + 1 };

enum { MEMORY_REPORT_LIMIT = 24 * 1024 * 1024, MEMORY_DEVICE_LIMIT = 128 };

/* EN: Retain complete reports in run before directory is deleted by the worker.
 * RU: Сохраняет полные отчёты в run до удаления directory рабочим потоком. */
static void simulation_memory_finish(WaveSimulation *run, const gchar *directory)
{
    for (guint id = 0; id < MEMORY_DEVICE_LIMIT; ++id)
    {
        gchar *name = g_strdup_printf("geany-memory-device-%u.dump", id);
        gchar *path = g_build_filename(directory, name, NULL);
        g_free(name);
        GStatBuf stat;
        gchar *contents = NULL;
        gsize length = 0;

        if (g_stat(path, &stat) == 0 && stat.st_size > 0 &&
            stat.st_size <= MEMORY_REPORT_LIMIT &&
            g_file_get_contents(path, &contents, &length, NULL) &&
            g_str_has_prefix(contents, "GEANY-MEMORY 1 ") &&
            g_str_has_suffix(contents, "\nEND\n") &&
            g_utf8_validate(contents, length, NULL))
        {
            if (!run->memory_reports)
                run->memory_reports = g_ptr_array_new_with_free_func(g_free);
            g_ptr_array_add(run->memory_reports, g_steal_pointer(&contents));
        }
        g_free(contents);
        g_free(path);
    }
}

typedef struct
{
    gchar **lines;
    GtkComboBox *base, *width, *endian, *encoding;
    GtkToggleButton *changes_only;
    GtkTextBuffer *buffer;
    GtkTextTag *changed;
    GtkWidget *calculator, *result;
    GtkEntry *left, *right, *target, *query;
    GtkToggleButton *numeric_search;
    GtkWidget *dump_view, *search_status;
    GtkNotebook *views;
    GtkComboBox *operation;
} MemoryPage;

/* EN: Release the report lines and page state held in data.
 * RU: Освобождает строки отчёта и состояние вкладки из data. */
static void memory_page_free(gpointer data)
{
    MemoryPage *page = data;
    g_strfreev(page->lines);
    g_free(page);
}

enum { MEMORY_TEXT_NONE, MEMORY_TEXT_ASCII, MEMORY_TEXT_UNICODE, MEMORY_TEXT_UTF8 };

/* EN: Append the printable character to text, or a dot for control characters.
 * RU: Добавляет печатный character в text или точку для управляющих символов. */
static void memory_text_character(GString *text, gunichar character)
{
    g_string_append_unichar(text, g_unichar_isprint(character) ? character : '.');
}

/* EN: Append a text column for the row in bytes and masks, bounded by size.
 * offset and count identify its bytes. value and unknown describe its decoded word.
 * encoding selects ASCII, Unicode code points or UTF-8. utf8_next tracks consumed bytes.
 * RU: Добавляет текст для строки из bytes и masks в пределах size.
 * offset и count задают байты строки. value и unknown описывают её числовое слово.
 * encoding выбирает ASCII, кодовые точки Unicode или UTF-8. utf8_next отмечает прочитанные байты. */
static void memory_text_append(GString *text, gint encoding, const guint8 *bytes,
                               const guint8 *masks, guint size, guint offset, guint count,
                               uint64_t value, uint64_t unknown, guint *utf8_next)
{
    if (encoding == MEMORY_TEXT_NONE)
        return;

    g_string_append(text, "    |");
    if (encoding == MEMORY_TEXT_UNICODE)
    {
        if (unknown || value > 0x10ffff || !g_unichar_validate((gunichar)value))
            g_string_append_c(text, '?');
        else
            memory_text_character(text, (gunichar)value);
    }
    else
    {
        for (guint pos = offset; pos < offset + count; ++pos)
        {
            if (encoding == MEMORY_TEXT_UTF8 && pos < *utf8_next)
                continue;
            if (pos >= size || masks[pos])
                g_string_append_c(text, '?');
            else if (encoding == MEMORY_TEXT_ASCII)
                memory_text_character(text, bytes[pos] < 128 ? bytes[pos] : '.');
            else
            {
                guint available = 1;
                while (available < 4 && pos + available < size && !masks[pos + available])
                    ++available;
                gunichar character = bytes[pos] == 0 ? 0 :
                    g_utf8_get_char_validated((const gchar *)bytes + pos, available);
                if (character == (gunichar)-1 || character == (gunichar)-2)
                    g_string_append_c(text, '?');
                else
                {
                    memory_text_character(text, character);
                    *utf8_next = pos + g_unichar_to_utf8(character, NULL);
                }
            }
        }
    }
    g_string_append_c(text, '|');
}

enum { MEMORY_INITIALIZED = 1, MEMORY_RECENT = 2 };

/* EN: Compare byte offsets at left and right for ascending dump order.
 * RU: Сравнивает байтовые адреса left и right для сортировки дампа по возрастанию. */
static gint memory_offset_compare(gconstpointer left, gconstpointer right)
{
    guint a = *(const guint *)left, b = *(const guint *)right;
    return (a > b) - (a < b);
}

/* EN: Set one sparse byte at offset in cells, preserving its accumulated flags.
 * RU: Задаёт разреженный байт по offset в cells, сохраняя накопленные флаги. */
static void memory_sparse_byte(GHashTable *cells, guint offset, guint value, guint mask, guint flags)
{
    gpointer key = GUINT_TO_POINTER(offset);
    guint old = GPOINTER_TO_UINT(g_hash_table_lookup(cells, key));
    guint stored = value | (mask << 8) | (flags << 16);
    if (flags == MEMORY_RECENT)
        stored |= old;
    else
        stored |= old & (MEMORY_RECENT << 16);
    g_hash_table_insert(cells, key, GUINT_TO_POINTER(stored));
}

/* EN: Rebuild page after control changes, highlighting addresses in the recent history.
 * control identifies the widget that emitted the signal.
 * RU: Обновляет page после изменения настроек и выделяет адреса из недавней истории.
 * control — виджет, отправивший сигнал. */
static void memory_page_refresh(GtkWidget *control, MemoryPage *page)
{
    (void)control;
    const guint bases[] = {16, 10, 2};
    gint selection = gtk_combo_box_get_active(page->base);
    guint base = bases[CLAMP(selection, 0, 2)];
    gboolean only = gtk_toggle_button_get_active(page->changes_only);
    gchar model[16], fill[3] = "";
    guint capacity, bits;
    if (sscanf(page->lines[0], "GEANY-MEMORY 1 %15s %u %u %2s",
               model, &capacity, &bits, fill) < 3 ||
        capacity == 0 || bits == 0 || bits > 64 || capacity > G_MAXUINT / ((bits + 7) / 8) - 8)
        return;

    const guint widths[] = {0, 1, 2, 4, 8};
    guint native_bytes = (bits + 7) / 8;
    guint selected_width = widths[CLAMP(gtk_combo_box_get_active(page->width), 0, 4)];
    guint word_bytes = selected_width ? selected_width : native_bytes;
    gboolean big_endian = gtk_combo_box_get_active(page->endian) == 1;
    guint word_bits = selected_width || big_endian ? word_bytes * 8 : bits;
    guint size = capacity * native_bytes;
    guint padded_size = ((size + word_bytes - 1) / word_bytes) * word_bytes;
    gboolean sparse = size > 8 * 1024 * 1024;
    guint8 *bytes = sparse ? NULL : g_new0(guint8, padded_size);
    guint8 *masks = sparse ? NULL : g_new(guint8, padded_size);
    guint8 *flags = sparse ? NULL : g_new0(guint8, padded_size);
    GHashTable *cells = sparse ? g_hash_table_new(g_direct_hash, g_direct_equal) : NULL;
    GArray *offsets = sparse ? g_array_new(FALSE, FALSE, sizeof(guint)) : NULL;
    gboolean default_ff = strcmp(fill, "FF") == 0;
    if (!sparse)
    {
        memset(masks, 0xff, padded_size);
        if (default_ff)
        {
            memset(bytes, 0xff, size);
            memset(masks, 0, size);
        }
    }

    for (guint i = 1; page->lines[i]; ++i)
    {
        guint address;
        uint64_t value, unknown, time, changed_address, before, before_mask, after, after_mask;
        if (sscanf(page->lines[i], "W %x %" SCNx64 " %" SCNx64,
                   &address, &value, &unknown) == 3 && address < capacity)
        {
            guint offset = address * native_bytes;
            for (guint byte = 0; byte < native_bytes; ++byte)
            {
                guint position = offset + byte;
                guint data_byte = (value >> (8 * byte)) & 0xff;
                guint mask_byte = (unknown >> (8 * byte)) & 0xff;
                if (sparse)
                {
                    guint row = position - position % word_bytes;
                    g_array_append_val(offsets, row);
                    memory_sparse_byte(cells, position, data_byte, mask_byte, MEMORY_INITIALIZED);
                }
                else
                {
                    bytes[position] = data_byte;
                    masks[position] = mask_byte;
                    flags[position] |= MEMORY_INITIALIZED;
                }
            }
        }
        else if (sscanf(page->lines[i], "C %" SCNu64 " %" SCNx64 " %" SCNx64
                        " %" SCNx64 " %" SCNx64 " %" SCNx64,
                        &time, &changed_address, &before, &before_mask, &after, &after_mask) == 6 &&
                 changed_address < capacity)
        {
            guint offset = (guint)changed_address * native_bytes;
            uint64_t changed_bits = (before ^ after) | (before_mask ^ after_mask);
            for (guint byte = 0; byte < native_bytes; ++byte)
            {
                if (!((changed_bits >> (8 * byte)) & 0xff))
                    continue;
                if (sparse)
                    memory_sparse_byte(cells, offset + byte, 0, 0, MEMORY_RECENT);
                else
                    flags[offset + byte] |= MEMORY_RECENT | (default_ff ? MEMORY_INITIALIZED : 0);
            }
        }
    }

    gtk_text_buffer_set_text(page->buffer, "", 0);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(page->buffer, &end);
    gchar *header = g_strdup_printf(insight_tr("%s · %u words · %u bits\n"), model, capacity, bits);
    gtk_text_buffer_insert(page->buffer, &end, header, -1);
    g_free(header);
    if (default_ff)
        gtk_text_buffer_insert(page->buffer, &end, insight_tr("Omitted bytes contain FF.\n"), -1);

    uint64_t word_mask = UINT64_MAX >> (64 - word_bits);
    gint encoding = gtk_combo_box_get_active(page->encoding);
    guint utf8_next = 0;
    if (sparse)
        g_array_sort(offsets, memory_offset_compare);
    guint row_count = sparse ? offsets->len : padded_size / word_bytes;
    for (guint index = 0; index < row_count; ++index)
    {
        guint offset = sparse ? g_array_index(offsets, guint, index) : index * word_bytes;
        if (sparse && index && offset == g_array_index(offsets, guint, index - 1))
            continue;

        guint8 row_flags = 0;
        guint8 row_bytes[11], row_masks[11];
        const guint8 *text_bytes = bytes, *text_masks = masks;
        guint text_offset = offset, text_size = size;
        if (sparse)
        {
            text_offset = 0;
            text_size = MIN(size - offset, word_bytes + 3);
            for (guint byte = 0; byte < word_bytes + 3; ++byte)
            {
                guint cell = GPOINTER_TO_UINT(
                    g_hash_table_lookup(cells, GUINT_TO_POINTER(offset + byte)));
                guint cell_flags = cell >> 16;
                row_bytes[byte] = cell & 0xff;
                row_masks[byte] = cell_flags & MEMORY_INITIALIZED ? (cell >> 8) & 0xff : 0xff;
                if (byte < word_bytes)
                    row_flags |= cell_flags;
            }
            text_bytes = row_bytes;
            text_masks = row_masks;
        }
        else
        {
            for (guint byte = 0; byte < word_bytes; ++byte)
                row_flags |= flags[offset + byte];
        }

        gboolean recent = (row_flags & MEMORY_RECENT) != 0;
        if (!(row_flags & MEMORY_INITIALIZED) || (only && !recent))
            continue;

        uint64_t value = memory_word_read(text_bytes + text_offset, word_bytes, big_endian);
        uint64_t unknown = memory_word_read(text_masks + text_offset, word_bytes, big_endian);
        if (!selected_width && !big_endian)
        {
            value &= word_mask;
            unknown &= word_mask;
        }
        gchar location[MEMORY_VALUE_TEXT_SIZE], word[MEMORY_VALUE_TEXT_SIZE];
        gchar mask[MEMORY_VALUE_TEXT_SIZE];
        memory_word_format(location, selected_width ? offset : offset / native_bytes, base, 0);
        memory_word_format(word, value, base, word_bits);
        memory_word_format(mask, unknown, base, word_bits);
        GString *row = g_string_new(NULL);
        g_string_append_printf(row, "%s    %s", location, word);
        if (unknown)
            g_string_append_printf(row, "    [%s]", mask);
        guint next_byte = sparse ? (utf8_next > offset ? utf8_next - offset : 0) : utf8_next;
        memory_text_append(row, encoding, text_bytes, text_masks, text_size, text_offset,
                           word_bytes, value, unknown, &next_byte);
        utf8_next = sparse ? offset + next_byte : next_byte;
        g_string_append_c(row, '\n');
        if (recent)
            gtk_text_buffer_insert_with_tags(page->buffer, &end, row->str, -1, page->changed, NULL);
        else
            gtk_text_buffer_insert(page->buffer, &end, row->str, -1);
        g_string_free(row, TRUE);
    }
    g_clear_pointer(&offsets, g_array_unref);
    g_clear_pointer(&cells, g_hash_table_unref);
    g_free(flags);
    g_free(masks);
    g_free(bytes);
}

/* EN: Create a scrollable view of buffer, with fixed-width text.
 * RU: Создаёт прокручиваемое представление buffer с моноширинным текстом. */
static GtkWidget *memory_page_scroll(GtkTextBuffer *buffer)
{
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *view = gtk_text_view_new_with_buffer(buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    return scroll;
}

/* EN: Show left and right in page's dump base, aligned and marked where digits differ.
 * RU: Показывает left и right в системе дампа page, выравнивая и выделяя разные разряды. */
static void memory_calculator_compare(MemoryPage *page, uint64_t left, uint64_t right)
{
    const guint bases[] = {16, 10, 2};
    const guint groups[] = {4, 3, 8};
    gint index = CLAMP(gtk_combo_box_get_active(page->base), 0, 2);
    guint group = groups[index];
    gchar digits[2][MEMORY_VALUE_TEXT_SIZE];
    memory_word_format(digits[0], left, bases[index], 0);
    memory_word_format(digits[1], right, bases[index], 0);
    guint lengths[] = {strlen(digits[0]), strlen(digits[1])};
    guint width = MAX(lengths[0], lengths[1]);
    guint padding[] = {width - lengths[0], width - lengths[1]};

    GString *markup = g_string_new("<tt>");
    for (guint row = 0; row < 2; ++row)
    {
        if (row)
            g_string_append_c(markup, '\n');
        for (guint col = 0; col < width; ++col)
        {
            if (col && (width - col) % group == 0)
                g_string_append_c(markup, ' ');
            gchar first = col < padding[0] ? '0' : digits[0][col - padding[0]];
            gchar second = col < padding[1] ? '0' : digits[1][col - padding[1]];
            gchar digit = row ? second : first;
            if (first != second)
                g_string_append_printf(markup, "<b><u>%c</u></b>", digit);
            else
                g_string_append_c(markup, digit);
        }
    }
    g_string_append(markup, "</tt>");
    gtk_label_set_markup(GTK_LABEL(page->result), markup->str);
    g_string_free(markup, TRUE);
}

/* EN: Calculate the selected operation for page when control changes.
 * RU: Вычисляет выбранную операцию для page после изменения control. */
static void memory_calculator_update(GtkWidget *control, MemoryPage *page)
{
    (void)control;
    gint operation = gtk_combo_box_get_active(page->operation);
    guint64 left, right = 0;
    const gchar *left_text = gtk_entry_get_text(page->left);
    const gchar *right_text = gtk_entry_get_text(page->right);
    gboolean unary = operation == MEMORY_NOT;
    gtk_widget_set_sensitive(GTK_WIDGET(page->right), !unary);

    if (!g_ascii_string_to_unsigned(left_text, 10, 0, G_MAXUINT64, &left, NULL) ||
        (!unary && !g_ascii_string_to_unsigned(right_text, 10, 0, G_MAXUINT64, &right, NULL)))
    {
        gtk_label_set_text(GTK_LABEL(page->result), insight_tr("Enter unsigned decimal operands."));
        return;
    }
    if (operation == MEMORY_COMPARE)
    {
        memory_calculator_compare(page, left, right);
        return;
    }
    uint64_t result;
    if (!memory_calculate(operation, left, right, &result))
    {
        gtk_label_set_text(GTK_LABEL(page->result), insight_tr("Division by zero."));
        return;
    }
    gchar decimal[MEMORY_VALUE_TEXT_SIZE], hex[MEMORY_VALUE_TEXT_SIZE];
    memory_word_format(decimal, result, 10, 64);
    memory_word_format(hex, result, 16, 64);
    gchar *text = g_strdup_printf("%s  (0x%s)", decimal, hex);
    gtk_label_set_text(GTK_LABEL(page->result), text);
    g_free(text);
}

/* EN: Remember the operand entry focused by event for subsequent imports into page.
 * RU: Запоминает поле entry, получившее фокус event, для переноса чисел в page. */
static gboolean memory_calculator_focus(GtkEntry *entry, GdkEventFocus *event, MemoryPage *page)
{
    (void)event;
    page->target = entry;
    return FALSE;
}

/* EN: Read the address or value under event in view into page's open calculator.
 * Return true if the double-click was handled.
 * RU: Передаёт адрес или значение под event в view в открытый калькулятор page.
 * Возвращает true, если двойной щелчок обработан. */
static gboolean memory_calculator_pick(GtkTextView *view, GdkEventButton *event, MemoryPage *page)
{
    if (event->type != GDK_2BUTTON_PRESS || event->button != 1 ||
        !gtk_widget_get_visible(page->calculator) ||
        event->window != gtk_text_view_get_window(view, GTK_TEXT_WINDOW_TEXT))
        return FALSE;

    gint x, y;
    GtkTextIter point;
    gtk_text_view_window_to_buffer_coords(view, GTK_TEXT_WINDOW_TEXT, event->x, event->y, &x, &y);
    if (!gtk_text_view_get_iter_at_location(view, &point, x, y) ||
        gtk_text_iter_get_line(&point) == 0)
        return FALSE;

    GtkTextIter start = point;
    gtk_text_iter_set_line_offset(&start, 0);
    GtkTextIter end = start;
    if (!gtk_text_iter_ends_line(&end))
        gtk_text_iter_forward_to_line_end(&end);
    gchar *line = gtk_text_buffer_get_text(page->buffer, &start, &end, FALSE);
    gchar *address_end = strchr(line, ' ');
    gboolean handled = FALSE;
    if (address_end)
    {
        gchar *value = address_end;
        while (*value == ' ')
            ++value;
        gchar *value_end = value;
        while (g_ascii_isxdigit(*value_end))
            ++value_end;

        gint offset = gtk_text_iter_get_line_offset(&point);
        gchar *digits = NULL;
        if (offset < address_end - line)
        {
            *address_end = '\0';
            digits = line;
        }
        else if (offset >= value - line && offset < value_end - line)
        {
            *value_end = '\0';
            digits = value;
        }

        const guint bases[] = {16, 10, 2};
        gint selection = gtk_combo_box_get_active(page->base);
        guint64 number;
        if (digits && g_ascii_string_to_unsigned(digits, bases[CLAMP(selection, 0, 2)],
                                                 0, G_MAXUINT64, &number, NULL))
        {
            gchar text[MEMORY_VALUE_TEXT_SIZE];
            memory_word_format(text, number, 10, 64);
            gtk_entry_set_text(gtk_widget_get_sensitive(GTK_WIDGET(page->target))
                               ? page->target : page->left, text);
            handled = TRUE;
        }
    }
    g_free(line);
    return handled;
}

/* EN: Show or hide page's calculator according to toggle.
 * RU: Показывает или скрывает калькулятор page по состоянию toggle. */
static void memory_calculator_toggle(GtkToggleButton *toggle, MemoryPage *page)
{
    gtk_widget_set_visible(page->calculator, gtk_toggle_button_get_active(toggle));
}

/* EN: Add calculator controls to box and bind them to page.
 * RU: Добавляет элементы калькулятора в box и привязывает их к page. */
static void memory_calculator_add(GtkBox *box, MemoryPage *page)
{
    page->calculator = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(box, page->calculator, FALSE, FALSE, 0);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(page->calculator), row, FALSE, FALSE, 0);
    page->left = GTK_ENTRY(gtk_entry_new());
    page->right = GTK_ENTRY(gtk_entry_new());
    page->target = page->left;
    page->operation = GTK_COMBO_BOX(gtk_combo_box_text_new());
    const gchar *operations[] = {
        "+", "−", "×", "÷", "AND", "OR", "XOR", "NOT", insight_tr("Compare digits")
    };
    for (guint i = 0; i < G_N_ELEMENTS(operations); ++i)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(page->operation), operations[i]);

    gtk_entry_set_placeholder_text(page->left, insight_tr("First operand"));
    gtk_entry_set_placeholder_text(page->right, insight_tr("Second operand"));
    gtk_entry_set_max_length(page->left, 20);
    gtk_entry_set_max_length(page->right, 20);
    gtk_combo_box_set_active(page->operation, MEMORY_ADD);
    gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(page->left), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(page->operation), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), GTK_WIDGET(page->right), TRUE, TRUE, 0);
    page->result = gtk_label_new(NULL);
    gtk_label_set_selectable(GTK_LABEL(page->result), TRUE);
    GtkWidget *result_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(result_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(result_scroll), TRUE);
    gtk_container_add(GTK_CONTAINER(result_scroll), page->result);
    gtk_box_pack_start(GTK_BOX(page->calculator), result_scroll, FALSE, FALSE, 0);
    g_signal_connect(page->left, "focus-in-event", G_CALLBACK(memory_calculator_focus), page);
    g_signal_connect(page->right, "focus-in-event", G_CALLBACK(memory_calculator_focus), page);
    g_signal_connect(page->left, "changed", G_CALLBACK(memory_calculator_update), page);
    g_signal_connect(page->right, "changed", G_CALLBACK(memory_calculator_update), page);
    g_signal_connect(page->operation, "changed", G_CALLBACK(memory_calculator_update), page);
    memory_calculator_update(NULL, page);

    gtk_widget_show_all(page->calculator);
    gtk_widget_set_no_show_all(page->calculator, TRUE);
    gtk_widget_hide(page->calculator);
}

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
