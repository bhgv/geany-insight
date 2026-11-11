/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Establish and monitor an owned SSH tunnel asynchronously, preserving the editor event loop.
 * RU: Асинхронно создаёт и контролирует SSH-туннель, сохраняя работу цикла событий редактора. */

/* EN: Close session's own SSH process and streams; preserve form entries and source mapping ownership.
 * RU: Закрывает собственные процесс и потоки SSH session; сохраняет поля формы и владение сопоставлением. */
static void native_remote_drop(NativeDebug *session)
{
    NativeRemote *remote = session->remote;
    if (!remote)
        return;
    if (remote->timer)
    {
        g_source_remove(remote->timer);
        remote->timer = 0;
    }
    if (remote->cancel)
        g_cancellable_cancel(remote->cancel);
    if (remote->ssh)
        g_subprocess_force_exit(remote->ssh);
    g_clear_object(&remote->ssh);
    g_clear_object(&remote->output);
    g_clear_object(&remote->errors);
    g_clear_object(&remote->cancel);
    remote->attached_pid = 0;
    remote->interrupt_pending = FALSE;
    remote->pause_requested = remote->clear_stop_signal = remote->release_pending = remote->own_continue_pending = FALSE;
    remote->busy = remote->connected = remote->detach_needed = remote->disconnect_needed = FALSE;
    remote->pending = NATIVE_REMOTE_NONE;
}

/* EN: Finish waiting for SSH process in object; update the dialog retained in data only for this tunnel.
 * RU: Завершает ожидание SSH object; обновляет сохранённый диалог data только для этого туннеля. */
static void native_remote_exited(GObject *object, GAsyncResult *result, gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    g_subprocess_wait_finish(G_SUBPROCESS(object), result, NULL);
    if (!session->closed && session->remote->ssh == G_SUBPROCESS(object))
    {
        gboolean connected = session->remote->connected;
        native_remote_drop(session);
        insight_queue_clear_full(&session->launch_commands, g_free);
        gtk_label_set_text(GTK_LABEL(session->remote->status),
            insight_tr("SSH tunnel ended. See the debugger console for authentication or forwarding errors."));
        native_debug_log(session, insight_tr("SSH tunnel ended."));
        if (connected)
        {
            native_target_clear(session);
            session->remote->disconnect_needed = TRUE;
        }
        else if (session->pending)
            session->remote->pending = NATIVE_REMOTE_DISCONNECT;
        else
            native_remote_restore(session);
        native_debug_update(session);
        native_debug_resume(dialog, session);
    }
    g_object_unref(dialog);
}

/* EN: Queue remote settings and connection for session after SSH confirms forwarding, without probing RSP.
 * RU: Ставит настройки и подключение session после подтверждения туннеля SSH, не проверяя порт RSP. */
static void native_remote_ready(NativeDebug *session)
{
    NativeRemote *remote = session->remote;
    if (remote->timer)
    {
        g_source_remove(remote->timer);
        remote->timer = 0;
    }
    if (remote->source_mapping)
        native_remote_mapping(&session->launch_commands, remote->source_mapping, NULL);
    g_clear_pointer(&remote->source_mapping, g_free);
    const gchar *from = gtk_entry_get_text(GTK_ENTRY(remote->remote_source));
    const gchar *to = gtk_entry_get_text(GTK_ENTRY(remote->local_source));
    if (*from && *to)
    {
        native_remote_mapping(&session->launch_commands, from, to);
        remote->source_mapping = g_strdup(from);
    }
    const gchar *sysroot = gtk_entry_get_text(GTK_ENTRY(remote->sysroot));
    gchar *setting = native_remote_setting("sysroot", *sysroot ? sysroot : "target:");
    if (!setting)
    {
        native_remote_drop(session);
        gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Paths must not contain line breaks."));
        return;
    }
    g_queue_push_tail(&session->launch_commands, setting);
    g_queue_push_tail(&session->launch_commands, g_strdup("-gdb-set remotetimeout 15"));
    g_queue_push_tail(&session->launch_commands, g_strdup_printf("-target-select %s 127.0.0.1:%u",
        remote->extended ? "extended-remote" : "remote", remote->local_port));
    gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("SSH tunnel ready; connecting GDB…"));
    native_debug_resume(session->window, session);
}

/* EN: Read SSH stdout/stderr from object for the dialog in data; only the exact readiness line starts GDB.
 * RU: Читает stdout/stderr SSH object для диалога data; GDB запускается только по точной строке готовности. */
static void native_remote_read(GObject *object, GAsyncResult *result, gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    NativeRemote *remote = session->remote;
    gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(object), result, NULL, NULL);
    gboolean current = !session->closed && remote->ssh &&
        (object == G_OBJECT(remote->output) || object == G_OBJECT(remote->errors));
    if (current && line)
    {
        g_strchomp(line);
        if (object == G_OBJECT(remote->output) && !strcmp(line, "GEANY-GDB-SSH-READY") && remote->timer)
            native_remote_ready(session);
        else if (object == G_OBJECT(remote->output) && native_attach_interrupt_reply(session, line))
        {
            /* EN/RU: The acknowledgement is protocol data, not console text. / Подтверждение — данные протокола. */
        }
        else if (*line)
        {
            gchar *text = g_strdup_printf("SSH: %.2000s", line);
            native_debug_log(session, text);
            g_free(text);
        }
        g_data_input_stream_read_line_async(G_DATA_INPUT_STREAM(object), G_PRIORITY_DEFAULT,
            remote->cancel, native_remote_read, g_object_ref(dialog));
    }
    g_free(line);
    g_object_unref(dialog);
}

