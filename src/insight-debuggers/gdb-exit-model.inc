/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Classify target exits and format their GDB exit codes and terminating signals.
 * RU: Определяет завершение цели и форматирует код выхода GDB и завершающий сигнал. */

/* EN: Return whether record reports target exit, independent of MI field order.
 * RU: Возвращает, сообщает ли record о завершении цели, независимо от порядка полей MI. */
static gboolean native_exit_record(const gchar *record)
{
    if (g_str_has_prefix(record, "=thread-group-exited,"))
        return TRUE;
    if (!g_str_has_prefix(record, "*stopped,"))
        return FALSE;
    gchar *reason = native_debug_field(record, "reason");
    gboolean exited = !g_strcmp0(reason, "exited") || !g_strcmp0(reason, "exited-normally") ||
                      !g_strcmp0(reason, "exited-signalled");
    g_free(reason);
    return exited;
}

/* EN: Describe exit record; caller frees the result. Return NULL for a group notification without
 * a code, so a preceding detailed exit survives. Decode GDB's octal codes and Windows hex codes.
 * RU: Описывает record завершения; результат освобождает вызывающий. Возвращает NULL для группового
 * уведомления без кода, сохраняя подробную причину. Читает восьмеричные коды GDB и hex-коды Windows. */
static gchar *native_exit_detail(const gchar *record)
{
    gchar *reason = native_debug_field(record, "reason");
    gchar *code = native_debug_field(record, "exit-code");
    gchar *result = NULL;
    if (!g_strcmp0(reason, "exited-signalled"))
    {
        gchar *name = native_debug_field(record, "signal-name");
        gchar *meaning = native_debug_field(record, "signal-meaning");
        result = g_strdup_printf("%s: %.100s%s%.200s", insight_tr("Program terminated by signal"),
                                 name ? name : "?", meaning && *meaning ? " — " : "", meaning ? meaning : "");
        g_free(name);
        g_free(meaning);
    }
    else if (!g_strcmp0(reason, "exited-normally"))
        result = g_strdup_printf(insight_tr("Program exited with code %u."), 0u);
    else if (code && *code && g_ascii_isdigit(*code))
    {
        gchar *end = NULL;
        guint64 number = g_ascii_strtoull(code, &end, 0);
        if (end && !*end && number <= G_MAXUINT32)
            result = g_strdup_printf(insight_tr("Program exited with code %u."), (guint)number);
    }
    if (!result && reason)
        result = g_strdup(insight_tr("Program exited."));
    g_free(reason);
    g_free(code);
    if (result)
    {
        gchar *valid = g_utf8_make_valid(result, -1);
        g_free(result);
        return valid;
    }
    return NULL;
}
