/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Validate saved source/exception breakpoints and watches and construct bounded MI restoration commands.
 * RU: Проверяет сохранённые точки/наблюдения и создаёт ограниченные команды восстановления MI. */

static gboolean native_profile_locations_validate(GKeyFile *profile, gint points, GError **error);

/* EN: Check value's UTF-8 length and single-line shape against limit; NULL is invalid.
 * RU: Проверяет UTF-8, длину и однострочность value по limit; NULL недопустим. */
static gboolean native_profile_text_valid(const gchar *value, glong limit)
{
    return value && strlen(value) <= (gsize)limit * 4 && g_utf8_validate(value, -1, NULL) &&
        g_utf8_strlen(value, -1) <= limit && !strpbrk(value, "\r\n");
}

/* EN: Return profile's owned, outer-trimmed watch expression at index; missing/multiline values return NULL.
 * Internal whitespace and quoted strings are preserved; the caller validates UTF-8 and length.
 * RU: Возвращает выражение наблюдения profile с индексом index без внешних пробелов; отсутствующая/многострочная запись даёт NULL.
 * Внутренние пробелы и строки в кавычках сохраняются; вызывающий проверяет UTF-8 и длину. */
static gchar *native_profile_watch_expression(GKeyFile *profile, guint index)
{
    gchar *key = g_strdup_printf("expression-%u", index);
    gchar *expression = g_key_file_get_string(profile, "watches", key, NULL);
    g_free(key);
    if (expression && strpbrk(expression, "\r\n"))
    {
        g_free(expression);
        return NULL;
    }
    return expression ? g_strstrip(expression) : NULL;
}

/* EN: Validate profile's point/watch section, requiring unique nonempty normalized watch expressions.
 * Return FALSE with error before applying invalid counts, fields or duplicate identities.
 * RU: Проверяет секцию точек/наблюдений profile, требуя уникальных непустых нормализованных выражений.
 * Возвращает FALSE с error до применения неверных чисел, полей либо повторяющихся выражений. */
static gboolean native_profile_debug_validate(GKeyFile *profile, GError **error)
{
    GError *failure = NULL;
    gint watches = g_key_file_get_integer(profile, "debugger", "watch-count", &failure);
    gint points = failure ? -1 : g_key_file_get_integer(profile, "debugger", "point-count", &failure);
    gboolean valid = !failure && watches >= 0 && watches <= 32 && points >= 0 && points <= 256;
    for (gint i = 0; valid && i < watches; i++)
    {
        gchar *value = native_profile_watch_expression(profile, (guint)i);
        valid = native_profile_text_valid(value, 1024) && *value;
        for (gint previous_index = 0; valid && previous_index < i; previous_index++)
        {
            gchar *previous = native_profile_watch_expression(profile, (guint)previous_index);
            valid = g_strcmp0(value, previous) != 0;
            g_free(previous);
        }
        g_free(value);
    }
    for (gint i = 0; valid && i < points; i++)
    {
        gchar *group = g_strdup_printf("breakpoint-%d", i);
        guint kind = native_profile_point_kind(profile, (guint)i);
        gchar *location = g_key_file_get_string(profile, group, kind ? "filter" : "location", NULL);
        gchar *condition = g_key_file_get_string(profile, group, "condition", NULL);
        valid = kind < 4 && native_profile_text_valid(location, kind ? 1024 : 4096) &&
                (kind || (*location && *location != '*')) && native_profile_text_valid(condition, 8192);
        gint ignore = g_key_file_get_integer(profile, group, "ignore", &failure);
        if (!failure)
            g_key_file_get_boolean(profile, group, "enabled", &failure);
        valid = valid && !failure && ignore >= 0;
        g_free(condition);
        g_free(location);
        g_free(group);
    }
    g_clear_error(&failure);
    if (!valid)
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            insight_tr("Invalid saved breakpoints or watches (limits: 256 points, 32 watches)."));
    if (valid && g_key_file_get_integer(profile, "debugger", "version", NULL) >= 3)
        valid = native_profile_locations_validate(profile, points, error);
    return valid;
}

/* EN: Return an owned creation command for validated point index in profile; catchpoint settings follow its confirmed ID.
 * RU: Возвращает команду создания проверенной точки index из profile; настройки исключения следуют за подтверждением ID. */
static gchar *native_profile_point_command(GKeyFile *profile, guint index)
{
    gchar *group = g_strdup_printf("breakpoint-%u", index);
    guint kind = native_profile_point_kind(profile, index);
    if (kind)
    {
        gchar *filter = g_key_file_get_string(profile, group, "filter", NULL);
        gchar *command = native_catch_command(kind - 1, filter);
        g_free(filter);
        g_free(group);
        return command;
    }
    gchar *location = g_key_file_get_string(profile, group, "location", NULL);
    gchar *condition = g_key_file_get_string(profile, group, "condition", NULL);
    gboolean enabled = g_key_file_get_boolean(profile, group, "enabled", NULL);
    gint ignore = g_key_file_get_integer(profile, group, "ignore", NULL);
    GString *command = g_string_new("-break-insert -f");
    if (!enabled)
        g_string_append(command, " -d");
    if (*condition)
    {
        gchar *escaped = g_strescape(condition, NULL);
        g_string_append_printf(command, " -c \"%s\"", escaped);
        g_free(escaped);
    }
    gchar *escaped = g_strescape(location, NULL);
    g_string_append_printf(command, " -i %d -- \"%s\"", ignore, escaped);
    g_free(escaped);
    g_free(condition);
    g_free(location);
    g_free(group);
    return g_string_free(command, FALSE);
}
