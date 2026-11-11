/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Ask GDB for actual symbols at numeric operands, across x86, ARM and RISC-V targets.
 * RU: Запрашивает у GDB настоящие символы числовых операндов для целей x86, ARM и RISC-V. */

/* EN: Queue at most 64 distinct hexadecimal operand lookups from record in assembly; no assembly-language guessing is used.
 * RU: Ставит до 64 разных запросов hex-операндов record в assembly без угадывания языка ассемблера. */
static void native_assembly_symbols(NativeAssembly *assembly, const gchar *record)
{
    const gchar *cursor = strstr(record, "asm_insns=["), *start, *end;
    if (!cursor)
        return;
    cursor += strlen("asm_insns=[");
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    guint rows = 0;
    while (rows++ < 256 && g_hash_table_size(seen) < 64 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *instruction = native_debug_field(tuple, "inst");
        const gchar *p = instruction;
        while (p && (p = strstr(p, "0x")) && g_hash_table_size(seen) < 64)
        {
            const gchar *last = p + 2;
            while (g_ascii_isxdigit(*last))
                last++;
            gchar *address = g_strndup(p, last - p);
            guint64 numeric = 0;
            if (native_assembly_number(address, &numeric) && numeric >= 4096 &&
                !g_hash_table_contains(seen, address))
            {
                gchar *command = g_strdup_printf("-interpreter-exec console \"info symbol %s\"", address);
                native_assembly_queue(assembly, command, 12, address, NULL, NULL, NULL, 0, FALSE);
                g_hash_table_add(seen, g_strdup(address));
            }
            g_free(address);
            p = last;
        }
        g_free(instruction);
        g_free(tuple);
    }
    g_hash_table_unref(seen);
}

/* EN: Capture only GDB console line emitted for session's owned info-symbol request, bounded to 4096 bytes.
 * RU: Захватывает только строку консоли GDB собственного запроса символа session, не более 4096 байтов. */
static void native_assembly_console(NativeDebug *session, const gchar *line)
{
    NativeAssemblyWork *work = session->assembly ? session->assembly->pending : NULL;
    if (!work || work->action != 12 || line[0] != '~')
        return;
    gchar *field = g_strconcat("value=", line + 1, NULL);
    gchar *text = native_debug_field(field, "value");
    if (text && strlen(text) <= 4096 && (!work->reply || strlen(work->reply) + strlen(text) <= 4096))
    {
        gchar *joined = g_strconcat(work->reply ? work->reply : "", text, NULL);
        g_free(work->reply);
        work->reply = joined;
    }
    g_free(text);
    g_free(field);
}

/* EN: Retain a confirmed symbol result from work in assembly; absence of a symbol never invents a name.
 * RU: Сохраняет подтверждённый результат символа work в assembly; отсутствие символа не создаёт вымышленного имени. */
static void native_assembly_symbol_reply(NativeAssembly *assembly, NativeAssemblyWork *work, const gchar *record)
{
    gchar *section = work->reply ? strstr(work->reply, " in section ") : NULL;
    guint64 address = 0;
    if (!section || !g_str_has_prefix(record, "^done") || !native_assembly_number(work->expression, &address))
        return;
    gchar *name = g_strndup(work->reply, section - work->reply);
    g_strstrip(name);
    if (native_name_text(name, 512, FALSE))
    {
        NativeNamedRange *range = g_new0(NativeNamedRange, 1);
        range->address = address;
        range->size = 1;
        range->name = g_steal_pointer(&name);
        range->scope = g_strdup("");
        g_ptr_array_add(assembly->ranges, range);
    }
    g_free(name);
}
