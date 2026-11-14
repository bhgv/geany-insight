/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Own the wizard-started foreground gdbserver and wait for readiness before tunnelling.
 * RU: Управляет запущенным мастером gdbserver и ждёт готовности перед созданием туннеля. */

/* EN: Report timeout for the dialog retained in data and cancel its owned server.
 * RU: Сообщает тайм-аут для диалога data и отменяет его собственный сервер. */
static gboolean native_server_timeout(gpointer data)
{
    NativeDebug *session = g_object_get_data(G_OBJECT(data), "native-debug");
    session->remote->server_timer = 0;
    native_remote_drop(session);
    gtk_label_set_text(GTK_LABEL(session->remote->status),
                       insight_tr("gdbserver did not become ready. Check SSH access and the debug port."));
    native_debug_update(session);
    return G_SOURCE_REMOVE;
}

/* EN: Reap object and report an unexpected server exit for the retained dialog data.
 * RU: Завершает ожидание object и сообщает неожиданный выход сервера диалога data. */
static void native_server_exited(GObject *object, GAsyncResult *result, gpointer data)
{
    NativeDebug *session = g_object_get_data(G_OBJECT(data), "native-debug");
    g_subprocess_wait_finish(G_SUBPROCESS(object), result, NULL);
    if (!session->closed && session->remote->server == G_SUBPROCESS(object))
    {
        gboolean connected = session->remote->connected;
        native_remote_drop(session);
        insight_queue_clear_full(&session->launch_commands, g_free);
        if (connected)
        {
            native_target_clear(session);
            session->remote->disconnect_needed = TRUE;
        }
        else if (session->pending)
            session->remote->pending = NATIVE_REMOTE_DISCONNECT;
        else
            native_remote_restore(session);
        native_debug_resume(session->window, session);
        gtk_label_set_text(GTK_LABEL(session->remote->status),
                           insight_tr("gdbserver ended. See SSH diagnostics in the console."));
        native_debug_update(session);
    }
    g_object_unref(data);
}

/* EN: Read server output object for retained dialog data; exact readiness starts its SSH tunnel once.
 * RU: Читает вывод сервера object диалога data; точная готовность однократно запускает SSH-туннель. */
static void native_server_read(GObject *object, GAsyncResult *result, gpointer data)
{
    NativeDebug *session = g_object_get_data(G_OBJECT(data), "native-debug");
    NativeRemote *remote = session->remote;
    gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(object), result, NULL, NULL);
    if (!session->closed &&
        (object == G_OBJECT(remote->server_output) || object == G_OBJECT(remote->server_errors)) && line)
    {
        g_strchomp(line);
        gchar *message = g_strdup_printf("gdbserver: %.2000s", line);
        native_debug_log(session, message);
        g_free(message);
        gchar *ready =
            g_strdup_printf("Listening on port %u",
                            (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote->server_port)));
        if (remote->server_timer && !strcmp(line, ready))
        {
            g_source_remove(remote->server_timer);
            remote->server_timer = 0;
            native_remote_spawn(session);
        }
        g_free(ready);
        if (remote->server)
            g_data_input_stream_read_line_async(G_DATA_INPUT_STREAM(object), G_PRIORITY_DEFAULT,
                                                remote->server_cancel, native_server_read,
                                                g_object_ref(data));
    }
    g_free(line);
    g_object_unref(data);
}

/* EN: Start an owned foreground --multi server in directory for session, or display a spawn error.
 * RU: Запускает собственный сервер --multi в directory для session либо показывает ошибку запуска. */
static void native_server_start(NativeDebug *session, const gchar *directory)
{
    NativeRemote *remote = session->remote;
    gchar *share = app->datadir ? g_path_get_dirname(app->datadir) : NULL;
    gchar *prefix = share ? g_path_get_dirname(share) : NULL;
    gchar *bin = prefix ? g_build_filename(prefix, "bin", NULL) : NULL;
    gchar *ssh = insight_remote_program("ssh", bin);
    gchar *helper = insight_remote_program("geany-insight-askpass", bin);
    GError *error = NULL;
    gchar *command = insight_ssh_server_command(
        directory, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote->server_port)), &error);
    InsightRemote *endpoint =
        command
            ? insight_remote_new(gtk_entry_get_text(GTK_ENTRY(remote->host)),
                                 gtk_entry_get_text(GTK_ENTRY(remote->user)),
                                 gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote->port)),
                                 gtk_entry_get_text(GTK_ENTRY(remote->identity)), "/", app->configdir, &error)
            : NULL;
    if (!error && !ssh)
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            insight_tr("OpenSSH was not found in PATH."));
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    if (!error)
        remote->server_auth = insight_ssh_auth_prepare(
            launcher, app->configdir, endpoint->host, endpoint->user, endpoint->port, endpoint->identity,
            helper, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote->remember)), &error);
    gchar **argv = !error
                       ? insight_ssh_setup_argv(endpoint, ssh, gtk_entry_get_text(GTK_ENTRY(remote->config)),
                                                command, helper != NULL)
                       : NULL;
    if (argv)
        remote->server = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv, &error);
    if (remote->server)
    {
        remote->busy = TRUE;
        remote->server_cancel = g_cancellable_new();
        remote->server_output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(remote->server));
        remote->server_errors = g_data_input_stream_new(g_subprocess_get_stderr_pipe(remote->server));
        remote->server_timer = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, 120, native_server_timeout,
                                                          g_object_ref(session->window), g_object_unref);
        g_subprocess_wait_async(remote->server, NULL, native_server_exited, g_object_ref(session->window));
        g_data_input_stream_read_line_async(remote->server_errors, G_PRIORITY_DEFAULT, remote->server_cancel,
                                            native_server_read, g_object_ref(session->window));
        g_data_input_stream_read_line_async(remote->server_output, G_PRIORITY_DEFAULT, remote->server_cancel,
                                            native_server_read, g_object_ref(session->window));
        gtk_label_set_text(GTK_LABEL(remote->status),
                           insight_tr("Starting gdbserver via SSH… Disconnect cancels startup."));
    }
    else
    {
        native_remote_drop(session);
        gtk_label_set_text(GTK_LABEL(remote->status),
                           error ? error->message : insight_tr("Remote connection failed."));
    }
    native_debug_update(session);
    g_clear_error(&error);
    g_strfreev(argv);
    g_object_unref(launcher);
    insight_remote_free(endpoint);
    g_free(command);
    g_free(ssh);
    g_free(helper);
    g_free(bin);
    g_free(prefix);
    g_free(share);
}
