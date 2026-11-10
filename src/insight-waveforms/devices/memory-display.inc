/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Memory snapshots and byte rendering.
 * RU: Снимки памяти и отображение байтов.
 */

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Retain completed memory reports and show them after simulation.
 * RU: Сохраняет завершённые отчёты памяти и показывает их после симуляции. */

#include "../memory-values.h"
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
        GStatBuf report_stat;
        gchar *contents = NULL;
        gsize length = 0;

        if (g_stat(path, &report_stat) == 0 && report_stat.st_size > 0 &&
            report_stat.st_size <= MEMORY_REPORT_LIMIT &&
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
