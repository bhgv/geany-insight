/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Integrate remote connect/detach/run with the native GDB command queue and control state.
 * RU: Связывает удалённое подключение, отсоединение и запуск с очередью GDB и состоянием кнопок. */

/* EN: Return whether session's connection form selects either SSH mode.
 * RU: Возвращает, выбран ли в форме session один из режимов SSH. */
static gboolean native_remote_selected(NativeDebug *session)
{
    return session->remote && gtk_combo_box_get_active(GTK_COMBO_BOX(session->remote->mode)) > 0;
}

/* EN: Update session's connection controls without changing its stored settings.
 * RU: Обновляет элементы подключения session, не меняя сохранённые настройки. */
static void native_remote_update(NativeDebug *session)
{
    NativeRemote *remote = session->remote;
    if (!remote)
        return;
    gboolean idle = session->ready && !session->pending && !session->writing && !remote->busy;
    native_attach_update(session, idle);
    gtk_widget_set_sensitive(remote->mode, idle && !remote->ssh && !session->running && !session->stopped);
    gtk_widget_set_sensitive(remote->form, idle && !remote->ssh && !session->running && !session->stopped);
    gtk_widget_set_sensitive(remote->profile_controls, idle && !session->running);
    gtk_widget_set_sensitive(remote->profile_save, idle && !session->running);
    gtk_widget_set_sensitive(remote->profile_restore, idle && !remote->ssh && !session->running && !session->stopped);
    gtk_widget_set_sensitive(remote->profile_forget, idle && !remote->ssh && !session->running && !session->stopped);
    gtk_widget_set_sensitive(remote->connect, idle && native_remote_selected(session) &&
        !remote->ssh && !session->running && !session->stopped);
    gtk_widget_set_sensitive(remote->disconnect, remote->ssh &&
        ((remote->busy && !remote->connected) || (idle && !session->stop_requested)));
    if (remote->busy)
    {
        for (guint i = 0; i < NATIVE_ACTION_COUNT; i++)
            gtk_widget_set_sensitive(session->buttons[i], FALSE);
        gtk_widget_set_sensitive(session->command, FALSE);
        gtk_widget_set_sensitive(session->point_controls, FALSE);
        gtk_widget_set_sensitive(session->breakpoint, FALSE);
    }
    if (native_remote_selected(session) && (!remote->connected || !remote->extended))
    {
        gtk_widget_set_sensitive(session->buttons[NATIVE_RUN], FALSE);
        gtk_widget_set_sensitive(session->buttons[NATIVE_RESTART], FALSE);
    }
}

/* EN: Return session's next detach/disconnect request, owned by the caller, or NULL.
 * RU: Возвращает следующую команду отсоединения session, принадлежащую вызывающему, либо NULL. */
static gchar *native_remote_next(NativeDebug *session)
{
    NativeRemote *remote = session->remote;
    if (!remote)
        return NULL;
    if (remote->detach_needed)
    {
        remote->detach_needed = FALSE;
        remote->pending = NATIVE_REMOTE_DETACH;
        return g_strdup("-target-detach");
    }
    if (remote->disconnect_needed)
    {
        remote->disconnect_needed = FALSE;
        remote->pending = NATIVE_REMOTE_DISCONNECT;
        return g_strdup("-target-disconnect");
    }
    return NULL;
}

/* EN: Restore only session-owned source substitution and the initial local sysroot after remote use.
 * RU: Восстанавливает только сопоставление session и исходный локальный sysroot после удалённой работы. */
static void native_remote_restore(NativeDebug *session)
{
    NativeRemote *remote = session->remote;
    if (remote->original_sysroot)
    {
        gchar *setting = native_remote_setting("sysroot", remote->original_sysroot);
        if (setting)
            g_queue_push_tail(&session->launch_commands, setting);
    }
    if (remote->source_mapping)
        native_remote_mapping(&session->launch_commands, remote->source_mapping, NULL);
    g_clear_pointer(&remote->source_mapping, g_free);
}

/* EN: Consume record for session's remote operation; detach resumes the inferior before closing SSH.
 * RU: Принимает record удалённой операции session; detach продолжает программу перед закрытием SSH. */
