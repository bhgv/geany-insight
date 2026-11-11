/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Resolve instruction rows and confirmed code breakpoints without guessing operand addresses.
 * RU: Определяет адреса строк инструкций и подтверждённые точки кода без угадывания адресов операндов. */

/* EN: Return the leading hex address in line, skipping display markers; NULL means no instruction. Caller frees it.
 * RU: Возвращает начальный шестнадцатеричный адрес line без маркеров; NULL означает отсутствие инструкции. Результат освобождает вызывающий. */
static gchar *native_instruction_address(const gchar *line)
{
    if (!line)
        return NULL;
    while (g_ascii_isspace(*line))
        line++;
    if (g_str_has_prefix(line, "● ") || g_str_has_prefix(line, "○ "))
        line += strlen("● ");
    if (g_str_has_prefix(line, "▶ "))
        line += strlen("▶ ");
    if (!g_str_has_prefix(line, "0x") || !g_ascii_isxdigit(line[2]))
        return NULL;
    const gchar *end = line + 2;
    while (g_ascii_isxdigit(*end))
        end++;
    if (*end && !g_ascii_isspace(*end))
        return NULL;
    gchar *raw = g_strndup(line, end - line);
    guint64 address;
    gchar *result = native_assembly_number(raw, &address) ?
        g_strdup_printf("0x%" G_GINT64_MODIFIER "x", address) : NULL;
    g_free(raw);
    return result;
}

/* EN: Record session's confirmed tuple code address; parent supplies multi-location metadata. Never expose source roots for deletion.
 * RU: Запоминает подтверждённый адрес кода tuple в session; parent задаёт метаданные составной точки. Точки исходников не доступны для удаления здесь. */
static void native_instruction_point(NativeDebug *session, const gchar *tuple, const gchar *parent)
{
    NativeAssembly *assembly = session->assembly;
    if (!assembly || !assembly->instruction_points)
        return;
    gchar *kind = native_debug_field(tuple, "type");
    if (!kind && parent)
        kind = native_debug_field(parent, "type");
    if (g_strcmp0(kind, "breakpoint") && g_strcmp0(kind, "hw breakpoint"))
    {
        g_free(kind);
        return;
    }
    gchar *raw = native_debug_field(tuple, "addr");
    gchar *address = native_instruction_address(raw);
    gchar *enabled = native_debug_field(tuple, "enabled");
    gchar *parent_enabled = parent ? native_debug_field(parent, "enabled") : NULL;
    if (address)
    {
        gboolean active = !g_strcmp0(enabled, "y") && g_strcmp0(parent_enabled, "n");
        gpointer previous = g_hash_table_lookup(assembly->instruction_marks, address);
        g_hash_table_replace(assembly->instruction_marks, g_strdup(address),
            GUINT_TO_POINTER(active || GPOINTER_TO_UINT(previous) == 2 ? 2 : 1));
        gchar *original = native_debug_field(tuple, "original-location");
        gchar *number = native_debug_field(tuple, "number");
        if (original && *original == '*' && native_context_id(number))
            g_hash_table_replace(assembly->instruction_points, g_strdup(address), g_strdup(number));
        g_free(number);
        g_free(original);
    }
    g_free(parent_enabled);
    g_free(enabled);
    g_free(address);
    g_free(raw);
    g_free(kind);
}

/* EN: Clear session's confirmed instruction point cache before rebuilding a successful breakpoint snapshot.
 * RU: Очищает подтверждённые точки инструкций session перед перестроением успешного снимка точек GDB. */
static void native_instruction_points_reset(NativeDebug *session)
{
    if (!session->assembly || !session->assembly->instruction_points)
        return;
    g_hash_table_remove_all(session->assembly->instruction_points);
    g_hash_table_remove_all(session->assembly->instruction_marks);
}
