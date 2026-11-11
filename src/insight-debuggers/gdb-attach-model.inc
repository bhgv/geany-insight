/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Validate process identifiers before constructing GDB attach commands.
 * RU: Проверяет идентификаторы процессов перед созданием команд подключения GDB. */

/* EN: Return an owned attach command for decimal pid, or NULL for zero, overflow or nondecimal input.
 * RU: Возвращает команду подключения для десятичного pid либо NULL для нуля, переполнения и неверного ввода. */
static gchar *native_attach_command(const gchar *pid)
{
    if (!native_context_id(pid) || strlen(pid) > 10)
        return NULL;
    guint64 number = g_ascii_strtoull(pid, NULL, 10);
    if (!number || number > G_MAXINT)
        return NULL;
    return g_strdup_printf("-target-attach %u", (guint)number);
}
