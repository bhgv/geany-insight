/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Match exact instruction addresses to bounded GDB mixed-source records without guessing a source line.
 * RU: Сопоставляет точные адреса инструкций ограниченным записям GDB с исходниками без угадывания строки. */

/* EN: Return an owned source filename and set line for exact address in successful record; NULL rejects missing/malformed mappings.
 * RU: Возвращает имя исходника и задаёт line для точного address в успешном record; NULL отклоняет отсутствующие/неверные соответствия. */
static gchar *native_instruction_source_location(const gchar *record, const gchar *address, guint *line)
{
    *line = 0;
    guint64 wanted = 0;
    if (!record || strlen(record) > 262144 || !g_str_has_prefix(record, "^done") ||
        !native_assembly_number(address, &wanted))
        return NULL;
    const gchar *cursor = strstr(record, "asm_insns=["), *start, *end;
    if (!cursor)
        return NULL;
    cursor += strlen("asm_insns=[");
    guint count = 0;
    while (count++ < 256 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *file = native_debug_field(tuple, "fullname");
        if (!file)
            file = native_debug_field(tuple, "file");
        gchar *row = native_debug_field(tuple, "line");
        guint64 number = 0;
        gboolean valid = native_name_text(file, 4096, FALSE) && row &&
            g_ascii_string_to_unsigned(row, 10, 1, G_MAXINT, &number, NULL);
        const gchar *instructions = strstr(tuple, "line_asm_insn=["), *first, *last;
        gboolean matches = FALSE;
        if (valid && instructions)
        {
            instructions += strlen("line_asm_insn=[");
            guint entries = 0;
            while (entries++ < 256 && (last = native_next_tuple(&instructions, &first)))
            {
                gchar *instruction = g_strndup(first, last - first);
                gchar *raw = native_debug_field(instruction, "address");
                guint64 actual = 0;
                matches = native_assembly_number(raw, &actual) && actual == wanted;
                g_free(raw);
                g_free(instruction);
                if (matches)
                    break;
            }
        }
        g_free(row);
        g_free(tuple);
        if (matches)
        {
            *line = (guint)number;
            return file;
        }
        g_free(file);
    }
    return NULL;
}
