/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Encode literal argv for GDB's raw -exec-arguments command and the target's startup convention.
 * RU: Кодирует буквальный argv для сырой команды GDB -exec-arguments и правил запуска целевой ОС. */

/* EN: Quote argument for the Windows C runtime command line; caller frees the result. Double backslashes
 * before a quote or the closing quote, while preserving ordinary path separators and empty arguments.
 * RU: Экранирует argument для командной строки C runtime Windows; результат освобождает вызывающий.
 * Удваивает слеши перед кавычкой, сохраняя обычные разделители путей и пустые аргументы. */
static gchar *native_windows_argument(const gchar *argument)
{
    GString *quoted = g_string_new("\"");
    guint slashes = 0;
    for (const gchar *p = argument;; p++)
    {
        if (*p == '\\')
        {
            slashes++;
            continue;
        }
        guint copies = (*p == '"' || !*p) ? slashes * 2 : slashes;
        for (guint i = 0; i < copies; i++)
            g_string_append_printf(quoted, "%c", '\\');
        slashes = 0;
        if (!*p)
            break;
        if (*p == '"')
            g_string_append_printf(quoted, "%c", '\\');
        g_string_append_printf(quoted, "%c", *p);
    }
    g_string_append_printf(quoted, "%c", '"');
    return g_string_free(quoted, FALSE);
}

/* EN: Parse text's quoted arguments and return an owned raw MI command for windows_target (local
 * Windows) or a POSIX target. Set error and return NULL on bad quoting or multiline input. Unix
 * launches must enable startup-with-shell: older GDB splits no-shell arguments only on whitespace.
 * RU: Разбирает кавычки text и возвращает сырую команду MI для windows_target (локальная Windows)
 * либо POSIX-цели. При неверных кавычках/переводах строк задаёт error и возвращает NULL. Unix требует
 * startup-with-shell: старые GDB без оболочки разделяют аргументы только по пробелам. */
static gchar *native_launch_arguments(const gchar *text, gboolean windows_target, GError **error)
{
    if (strpbrk(text, "\r\n"))
    {
        g_set_error_literal(error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING,
                            insight_tr("Program arguments must not contain line breaks."));
        return NULL;
    }
    gint count = 0;
    gchar **arguments = NULL;
    gchar *trimmed = g_strdup(text);
    g_strstrip(trimmed);
    gboolean valid = !*trimmed || g_shell_parse_argv(trimmed, &count, &arguments, error);
    g_free(trimmed);
    if (!valid)
        return NULL;
    GString *command = g_string_new("-exec-arguments");
    for (gint i = 0; i < count; i++)
    {
        gchar *quoted = windows_target ? native_windows_argument(arguments[i]) : g_shell_quote(arguments[i]);
        g_string_append_printf(command, " %s", quoted);
        g_free(quoted);
    }
    g_strfreev(arguments);
    return g_string_free(command, FALSE);
}
