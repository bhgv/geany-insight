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

/* EN: Resolve file exactly through record's complete source list of at most 4096 entries.
 * Return one owned unambiguous fullname or NULL for missing, conflicting, truncated or over-limit catalogs.
 * RU: Разрешает точное file по полному списку record не более чем из 4096 записей.
 * Возвращает единственный полный путь либо NULL при отсутствии, конфликте, обрезке или превышении лимита. */
static gchar *native_instruction_source_fullname(const gchar *record, const gchar *file)
{
    if (!record || !file || strlen(record) > 4194304 || !g_str_has_prefix(record, "^done"))
        return NULL;
    const gchar *cursor = strstr(record, "files=["), *start, *end;
    if (!cursor)
        return NULL;
    cursor += strlen("files=[");
    gchar *result = NULL;
    guint count = 0;
    while (count < 4096 && (end = native_next_tuple(&cursor, &start)))
    {
        count++;
        gchar *tuple = g_strndup(start, end - start);
        gchar *name = native_debug_field(tuple, "file");
        gchar *full = native_debug_field(tuple, "fullname");
        gboolean matches = !g_strcmp0(name, file) && native_name_text(full, 4096, FALSE);
        gboolean conflict = matches && result && g_strcmp0(result, full);
        if (matches && !result)
            result = g_strdup(full);
        g_free(full);
        g_free(name);
        g_free(tuple);
        if (conflict)
        {
            g_free(result);
            return NULL;
        }
    }
    if (*cursor != ']')
    {
        g_free(result);
        result = NULL;
    }
    return result;
}
