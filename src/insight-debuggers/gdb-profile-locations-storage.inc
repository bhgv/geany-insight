/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Serialize disabled C++ locations and track their new parent IDs during profile replay.
 * RU: Сохраняет выключенные подпозиции C++ и отслеживает новые ID родителей при восстановлении. */

/* EN: Append an owned copy of location to locations, bounded to a validation overflow sentinel.
 * RU: Добавляет копию location в locations с ограничением до признака превышения лимита проверки. */
static void native_saved_location_add(GPtrArray *locations, const NativeSavedLocation *location)
{
    if (locations->len >= 1025)
        return;
    NativeSavedLocation *copy = g_new0(NativeSavedLocation, 1);
    copy->file = g_strdup(location->file);
    copy->function = g_strdup(location->function);
    copy->line = location->line;
    g_ptr_array_add(locations, copy);
}

/* EN: Capture disabled child tuple only when it belongs to point; missing coordinates make saving fail validation.
 * RU: Сохраняет выключенный tuple только для родителя point; отсутствие координат запрещает сохранение проверкой. */
static void native_profile_location_capture(NativeSavedPoint *point, const gchar *tuple)
{
    if (!point)
        return;
    gchar *number = native_debug_field(tuple, "number");
    gchar *enabled = native_debug_field(tuple, "enabled");
    gchar *prefix = g_strconcat(point->number, ".", NULL);
    if (number && g_str_has_prefix(number, prefix) && !g_strcmp0(enabled, "n"))
    {
        NativeSavedLocation *location = native_profile_location_parse(tuple);
        if (location)
        {
            native_saved_location_add(point->disabled_locations, location);
            native_saved_location_free(location);
        }
        else
        {
            NativeSavedLocation invalid = {"", "", 0};
            native_saved_location_add(point->disabled_locations, &invalid);
        }
    }
    g_free(prefix);
    g_free(enabled);
    g_free(number);
}

/* EN: Save point's disabled selectors in profile under index, including unresolved selectors from session.
 * RU: Сохраняет выключенные селекторы point в profile под index, включая ожидающие правила session. */
static void native_profile_locations_store(NativeDebug *session, GKeyFile *profile,
                                           NativeSavedPoint *point, guint index)
{
    GPtrArray *locations = g_ptr_array_new_with_free_func(native_saved_location_free);
    for (guint i = 0; i < point->disabled_locations->len; i++)
        native_saved_location_add(locations, point->disabled_locations->pdata[i]);
    for (guint i = 0; i < session->profile_disabled_pending->len; i++)
    {
        NativeLocationOverride *override = session->profile_disabled_pending->pdata[i];
        if (!override->applied && !g_strcmp0(override->parent_number, point->number))
            native_saved_location_add(locations, override->location);
    }
    gchar *parent = g_strdup_printf("breakpoint-%u", index);
    g_key_file_set_integer(profile, parent, "disabled-count", locations->len);
    for (guint i = 0; i < locations->len; i++)
    {
        NativeSavedLocation *location = locations->pdata[i];
        gchar *group = g_strdup_printf("%s-location-%u", parent, i);
        g_key_file_set_string(profile, group, "file", location->file);
        g_key_file_set_string(profile, group, "function", location->function);
        g_key_file_set_integer(profile, group, "line", location->line);
        g_free(group);
    }
    g_free(parent);
    g_ptr_array_unref(locations);
}

/* EN: Load validated profile's disabled selectors into session, awaiting fresh parent IDs and symbols.
 * RU: Загружает проверенные селекторы profile в session до получения новых ID родителей и символов. */
static void native_profile_locations_load(NativeDebug *session, GKeyFile *profile, guint points)
{
    g_ptr_array_set_size(session->profile_disabled_pending, 0);
    session->profile_insert_next = 0;
    session->profile_insert_count = points;
    session->profile_insert_pending = FALSE;
    if (g_key_file_get_integer(profile, "debugger", "version", NULL) < 3)
        return;
    for (guint i = 0; i < points; i++)
    {
        gchar *parent = g_strdup_printf("breakpoint-%u", i);
        gint count = g_key_file_get_integer(profile, parent, "disabled-count", NULL);
        for (gint j = 0; j < count; j++)
        {
            gchar *group = g_strdup_printf("%s-location-%d", parent, j);
            NativeLocationOverride *override = g_new0(NativeLocationOverride, 1);
            override->point_index = i;
            override->location = g_new0(NativeSavedLocation, 1);
            override->location->file = g_key_file_get_string(profile, group, "file", NULL);
            override->location->function = g_key_file_get_string(profile, group, "function", NULL);
            override->location->line = g_key_file_get_integer(profile, group, "line", NULL);
            g_ptr_array_add(session->profile_disabled_pending, override);
            g_free(group);
        }
        g_free(parent);
    }
}

/* EN: Track profile insertion command in session before sending it, without relying on reusable GDB numbers.
 * RU: Отслеживает команду вставки command в session перед отправкой без зависимости от повторяемых номеров GDB. */
static void native_profile_command_sent(NativeDebug *session, const gchar *command)
{
    gboolean insertion = g_str_has_prefix(command, "-break-insert ") ||
        g_str_has_prefix(command, "-catch-throw") || g_str_has_prefix(command, "-catch-catch") ||
        g_str_has_prefix(command, "-catch-rethrow");
    if (session->profile_restoring && insertion &&
        session->profile_insert_next < session->profile_insert_count)
    {
        session->profile_insert_index = session->profile_insert_next++;
        session->profile_insert_pending = TRUE;
    }
}

/* EN: Associate pending profile selectors with the parent returned in session's insertion result record.
 * RU: Связывает ожидающие селекторы session с родителем из ответа вставки record. */
static void native_profile_location_parent_reply(NativeDebug *session, const gchar *record)
{
    if (!session->profile_insert_pending || *record != '^')
        return;
    session->profile_insert_pending = FALSE;
    gchar *number = g_str_has_prefix(record, "^done,bkpt=") ? native_debug_field(record, "number") : NULL;
    if (native_context_id(number))
    {
        if (session->profile_restore_data)
            native_profile_catch_settings(session->profile_restore_data, session->profile_insert_index,
                                          number, &session->launch_commands);
        for (guint i = 0; i < session->profile_disabled_pending->len; i++)
        {
            NativeLocationOverride *override = session->profile_disabled_pending->pdata[i];
            if (override->point_index == session->profile_insert_index)
            {
                g_free(override->parent_number);
                override->parent_number = g_strdup(number);
            }
        }
    }
    g_free(number);
}
