/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Validate saved source breakpoints/watches and construct bounded MI restoration commands.
 * RU: Проверяет сохранённые точки/наблюдения и создаёт ограниченные команды восстановления MI. */

static gboolean native_profile_locations_validate(GKeyFile *profile, gint points, GError **error);

/* EN: Check value's UTF-8 length and single-line shape against limit; NULL is invalid.
 * RU: Проверяет UTF-8, длину и однострочность value по limit; NULL недопустим. */
static gboolean native_profile_text_valid(const gchar *value, glong limit)
{
    return value && strlen(value) <= (gsize)limit * 4 && g_utf8_validate(value, -1, NULL) &&
        g_utf8_strlen(value, -1) <= limit && !strpbrk(value, "\r\n");
}

/* EN: Validate profile's version-two source-point/watch section; return FALSE with error before applying it.
 * RU: Проверяет секцию точек/наблюдений версии два в profile; ошибка даёт FALSE и error до применения. */
static gboolean native_profile_debug_validate(GKeyFile *profile, GError **error)
{
    GError *failure = NULL;
    gint watches = g_key_file_get_integer(profile, "debugger", "watch-count", &failure);
    gint points = failure ? -1 : g_key_file_get_integer(profile, "debugger", "point-count", &failure);
    gboolean valid = !failure && watches >= 0 && watches <= 32 && points >= 0 && points <= 256;
    for (gint i = 0; valid && i < watches; i++)
    {
        gchar *key = g_strdup_printf("expression-%d", i);
        gchar *value = g_key_file_get_string(profile, "watches", key, NULL);
        valid = native_profile_text_valid(value, 1024) && *value;
        g_free(value);
        g_free(key);
    }
    for (gint i = 0; valid && i < points; i++)
    {
        gchar *group = g_strdup_printf("breakpoint-%d", i);
        gchar *location = g_key_file_get_string(profile, group, "location", NULL);
        gchar *condition = g_key_file_get_string(profile, group, "condition", NULL);
        valid = native_profile_text_valid(location, 4096) && *location && *location != '*' &&
                native_profile_text_valid(condition, 8192);
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

/* EN: Return an owned MI command for validated profile point index, preserving condition, enabled state and ignore count.
 * RU: Возвращает команду MI проверенной точки index из profile с условием, состоянием и счётчиком пропусков. */
static gchar *native_profile_point_command(GKeyFile *profile, guint index)
{
    gchar *group = g_strdup_printf("breakpoint-%u", index);
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