/* EN: Cancel a tunnel that failed to become ready within two minutes; data retains the dialog.
 * RU: Отменяет туннель, не готовый за две минуты; data удерживает диалог. */
static gboolean native_remote_timeout(gpointer data)
{
    GtkWidget *dialog = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    session->remote->timer = 0;
    native_remote_drop(session);
    gtk_label_set_text(GTK_LABEL(session->remote->status), insight_tr("SSH connection timed out."));
    native_debug_update(session);
    return G_SOURCE_REMOVE;
}

/* EN: Reserve an ephemeral loopback port for session, release it for SSH and return FALSE on bind errors.
 * RU: Выбирает временный loopback-порт session, освобождает для SSH; ошибка привязки даёт FALSE. */
static gboolean native_remote_port(NativeDebug *session, GError **error)
{
    GSocketListener *listener = g_socket_listener_new();
    GInetAddress *address = g_inet_address_new_from_string("127.0.0.1");
    GSocketAddress *requested = g_inet_socket_address_new(address, 0), *effective = NULL;
    gboolean valid = g_socket_listener_add_address(listener, requested, G_SOCKET_TYPE_STREAM,
        G_SOCKET_PROTOCOL_TCP, NULL, &effective, error);
    if (valid)
        session->remote->local_port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(effective));
    g_clear_object(&effective);
    g_object_unref(requested);
    g_object_unref(address);
    g_socket_listener_close(listener);
    g_object_unref(listener);
    return valid;
}

/* EN: Start validated SSH settings in session with bundled/PATH clients and optional GUI askpass; report spawn errors.
 * RU: Запускает проверенные настройки SSH session через комплект/PATH и необязательный GUI askpass; сообщает ошибки запуска. */
static gboolean native_remote_spawn(NativeDebug *session)
{
    NativeRemote *remote = session->remote;
    gchar *share = app->datadir ? g_path_get_dirname(app->datadir) : NULL;
    gchar *prefix = share ? g_path_get_dirname(share) : NULL;
    gchar *bin = prefix ? g_build_filename(prefix, "bin", NULL) : NULL;
    gchar *ssh = insight_remote_program("ssh", bin);
    gchar *helper = insight_remote_program("geany-insight-askpass", bin);
    g_free(bin);
    g_free(prefix);
    g_free(share);
    GError *error = NULL;
    if (!ssh)
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, insight_tr("OpenSSH was not found in PATH."));
    if (!error)
        native_remote_port(session, &error);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    if (helper)
    {
        g_subprocess_launcher_setenv(launcher, "SSH_ASKPASS", helper, TRUE);
        g_subprocess_launcher_setenv(launcher, "SSH_ASKPASS_REQUIRE", "force", TRUE);
        g_subprocess_launcher_setenv(launcher, "GEANY_INSIGHT_SSH_SAVE_PASSWORD", "0", TRUE);
    }
    gchar **argv = !error ? native_remote_argv(ssh,
        gtk_entry_get_text(GTK_ENTRY(remote->host)), gtk_entry_get_text(GTK_ENTRY(remote->user)),
        (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote->port)),
        gtk_entry_get_text(GTK_ENTRY(remote->identity)), gtk_entry_get_text(GTK_ENTRY(remote->config)),
        gtk_entry_get_text(GTK_ENTRY(remote->server_host)),
        (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote->server_port)),
        remote->local_port, helper != NULL) : NULL;
    if (argv)
        remote->ssh = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv, &error);
    g_strfreev(argv);
    g_object_unref(launcher);
    g_free(helper);
    g_free(ssh);
    if (!remote->ssh)
    {
        gtk_label_set_text(GTK_LABEL(remote->status), error ? error->message : insight_tr("Invalid SSH endpoint."));
        g_clear_error(&error);
        return FALSE;
    }
    remote->busy = TRUE;
    remote->cancel = g_cancellable_new();
    remote->output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(remote->ssh));
    remote->errors = g_data_input_stream_new(g_subprocess_get_stderr_pipe(remote->ssh));
    remote->timer = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, 120,
        native_remote_timeout, g_object_ref(session->window), g_object_unref);
    g_subprocess_wait_async(remote->ssh, NULL, native_remote_exited, g_object_ref(session->window));
    g_data_input_stream_read_line_async(remote->output, G_PRIORITY_DEFAULT,
        remote->cancel, native_remote_read, g_object_ref(session->window));
    g_data_input_stream_read_line_async(remote->errors, G_PRIORITY_DEFAULT,
        remote->cancel, native_remote_read, g_object_ref(session->window));
    gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Connecting SSH… Use Disconnect to cancel."));
    native_debug_update(session);
    return TRUE;
}
