/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Keep confirmed source breakpoint recipes and restore them with watch expressions across sessions.
 * RU: Хранит подтверждённые настройки точек и восстанавливает их вместе с наблюдениями между сеансами. */

/* EN: Release the saved source breakpoint in data, including its owned strings.
 * RU: Освобождает сохранённую точку data вместе с принадлежащими ей строками. */
static void native_saved_point_free(gpointer data)
{
    NativeSavedPoint *point = data;
    g_free(point->filter);
    g_free(point->number);
    g_ptr_array_unref(point->disabled_locations);
    g_free(point->location);
    g_free(point->condition);
    g_free(point);
}

/* EN: Capture session's persistent parent source/exception points from successful record; omit data/temporary/address points.
 * RU: Сохраняет постоянные родительские точки исходников/исключений session из успешного record; пропускает точки данных, временные и адресные точки. */
static void native_profile_points_snapshot(NativeDebug *session, const gchar *record)
{
    const gchar *body = strstr(record, "body=[");
    if (!g_str_has_prefix(record, "^done") || !body)
        return;
    g_ptr_array_set_size(session->saved_points, 0);
    const gchar *cursor = body + strlen("body=["), *start, *end;
    NativeSavedPoint *current = NULL;
    guint scanned_points = 0;
    while (scanned_points++ < 4096 && session->saved_points->len < 257 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *number = native_debug_field(tuple, "number");
        gchar *kind = native_debug_field(tuple, "type");
        gchar *disposition = native_debug_field(tuple, "disp");
        gchar *location = native_debug_field(tuple, "original-location");
        gchar *event = native_debug_field(tuple, "catch-type");
        guint catch_kind = !g_strcmp0(event, "throw") ? 1 : !g_strcmp0(event, "catch") ? 2 :
                           !g_strcmp0(event, "rethrow") ? 3 : 0;
        g_free(event);
        gboolean source = !g_strcmp0(kind, "breakpoint") && location && *location && *location != '*';
        gboolean exception = !g_strcmp0(kind, "catchpoint") && catch_kind;
        if (native_context_id(number))
            current = NULL;
        if (native_context_id(number) && !g_strcmp0(disposition, "keep") && (source || exception))
        {
            NativeSavedPoint *point = g_new0(NativeSavedPoint, 1);
            point->number = g_strdup(number);
            point->catch_kind = exception ? catch_kind : 0;
            if (exception)
            {
                point->filter = native_debug_field(tuple, "regexp");
                if (!point->filter)
                    point->filter = g_strdup("");
            }
            point->disabled_locations = g_ptr_array_new_with_free_func(native_saved_location_free);
            current = point;
            if (source)
                point->location = g_steal_pointer(&location);
            point->condition = native_debug_field(tuple, "cond");
            if (!point->condition)
                point->condition = g_strdup("");
            gchar *enabled = native_debug_field(tuple, "enabled");
            point->enabled = g_strcmp0(enabled, "n") != 0;
            gchar *ignore = native_debug_field(tuple, "ignore");
            guint64 count = 0;
            if (ignore && g_ascii_string_to_unsigned(ignore, 10, 0, G_MAXINT, &count, NULL))
                point->ignore = (guint)count;
            g_ptr_array_add(session->saved_points, point);
            g_free(ignore);
            g_free(enabled);
        }
        native_profile_location_capture(current, tuple);
        const gchar *nested = strstr(tuple, "locations=[");
        if (nested)
        {
            nested += strlen("locations=[");
            const gchar *child_start, *child_end;
            guint scanned = 0;
            while (scanned++ < 4096 && (child_end = native_next_tuple(&nested, &child_start)))
            {
                gchar *child = g_strndup(child_start, child_end - child_start);
                native_profile_location_capture(current, child);
                g_free(child);
            }
        }
        g_free(location);
        g_free(disposition);
        g_free(kind);
        g_free(number);
        g_free(tuple);
    }
    guint unresolved = native_profile_locations_apply(session, record);
    if (session->profile_restoring && g_queue_is_empty(&session->launch_commands))
    {
        session->profile_restoring = FALSE;
        g_clear_pointer(&session->profile_restore_data, g_key_file_unref);
        if (unresolved && !session->profile_restore_failed)
        {
            gchar *message = g_strdup_printf(insight_tr("Profile restored; %u C++ locations await matching symbols."), unresolved);
            gtk_label_set_text(GTK_LABEL(session->remote->status), message);
            g_free(message);
        }
        else if (!session->profile_restore_failed)
            gtk_label_set_text(GTK_LABEL(session->remote->status),
                insight_tr("Debugger profile restored. Connect or Run explicitly."));
    }
}

