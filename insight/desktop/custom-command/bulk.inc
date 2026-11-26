/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Expand bounded open/modified-file arguments without shell reparsing.
 * RU: Раскрывает ограниченные списки открытых/изменённых файлов без повторного разбора оболочкой. */

/* EN: Release owned file values and file; NULL is accepted.
 * RU: Освобождает значения файла file и сам объект; NULL допустим. */
void insight_custom_file_free(gpointer file)
{
    InsightCustomFile *item = file;
    if (!item)
        return;
    g_hash_table_unref(item->values);
    g_free(item);
}

/* EN: Find at most one unescaped bulk marker in argument; write offset/modified.
 * Return FALSE and set error for ambiguous multiple markers, without changing argument.
 * RU: Находит не более одного неэкранированного маркера в argument; записывает offset/modified.
 * При нескольких маркерах возвращает FALSE с error, не меняя argument. */
static gboolean bulk_marker(const gchar *argument, gssize *offset,
    gboolean *modified, GError **error)
{
    *offset = -1;
    for (const gchar *at = argument; *at;)
    {
        if (*at != '%')
        {
            at++;
            continue;
        }
        if (at[1] == '%')
        {
            at += 2;
            continue;
        }
        const gchar *end = strchr(at + 1, '%');
        gboolean long_name = end && end - at > 2 && end - at <= 65;
        if (long_name)
        {
            for (const gchar *p = at + 1; p < end; p++)
                if (!g_ascii_isalnum(*p) && *p != '_')
                    long_name = FALSE;
        }
        if (long_name)
        {
            at = end + 1;
            continue;
        }
        if (at[1] && strchr("frnbe", at[1]) && at[2] == '%' &&
            (at[3] == 'o' || at[3] == 'm'))
        {
            if (*offset >= 0)
                return fail(error, "Use one bulk file modifier per argument");
            *offset = at - argument;
            *modified = at[3] == 'm';
            at += 4;
        }
        else
            at += at[1] ? 2 : 1;
    }
    return TRUE;
}

/* EN: Append owned text to result if argument/total bounds permit; always consumes text.
 * total tracks UTF-8 bytes including NUL terminators; errors leave previous result entries owned by result.
 * RU: Добавляет собственный text в result при соблюдении границ; всегда принимает владение text.
 * total считает байты UTF-8 с NUL; при ошибке прежние элементы остаются во владении result. */
static gboolean bulk_append(GPtrArray *result, gchar *text, gsize *total, GError **error)
{
    if (!text)
        return FALSE;
    gsize bytes = strlen(text) + 1;
    if (result->len >= 128 || bytes > 65537 || *total + bytes > 256u * 1024u)
    {
        g_free(text);
        return fail(error, "Expanded custom command is too large");
    }
    *total += bytes;
    g_ptr_array_add(result, text);
    return TRUE;
}

/* EN: Expand argument with context values overridden by this file's filename values.
 * Both maps are borrowed and unchanged; return owned text or NULL/error.
 * RU: Раскрывает argument с заменой файловых значений context значениями file.
 * Обе таблицы заимствованы и неизменны; возвращает собственный текст либо NULL/error. */
static gchar *bulk_argument(const gchar *argument, GHashTable *context,
    InsightCustomFile *file, GError **error)
{
    const gchar *required[] = {"f", "r", "n", "b", "e"};
    for (guint i = 0; i < G_N_ELEMENTS(required); i++)
    {
        if (!g_hash_table_contains(file->values, required[i]))
        {
            fail(error, "Custom command variable is unavailable");
            return NULL;
        }
    }
    GHashTable *values = g_hash_table_new(g_str_hash, g_str_equal);
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, context);
    while (g_hash_table_iter_next(&iter, &key, &value))
        g_hash_table_insert(values, key, value);
    g_hash_table_iter_init(&iter, file->values);
    while (g_hash_table_iter_next(&iter, &key, &value))
        g_hash_table_insert(values, key, value);
    gchar *text = insight_custom_expand(argument, values, error);
    g_hash_table_unref(values);
    return text;
}

/* EN: Parse input once, then expand each bulk argument per borrowed file snapshot.
 * values holds current context, files holds InsightCustomFile entries in document order.
 * Return an owned NULL-terminated argv or NULL/error; never launch, save or modify either input map.
 * RU: Однократно разбирает input, затем раскрывает массовые аргументы по заимствованному снимку files.
 * values содержит текущий контекст, files — InsightCustomFile в порядке документов.
 * Возвращает собственный argv с NULL либо NULL/error; ничего не запускает, не сохраняет и не меняет таблицы. */
gchar **insight_custom_argv_files(const gchar *input, GHashTable *values,
    GPtrArray *files, GError **error)
{
    if (!values || !field(input, 16384))
    {
        fail(error, "Invalid custom command template");
        return NULL;
    }
    gint count = 0;
    gchar **parsed = NULL;
    if (!g_shell_parse_argv(input, &count, &parsed, error))
        return NULL;
    GPtrArray *result = g_ptr_array_new_with_free_func(g_free);
    gsize total = 0;
    gboolean ok = count <= 128;
    if (!ok)
        fail(error, "Custom command has too many arguments");
    for (gint i = 0; ok && i < count; i++)
    {
        gssize offset;
        gboolean modified = FALSE;
        ok = bulk_marker(parsed[i], &offset, &modified, error);
        if (!ok)
            break;
        if (offset < 0)
        {
            ok = bulk_append(result, insight_custom_expand(parsed[i], values, error), &total, error);
            continue;
        }
        if (i == 0)
        {
            ok = fail(error, "Bulk file modifiers cannot select the executable");
            break;
        }
        gchar *prefix = g_strndup(parsed[i], offset + 2);
        gchar *argument = g_strconcat(prefix, parsed[i] + offset + 4, NULL);
        g_free(prefix);
        guint selected = 0;
        for (guint n = 0; ok && files && n < files->len; n++)
        {
            InsightCustomFile *file = g_ptr_array_index(files, n);
            if (modified && !file->modified)
                continue;
            selected++;
            ok = bulk_append(result, bulk_argument(argument, values, file, error), &total, error);
        }
        g_free(argument);
        if (ok && !selected)
            ok = fail(error, modified ? "No named modified files are open" : "No named files are open");
    }
    g_strfreev(parsed);
    if (ok && (!result->len || !*(gchar *)g_ptr_array_index(result, 0)))
        ok = fail(error, "Custom command executable is empty");
    if (!ok)
    {
        g_ptr_array_unref(result);
        return NULL;
    }
    g_ptr_array_add(result, NULL);
    return (gchar **)g_ptr_array_free(result, FALSE);
}
