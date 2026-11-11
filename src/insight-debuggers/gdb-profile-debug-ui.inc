/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Keep confirmed source breakpoint recipes and restore them with watch expressions across sessions.
 * RU: Хранит подтверждённые настройки точек и восстанавливает их вместе с наблюдениями между сеансами. */

/* EN: Release the saved source breakpoint in data, including its owned strings.
 * RU: Освобождает сохранённую точку data вместе с принадлежащими ей строками. */
static void native_saved_point_free(gpointer data)
{
    NativeSavedPoint *point = data;
    g_free(point->number);
    g_ptr_array_unref(point->disabled_locations);
    g_free(point->location);
    g_free(point->condition);
    g_free(point);
}

/* EN: Capture session's persistent parent source points from successful record; omit runtime/temporary/address points.
 * RU: Сохраняет постоянные родительские точки session из успешного record; пропускает временные и адресные точки. */
static void native_profile_points_snapshot(NativeDebug *session, const gchar *record)
{
    const gchar *body = strstr(record, "body=[");
    if (!g_str_has_prefix(record, "^done") || !body)
        return;
    g_ptr_array_set_size(session->saved_source_points, 0);
    const gchar *cursor = body + strlen("body=["), *start, *end;
    NativeSavedPoint *current = NULL;
    while (session->saved_source_points->len < 257 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *number = native_debug_field(tuple, "number");
        gchar *kind = native_debug_field(tuple, "type");
        gchar *disposition = native_debug_field(tuple, "disp");
        gchar *location = native_debug_field(tuple, "original-location");
        if (native_context_id(number))
            current = NULL;
        if (native_context_id(number) && !g_strcmp0(kind, "breakpoint") &&
            !g_strcmp0(disposition, "keep") && location && *location && *location != '*')
        {
            NativeSavedPoint *point = g_new0(NativeSavedPoint, 1);
            point->number = g_strdup(number);
            point->disabled_locations = g_ptr_array_new_with_free_func(native_saved_location_free);
            current = point;
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
            g_ptr_array_add(session->saved_source_points, point);
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
    g_key_file_set_integer(profile, "debugger", "point-count", session->saved_source_points->len);
    for (guint i = 0; i < session->expressions->len; i++)
    {
        NativeWatch *watch = session->expressions->pdata[i];
        gchar *key = g_strdup_printf("expression-%u", i);
        g_key_file_set_string(profile, "watches", key, watch->expression);
        g_free(key);
    }
    for (guint i = 0; i < session->saved_source_points->len; i++)
    {
        NativeSavedPoint *point = session->saved_source_points->pdata[i];
        gchar *group = g_strdup_printf("breakpoint-%u", i);
        g_key_file_set_string(profile, group, "location", point->location);
        g_key_file_set_string(profile, group, "condition", point->condition);
        g_key_file_set_boolean(profile, group, "enabled", point->enabled);
        g_key_file_set_integer(profile, group, "ignore", point->ignore);
        native_profile_locations_store(session, profile, point, i);
        g_free(group);
    }
}

/* EN: Restore validated profile into session without running the target; replace source points and watches.
 * RU: Восстанавливает проверенный профиль в session без запуска; заменяет точки исходников и наблюдения. */
static void native_profile_debug_load(NativeDebug *session, GKeyFile *profile)
{
    if (g_key_file_get_integer(profile, "debugger", "version", NULL) < 2)
        return;
    GtkTreeModel *model = GTK_TREE_MODEL(session->point_rows);
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *number = NULL, *kind = NULL;
        gtk_tree_model_get(model, &row, 1, &number, 8, &kind, -1);
        if (native_context_id(number))
            current = NULL;
        if (native_context_id(number) && !g_strcmp0(kind, "breakpoint"))
            g_queue_push_tail(&session->launch_commands, g_strdup_printf("-break-delete %s", number));
        g_free(kind);
        g_free(number);
    }
    gint count = g_key_file_get_integer(profile, "debugger", "point-count", NULL);
    native_profile_locations_load(session, profile, (guint)count);
    for (gint i = 0; i < count; i++)
        g_queue_push_tail(&session->launch_commands, native_profile_point_command(profile, (guint)i));
    g_ptr_array_set_size(session->expressions, 0);
    count = g_key_file_get_integer(profile, "debugger", "watch-count", NULL);
    for (gint i = 0; i < count; i++)
    {
        gchar *key = g_strdup_printf("expression-%d", i);
        NativeWatch *watch = g_new0(NativeWatch, 1);
        watch->expression = g_key_file_get_string(profile, "watches", key, NULL);
        g_ptr_array_add(session->expressions, watch);
        g_free(key);
    }
    session->watch_next = 0;
    session->points_needed = session->profile_restoring = TRUE;
    session->profile_restore_failed = FALSE;
    native_watch_refresh(session, FALSE);
    gtk_label_set_text(GTK_LABEL(session->remote->status), insight_tr("Restoring saved source breakpoints…"));
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
        gtk_label_set_text(GTK_LABEL(session->remote->status),
            insight_tr("Some source breakpoints could not be restored. See the GDB console."));
    }
}
