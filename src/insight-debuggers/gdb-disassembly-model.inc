/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Build bounded disassembly requests and format GDB instruction tuples.
 * RU: Создаёт ограниченные запросы дизассемблера и форматирует инструкции GDB. */

/* EN: Return an owned MI request for expression/span (1..128 bytes), or NULL for invalid input.
 * RU: Возвращает команду MI для expression/span (1..128 байтов) либо NULL для неверных данных. */
static gchar *native_assembly_window(const gchar *expression, guint span)
{
    if (!span || span > 128 || !expression || !*expression || strlen(expression) > 2048 ||
        !g_utf8_validate(expression, -1, NULL) || g_utf8_strlen(expression, -1) > 512 ||
        strpbrk(expression, "\r\n"))
        return NULL;
    gchar *end = g_strdup_printf("(%s)+%u", expression, span);
    gchar *escaped = g_strescape(expression, NULL);
    gchar *escaped_end = g_strescape(end, NULL);
    gchar *command = g_strdup_printf("-data-disassemble -s \"%s\" -e \"%s\" -- 2", escaped, escaped_end);
    g_free(escaped_end);
    g_free(escaped);
    g_free(end);
    return command;
}

/* EN: Return the default bounded 128-byte request for expression, or NULL when validation fails.
 * RU: Возвращает стандартный запрос до 128 байтов для expression либо NULL при ошибке проверки. */
static gchar *native_assembly_command(const gchar *expression)
{
    return native_assembly_window(expression, 128);
}

/* EN: Return an owned, bounded GDB opcode string raw when all hex groups contain whole bytes; NULL means absent/invalid bytes.
 * RU: Возвращает ограниченную строку кодов raw GDB, если группы содержат целые байты; NULL означает отсутствие или неверные байты. */
static gchar *native_assembly_bytes(const gchar *raw)
{
    if (!raw || !*raw || strlen(raw) > 512)
        return NULL;
    guint digits = 0, total = 0;
    for (const gchar *p = raw; ; p++)
    {
        if (!*p || *p == ' ' || *p == '\t')
        {
            if (digits % 2)
                return NULL;
            total += digits;
            digits = 0;
            if (!*p)
                break;
        }
        else if (strchr("0123456789abcdefABCDEF", *p))
            digits++;
        else
            return NULL;
    }
    return total ? g_strdup(raw) : NULL;
}

/* EN: Return owned instructions from record, optionally showing bytes, capped at 256 rows/64 KiB; NULL denotes malformed/error output.
 * RU: Возвращает инструкции record с необязательными bytes до 256 строк/64 КиБ; NULL означает повреждение или ошибку. */
static gchar *native_assembly_format_mode(const gchar *record, gboolean bytes)
{
    const gchar *prefix = "^done,asm_insns=[";
    if (!g_str_has_prefix(record, prefix))
        return NULL;
    const gchar *cursor = record + strlen(prefix), *start, *end;
    GString *text = g_string_new("");
    guint count = 0;
    gboolean complete = TRUE;
    while ((end = native_next_tuple(&cursor, &start)))
    {
        if (++count > 256 || end - start > 8192)
            break;
        gchar *tuple = g_strndup(start, end - start);
        gchar *address = native_debug_field(tuple, "address");
        gchar *instruction = native_debug_field(tuple, "inst");
        gchar *function = native_debug_field(tuple, "func-name");
        gchar *offset = native_debug_field(tuple, "offset");
        gboolean valid = address && instruction && !strpbrk(address, "\r\n") &&
            g_utf8_validate(instruction, -1, NULL);
        if (valid)
        {
            gchar *label = g_strdup_printf("%s + %s", function ? function : "?", offset ? offset : "0");
            gchar *safe = g_strescape(label, NULL);
            if (bytes)
            {
                gchar *raw = native_debug_field(tuple, "opcodes");
                gchar *opcodes = native_assembly_bytes(raw);
                g_string_append_printf(text, "%s  <%s>  [%s]  %s\n", address, safe, opcodes ? opcodes : "??", instruction);
                g_free(opcodes);
                g_free(raw);
            }
            else
                g_string_append_printf(text, "%s  <%s>  %s\n", address, safe, instruction);
            g_free(safe);
            g_free(label);
        }
        g_free(offset);
        g_free(function);
        g_free(instruction);
        g_free(address);
        g_free(tuple);
        if (!valid || text->len > 65536)
        {
            complete = FALSE;
            break;
        }
    }
    if (!complete || !count || count > 256 || text->len > 65536 || *cursor != ']')
    {
        g_string_free(text, TRUE);
        return NULL;
    }
    return g_string_free(text, FALSE);
}

/* EN: Return normal instruction text for record without bytes; caller owns the result or NULL on invalid output.
 * RU: Возвращает обычный текст инструкций record без байтов; результат принадлежит вызывающему, при ошибке NULL. */
static gchar *native_assembly_format(const gchar *record)
{
    return native_assembly_format_mode(record, FALSE);
}

/* EN: Return record text with validated GDB byte groups; unavailable bytes are marked ?? and never invented.
 * RU: Возвращает текст record с проверенными группами байтов GDB; отсутствующие байты отмечаются ??, без вымышленных значений. */
static gchar *native_assembly_format_bytes(const gchar *record)
{
    return native_assembly_format_mode(record, TRUE);
}
