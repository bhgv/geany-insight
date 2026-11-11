/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Identify persisted source/exception recipes and configure fresh catchpoint IDs during replay.
 * RU: Различает сохранённые точки исходников/исключений и настраивает новые ID при восстановлении. */

/* EN: Return 0 for a legacy/source point, 1..3 for exception events, or 4 for an invalid profile kind at index.
 * RU: Возвращает 0 для старой/исходной точки, 1..3 для исключений либо 4 для неверного вида точки index в profile. */
static guint native_profile_point_kind(GKeyFile *profile, guint index)
{
    if (g_key_file_get_integer(profile, "debugger", "version", NULL) < 4)
        return 0;
    gchar *group = g_strdup_printf("breakpoint-%u", index);
    gchar *kind = g_key_file_get_string(profile, group, "kind", NULL);
    const gchar *names[] = {"source", "throw", "catch", "rethrow"};
    guint result = G_N_ELEMENTS(names);
    for (guint i = 0; i < G_N_ELEMENTS(names); i++)
        if (!g_strcmp0(kind, names[i]))
            result = i;
    g_free(kind);
    g_free(group);
    return result;
}

/* EN: Prepend validated profile point index's settings to commands using the confirmed new parent number.
 * RU: Добавляет настройки проверенной точки index из profile в начало commands с новым подтверждённым number. */
static void native_profile_catch_settings(GKeyFile *profile, guint index, const gchar *number, GQueue *commands)
{
    guint kind = native_profile_point_kind(profile, index);
    if (!kind || kind > 3 || !native_context_id(number))
        return;
    gchar *group = g_strdup_printf("breakpoint-%u", index);
    gchar *condition = g_key_file_get_string(profile, group, "condition", NULL);
    gint ignore = g_key_file_get_integer(profile, group, "ignore", NULL);
    if (ignore)
        g_queue_push_head(commands, g_strdup_printf("-break-after %s %d", number, ignore));
    if (condition && *condition)
    {
        gchar *escaped = g_strescape(condition, NULL);
        g_queue_push_head(commands, g_strdup_printf("-break-condition %s \"%s\"", number, escaped));
        g_free(escaped);
    }
    if (!g_key_file_get_boolean(profile, group, "enabled", NULL))
        g_queue_push_head(commands, g_strdup_printf("-break-disable %s", number));
    g_free(condition);
    g_free(group);
}
