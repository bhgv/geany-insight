/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Keep a bounded history of confirmed code addresses; failed reads never move the history cursor.
 * RU: Хранит ограниченную историю подтверждённых адресов кода; неуспешное чтение не сдвигает позицию истории. */

/* EN: Clear assembly's process-specific navigation state without freeing GTK widgets.
 * RU: Очищает состояние навигации assembly, относящееся к процессу, без освобождения виджетов GTK. */
static void native_navigation_clear(NativeAssembly *assembly)
{
    if (assembly->history)
        g_ptr_array_set_size(assembly->history, 0);
    assembly->history_position = assembly->history_requested = 0;
    assembly->history_replaying = FALSE;
}

/* EN: Record assembly's successful record start or commit a pending history move; reject errors, leaving the old position intact.
 * RU: Записывает начало успешного record в assembly либо подтверждает переход истории; при ошибке сохраняет старую позицию. */
static void native_navigation_record(NativeAssembly *assembly, const gchar *record)
{
    gboolean replaying = assembly->history_replaying;
    assembly->history_replaying = FALSE;
    if (!record || !g_str_has_prefix(record, "^done") || !strstr(record, "asm_insns=["))
        return;
    gchar *raw = native_debug_field(record, "address");
    guint64 value = 0;
    gboolean valid = native_assembly_number(raw, &value);
    g_free(raw);
    if (!valid)
        return;
    gchar *address = g_strdup_printf("0x%" G_GINT64_MODIFIER "x", value);
    if (!assembly->history)
        assembly->history = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *history = assembly->history;
    if (replaying)
    {
        if (assembly->history_requested < history->len &&
            !g_strcmp0(address, history->pdata[assembly->history_requested]))
            assembly->history_position = assembly->history_requested;
        g_free(address);
        return;
    }
    if (history->len && !g_strcmp0(address, history->pdata[assembly->history_position]))
    {
        g_free(address);
        return;
    }
    if (history->len)
        g_ptr_array_set_size(history, assembly->history_position + 1);
    if (history->len == 64)
        g_ptr_array_remove_index(history, 0);
    g_ptr_array_add(history, address);
    assembly->history_position = history->len - 1;
}