/* EN: Add session's confirmed source point recipes and watch expressions to profile; values/hit totals are omitted.
 * RU: Добавляет подтверждённые точки и выражения session в profile; значения и число срабатываний не сохраняются. */
static void native_profile_debug_store(NativeDebug *session, GKeyFile *profile)
{
    g_key_file_set_integer(profile, "debugger", "watch-count", session->expressions->len);
    g_key_file_set_integer(profile, "debugger", "point-count", session->saved_points->len);
    for (guint i = 0; i < session->expressions->len; i++)
    {
        NativeWatch *watch = session->expressions->pdata[i];
        gchar *key = g_strdup_printf("expression-%u", i);
        g_key_file_set_string(profile, "watches", key, watch->expression);
        g_free(key);
    }
    for (guint i = 0; i < session->saved_points->len; i++)
    {
        NativeSavedPoint *point = session->saved_points->pdata[i];
        gchar *group = g_strdup_printf("breakpoint-%u", i);
        const gchar *kinds[] = {"source", "throw", "catch", "rethrow"};
        g_key_file_set_string(profile, group, "kind", kinds[point->catch_kind]);
        g_key_file_set_string(profile, group, point->catch_kind ? "filter" : "location",
                              point->catch_kind ? point->filter : point->location);
        g_key_file_set_string(profile, group, "condition", point->condition);
        g_key_file_set_boolean(profile, group, "enabled", point->enabled);
        g_key_file_set_integer(profile, group, "ignore", point->ignore);
        native_profile_locations_store(session, profile, point, i);
        g_free(group);
    }
}

/* EN: Restore validated profile into session without running the target; replace supported points and normalized watches.
 * Watch normalization matches manual editing so restored expressions remain removable by the same text.
 * RU: Восстанавливает проверенный profile в session без запуска; заменяет поддерживаемые точки и нормализованные наблюдения.
 * Нормализация совпадает с ручным вводом, поэтому загруженные выражения можно удалить тем же текстом. */
static void native_profile_debug_load(NativeDebug *session, GKeyFile *profile)
{
    if (g_key_file_get_integer(profile, "debugger", "version", NULL) < 2)
        return;
    gboolean exceptions = g_key_file_get_integer(profile, "debugger", "version", NULL) >= 4;
    for (guint i = 0; i < session->saved_points->len; i++)
    {
        NativeSavedPoint *point = session->saved_points->pdata[i];
        if (native_context_id(point->number) && (!point->catch_kind || exceptions))
            g_queue_push_tail(&session->launch_commands, g_strdup_printf("-break-delete %s", point->number));
    }
    g_clear_pointer(&session->profile_restore_data, g_key_file_unref);
    session->profile_restore_data = g_key_file_ref(profile);
    gint count = g_key_file_get_integer(profile, "debugger", "point-count", NULL);
    native_profile_locations_load(session, profile, (guint)count);
    for (gint i = 0; i < count; i++)
        g_queue_push_tail(&session->launch_commands, native_profile_point_command(profile, (guint)i));
    g_ptr_array_set_size(session->expressions, 0);
    count = g_key_file_get_integer(profile, "debugger", "watch-count", NULL);
    for (gint i = 0; i < count; i++)
    {
        NativeWatch *watch = g_new0(NativeWatch, 1);
        watch->expression = native_profile_watch_expression(profile, (guint)i);
        g_ptr_array_add(session->expressions, watch);
    }
    session->watch_next = 0;
    session->points_needed = session->profile_restoring = TRUE;
    session->profile_restore_failed = FALSE;
    native_watch_refresh(session, FALSE);
    gtk_label_set_text(GTK_LABEL(session->remote->status), insight_tr("Restoring saved breakpoints…"));
    native_debug_resume(session->window, session);
}

/* EN: Mark a failed restoration record in session, keeping the saved file intact and reporting the GDB error.
 * RU: Отмечает ошибку восстановления record в session, сохраняя файл и показывая ошибку GDB. */
static void native_profile_debug_reply(NativeDebug *session, const gchar *record)
{
    native_profile_location_parent_reply(session, record);
    if (session->profile_restoring && g_str_has_prefix(record, "^error"))
    {
        session->profile_restore_failed = TRUE;
        session->profile_restoring = FALSE;
        g_clear_pointer(&session->profile_restore_data, g_key_file_unref);
        gtk_label_set_text(GTK_LABEL(session->remote->status),
            insight_tr("Some breakpoints could not be restored. See the GDB console."));
    }
}