static gboolean native_remote_reply(NativeDebug *session, const gchar *record)
{
    NativeRemote *remote = session->remote;
    if (!remote)
        return FALSE;
    if (remote->pending == NATIVE_REMOTE_NONE)
    {
        if (remote->busy && !remote->timer && g_str_has_prefix(record, "^error"))
        {
            gchar *message = native_debug_field(record, "msg");
            gtk_label_set_text(GTK_LABEL(remote->status), message ? message : insight_tr("Remote connection failed."));
            g_free(message);
            insight_queue_clear_full(&session->launch_commands, g_free);
            native_remote_drop(session);
            native_remote_restore(session);
            return TRUE;
        }
        return FALSE;
    }
    NativeRemoteRequest request = remote->pending;
    remote->pending = NATIVE_REMOTE_NONE;
    gboolean success = g_str_has_prefix(record, "^connected") || g_str_has_prefix(record, "^done");
    if (request == NATIVE_REMOTE_SYSROOT)
    {
        g_free(remote->original_sysroot);
        remote->original_sysroot = success ? native_debug_field(record, "value") : NULL;
        return TRUE;
    }
    if (request == NATIVE_REMOTE_CONNECT && success)
    {
        remote->busy = FALSE;
        remote->connected = TRUE;
        session->points_needed = TRUE;
        if (!remote->extended && !session->stopped)
        {
            /* EN: Some GDB versions report the initial remote stop only in ^connected.
             * RU: Некоторые версии GDB сообщают начальную остановку только через ^connected. */
            session->running = FALSE;
            session->stopped = TRUE;
            native_values_reset(session);
            session->threads_needed = session->stack_needed = session->locals_needed = TRUE;
            session->watch_next = 0;
            native_debug_observe(session, record);
        }
        gtk_label_set_text(GTK_LABEL(remote->status), insight_tr(remote->extended ?
            "SSH gdbserver connected. Run starts the remote executable." :
            "SSH gdbserver connected. Continue resumes the remote program."));
    }
    else if (request == NATIVE_REMOTE_ATTACH)
    {
        if (success)
        {
            session->running = FALSE;
            session->stopped = TRUE;
            session->points_needed = TRUE;
            session->threads_needed = session->stack_needed = session->locals_needed = TRUE;
            session->watch_next = 0;
            gtk_label_set_text(GTK_LABEL(remote->status),
                insight_tr("Remote process attached. Inspect or Continue; Detach leaves it running."));
        }
        else
        {
            gchar *message = native_debug_field(record, "msg");
            gchar *status = g_strdup_printf(insight_tr("Attach failed; SSH connection remains open: %s"),
                message ? message : insight_tr("Unknown GDB error"));
            gtk_label_set_text(GTK_LABEL(remote->status), status);
            g_free(status);
            g_free(message);
        }
    }
    else if (request == NATIVE_REMOTE_DETACH)
    {
        if (!success)
        {
            gchar *message = native_debug_field(record, "msg");
            gtk_label_set_text(GTK_LABEL(remote->status), message ? message : insight_tr("Detach failed."));
            g_free(message);
            remote->busy = FALSE;
            return TRUE;
        }
        native_target_clear(session);
        remote->disconnect_needed = TRUE;
    }
    else
    {
        if (!success && request == NATIVE_REMOTE_CONNECT)
        {
            gchar *message = native_debug_field(record, "msg");
            gtk_label_set_text(GTK_LABEL(remote->status), message ? message : insight_tr("Remote connection failed."));
            g_free(message);
            insight_queue_clear_full(&session->launch_commands, g_free);
        }
        else if (success || remote->ssh)
            gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Remote disconnected; SSH tunnel closed."));
        native_remote_drop(session);
        native_target_clear(session);
        native_remote_restore(session);
    }
    return TRUE;
}

/* EN: Validate remote Run for session and queue executable/arguments, returning FALSE with a form error.
 * RU: Проверяет удалённый Run session и ставит файл/аргументы в очередь; ошибка формы даёт FALSE. */
static gboolean native_remote_launch(NativeDebug *session)
{
    NativeRemote *remote = session->remote;
    const gchar *path = gtk_entry_get_text(GTK_ENTRY(remote->executable));
    GError *error = NULL;
    gchar *arguments = native_launch_arguments(gtk_entry_get_text(GTK_ENTRY(session->launch_args)), &error);
    gchar *executable = native_remote_setting("remote exec-file", path);
    gboolean valid = remote->connected && remote->extended && !remote->busy && *path && arguments && executable;
    if (!valid)
    {
        gtk_label_set_text(GTK_LABEL(remote->status), error ? error->message :
            insight_tr("Connect in extended-remote mode and specify the remote executable before Run or Restart."));
        g_clear_error(&error);
        g_free(arguments);
        g_free(executable);
        return FALSE;
    }
    insight_queue_clear_full(&session->launch_commands, g_free);
    g_queue_push_tail(&session->launch_commands, executable);
    g_queue_push_tail(&session->launch_commands, arguments);
    g_queue_push_tail(&session->launch_commands, g_strdup("-exec-run"));
    return TRUE;
}
