/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Validate bounded GDB memory blocks and format byte addresses, hex and ASCII.
 * RU: Проверяет ограниченные блоки памяти GDB и форматирует адреса, hex и ASCII. */

/* EN: Parse GDB's hexadecimal text into value, accepting its optional 0x prefix; return FALSE on errors.
 * RU: Разбирает шестнадцатеричный text GDB в value с необязательным префиксом 0x; ошибка даёт FALSE. */
static gboolean native_memory_address(const gchar *text, guint64 *value)
{
    if (!text)
        return FALSE;
    if (g_str_has_prefix(text, "0x") || g_str_has_prefix(text, "0X"))
        text += 2;
    return g_ascii_string_to_unsigned(text, 16, 0, G_MAXUINT64, value, NULL);
}

/* EN: Format record for count requested bytes; return allocated text or NULL for invalid/gapped blocks.
 * RU: Форматирует record для count запрошенных байтов; возвращает текст либо NULL при неверных блоках/пропусках. */
static gchar *native_memory_format(const gchar *record, guint count)
{
    const gchar *body = strstr(record, "memory=[");
    if (!g_str_has_prefix(record, "^done") || !body || !count || count > 4096)
        return NULL;
    guchar bytes[4096] = {0};
    gboolean filled[4096] = {FALSE};
    guint64 base = 0;
    guint total = 0;
    const gchar *cursor = body + strlen("memory=[");
    const gchar *start_tuple, *end_tuple;
    while ((end_tuple = native_next_tuple(&cursor, &start_tuple)))
    {
        gchar *tuple = g_strndup(start_tuple, end_tuple - start_tuple);
        gchar *begin = native_debug_field(tuple, "begin");
        gchar *offset = native_debug_field(tuple, "offset");
        gchar *contents = native_debug_field(tuple, "contents");
        guint64 address = 0, start = 0;
        gboolean valid = begin && offset && contents &&
            native_memory_address(begin, &address) &&
            native_memory_address(offset, &start) && start < count;
        gsize length = contents ? strlen(contents) : 0;
        valid = valid && length > 0 && !(length % 2) && length / 2 <= count - start;
        if (valid && !total)
        {
            valid = address >= start;
            base = address - start;
        }
        valid = valid && start <= G_MAXUINT64 - base && address == base + start;
        for (guint i = 0; valid && i < length / 2; i++)
        {
            gint high = g_ascii_xdigit_value(contents[i * 2]);
            gint low = g_ascii_xdigit_value(contents[i * 2 + 1]);
            guint index = (guint)start + i;
            valid = high >= 0 && low >= 0 && !filled[index];
            if (valid)
            {
                bytes[index] = (guchar)((high << 4) | low);
                filled[index] = TRUE;
                total++;
            }
        }
        g_free(contents);
        g_free(offset);
        g_free(begin);
        g_free(tuple);
        if (!valid)
            return NULL;
    }
    if (total != count || count - 1 > G_MAXUINT64 - base)
        return NULL;
    GString *text = g_string_new(NULL);
    for (guint row = 0; row < count; row += 16)
    {
        g_string_append_printf(text, "0x%016" G_GINT64_MODIFIER "x  ", base + row);
        for (guint column = 0; column < 16; column++)
        {
            if (row + column < count)
                g_string_append_printf(text, "%02x ", bytes[row + column]);
            else
                g_string_append(text, "   ");
        }
        g_string_append(text, " | ");
        for (guint column = 0; column < 16 && row + column < count; column++)
        {
            guchar byte = bytes[row + column];
            g_string_append_c(text, byte >= 32 && byte <= 126 ? (gchar)byte : '.');
        }
        g_string_append_c(text, '\n');
    }
    return g_string_free(text, FALSE);
}
