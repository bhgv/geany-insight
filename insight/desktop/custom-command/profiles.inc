/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Persist custom-command profiles with backward-compatible diagnostic options and atomic writes.
 * RU: Сохраняет профили команд с совместимыми настройками диагностики и атомарной записью. */

/* EN: Release owned command data; NULL is safe.
 * RU: Освобождает собственные данные команды data; NULL безопасен. */
void insight_custom_command_free(gpointer data)
{
    InsightCustomCommand *command = data;
    if (!command)
        return;
    g_free(command->name);
    g_free(command->command);
    g_free(command->directory);
    g_free(command->diagnostic_pattern);
    g_free(command->shortcut);
    g_free(command);
}

/* EN: Clone all fields of borrowed command; return owned copy, including optional diagnostic configuration.
 * RU: Копирует все поля заимствованного command; возвращает собственную копию с настройками диагностики. */
InsightCustomCommand *insight_custom_command_copy(const InsightCustomCommand *command)
{
    InsightCustomCommand *copy = g_new0(InsightCustomCommand, 1);
    copy->name = g_strdup(command->name);
    copy->command = g_strdup(command->command);
    copy->directory = g_strdup(command->directory);
    copy->diagnostic_pattern = g_strdup(command->diagnostic_pattern);
    copy->parse_on_finish = command->parse_on_finish;
    copy->shortcut = g_strdup(command->shortcut);
    return copy;
}

/* EN: Validate command fields and optional named-group pattern; return TRUE when valid, FALSE/error otherwise, without edits.
 * RU: Проверяет поля command и необязательный именованный шаблон; возвращает TRUE при успехе либо FALSE/error, без правок. */
static gboolean command_valid(InsightCustomCommand *command, GError **error)
{
    if (!command || !field(command->name, 128) || !*command->name ||
        !field(command->command, 16384) || !*command->command || !field(command->directory, 16384) || !field(command->shortcut ? command->shortcut : "", 128))
        return fail(error, "Invalid saved custom command");
    GRegex *regex = insight_custom_diagnostic_regex(command->diagnostic_pattern, error);
    if (command->diagnostic_pattern && *command->diagnostic_pattern && !regex)
        return FALSE;
    if (regex)
        g_regex_unref(regex);
    return TRUE;
}

/* EN: Read one indexed command from key; old profiles default to built-in/manual parsing. Return owned command or NULL/error.
 * RU: Читает команду index из key; старые профили получают встроенный ручной разбор. Собственная команда либо NULL/error. */
static InsightCustomCommand *command_read(GKeyFile *key, guint index, GError **error)
{
    gchar *group = g_strdup_printf("command-%u", index);
    InsightCustomCommand *command = g_new0(InsightCustomCommand, 1);
    command->name = g_key_file_get_string(key, group, "name", NULL);
    command->command = g_key_file_get_string(key, group, "command", NULL);
    command->directory = g_key_file_get_string(key, group, "directory", NULL);
    GError *local = NULL;
    if (g_key_file_has_key(key, group, "diagnostic-pattern", NULL))
        command->diagnostic_pattern = g_key_file_get_string(key, group, "diagnostic-pattern", &local);
    if (!local && g_key_file_has_key(key, group, "shortcut", NULL))
        command->shortcut = g_key_file_get_string(key, group, "shortcut", &local);
    if (!local && g_key_file_has_key(key, group, "parse-on-finish", NULL))
        command->parse_on_finish = g_key_file_get_boolean(key, group, "parse-on-finish", &local);
    g_free(group);
    if (local)
    {
        g_propagate_error(error, local);
        insight_custom_command_free(command);
        return NULL;
    }
    if (!command_valid(command, error))
    {
        insight_custom_command_free(command);
        return NULL;
    }
    return command;
}

/* EN: Read bounded UTF-8 profile list from path; missing files yield an owned empty list, malformed settings return NULL/error.
 * RU: Читает ограниченный список UTF-8 из path; отсутствие даёт собственный пустой список, неверные настройки — NULL/error. */
GPtrArray *insight_custom_commands_load(const gchar *path, GError **error)
{
    GPtrArray *list = g_ptr_array_new_with_free_func(insight_custom_command_free);
    GFile *file = g_file_new_for_path(path);
    GError *local = NULL;
    GFileInputStream *stream = g_file_read(file, NULL, &local);
    g_object_unref(file);
    if (!stream)
    {
        if (g_error_matches(local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
            g_clear_error(&local);
            return list;
        }
        g_propagate_error(error, local);
        g_ptr_array_unref(list);
        return NULL;
    }
    gchar *text = g_malloc(LIMIT + 1);
    gsize length = 0;
    gboolean ok = g_input_stream_read_all(G_INPUT_STREAM(stream), text, LIMIT + 1, &length, NULL, error);
    g_object_unref(stream);
    if (ok && length > LIMIT)
        ok = fail(error, "Custom command settings exceed the size limit");
    GKeyFile *key = g_key_file_new();
    if (ok)
        ok = g_key_file_load_from_data(key, text, length, G_KEY_FILE_NONE, error);
    g_free(text);
    gint count = 0;
    if (ok)
    {
        GError *failure = NULL;
        count = g_key_file_get_integer(key, "commands", "count", &failure);
        if (failure)
        {
            g_propagate_error(error, failure);
            ok = FALSE;
        }
    }
    if (ok && (count < 0 || count > 64))
        ok = fail(error, "Custom command count exceeds the limit");
    for (gint i = 0; ok && i < count; i++)
    {
        InsightCustomCommand *command = command_read(key, i, error);
        if (!command)
            ok = FALSE;
        else
            g_ptr_array_add(list, command);
    }
    g_key_file_unref(key);
    if (!ok)
    {
        g_ptr_array_unref(list);
        return NULL;
    }
    return list;
}

/* EN: Validate list before atomically replacing private settings at path; report errors and preserve existing settings on failure.
 * RU: Проверяет list перед атомарной частной заменой настроек path; при ошибке сообщает error, сохраняя прежний файл. */
gboolean insight_custom_commands_save(const gchar *path, GPtrArray *list, GError **error)
{
    if (!list || list->len > 64)
        return fail(error, "Custom command count exceeds the limit");
    GKeyFile *key = g_key_file_new();
    g_key_file_set_integer(key, "commands", "count", list->len);
    gboolean ok = TRUE;
    for (guint i = 0; ok && i < list->len; i++)
    {
        InsightCustomCommand *command = g_ptr_array_index(list, i);
        ok = command_valid(command, error);
        if (!ok)
            break;
        gchar *group = g_strdup_printf("command-%u", i);
        g_key_file_set_string(key, group, "name", command->name);
        g_key_file_set_string(key, group, "command", command->command);
        g_key_file_set_string(key, group, "directory", command->directory);
        g_key_file_set_string(key, group, "diagnostic-pattern", command->diagnostic_pattern ? command->diagnostic_pattern : "");
        g_key_file_set_boolean(key, group, "parse-on-finish", command->parse_on_finish);
        g_key_file_set_string(key, group, "shortcut", command->shortcut ? command->shortcut : "");
        g_free(group);
    }
    gsize length = 0;
    gchar *text = ok ? g_key_file_to_data(key, &length, error) : NULL;
    g_key_file_unref(key);
    if (text && length > LIMIT)
        ok = fail(error, "Custom command settings exceed the size limit");
    if (ok && text)
    {
        GFile *file = g_file_new_for_path(path);
        ok = g_file_replace_contents(file, text, length, NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL, error);
        g_object_unref(file);
    }
    else
        ok = FALSE;
    g_free(text);
    return ok;
}
