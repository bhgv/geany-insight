/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Stop an attached process by PID over the owned SSH channel when gdbserver shares that host.
 * RU: Останавливает подключённый процесс по PID через собственный SSH-канал, если gdbserver на том же хосте. */

typedef struct
{
    GtkWidget *window;
    GSubprocess *ssh;
    gchar *line;
    gboolean release;
} NativeInterruptWrite;

/* EN: Complete the write to stream from result; data owns the window, tunnel and buffer until completion.
 * RU: Завершает запись в stream по result; data удерживает окно, туннель и буфер до завершения. */
static void native_attach_interrupt_written(GObject *stream, GAsyncResult *result, gpointer data)
{
    NativeInterruptWrite *request = data;
    NativeDebug *session = g_object_get_data(G_OBJECT(request->window), "native-debug");
    GError *error = NULL;
    gboolean success = g_output_stream_write_all_finish(G_OUTPUT_STREAM(stream), result, NULL, &error);
    if (!session->closed && session->remote->ssh == request->ssh && !success)
    {
        session->remote->interrupt_pending = session->remote->pause_requested = FALSE;
        session->remote->release_pending = session->remote->clear_stop_signal = FALSE;
        session->remote->busy = session->remote->detach_needed = session->remote->disconnect_needed = FALSE;
        insight_queue_clear_full(&session->launch_commands, g_free);
        session->stop_requested = FALSE;
        gtk_label_set_text(GTK_LABEL(session->remote->status), error ? error->message :
            insight_tr(request->release ? "Could not release the remote pause signal." : "Could not send the remote pause request."));
        native_debug_update(session);
    }
    g_clear_error(&error);
    g_free(request->line);
    g_object_unref(request->ssh);
    g_object_unref(request->window);
    g_free(request);
}

/* EN: Send owned line for session and release mode, retaining window/tunnel until the asynchronous write finishes.
 * RU: Отправляет принадлежащую строку line для session в режиме release, удерживая окно/туннель до завершения записи. */
static void native_attach_control_write(NativeDebug *session, gchar *line, gboolean release)
{
    NativeRemote *remote = session->remote;
    NativeInterruptWrite *request = g_new0(NativeInterruptWrite, 1);
    request->window = g_object_ref(session->window);
    request->ssh = g_object_ref(remote->ssh);
    request->line = line;
    request->release = release;
    g_output_stream_write_all_async(g_subprocess_get_stdin_pipe(remote->ssh), line, strlen(line),
        G_PRIORITY_DEFAULT, remote->cancel, native_attach_interrupt_written, request);
}

/* EN: Clear session's own kernel group-stop while GDB keeps the inferior ptrace-stopped; wait for SSH acknowledgement.
 * RU: Снимает собственную групповую остановку session, пока GDB удерживает процесс через ptrace; ждёт подтверждения SSH. */
static void native_attach_release(NativeDebug *session)
{
    NativeRemote *remote = session->remote;
    gchar *line = native_attach_signal_line(remote->attached_pid,
        gtk_entry_get_text(GTK_ENTRY(remote->server_host)), TRUE);
    if (line && remote->ssh)
    {
        remote->release_pending = TRUE;
        native_attach_control_write(session, line, TRUE);
    }
    else
        g_free(line);
}

/* EN: Handle an interrupt command for session's attached PID on the SSH host; FALSE preserves normal GDB routing.
 * RU: Обрабатывает команду command для PID session на SSH-хосте; FALSE оставляет обычный маршрут GDB. */
static gboolean native_attach_interrupt(NativeDebug *session, const gchar *command)
{
    NativeRemote *remote = session->remote;
    if (!remote || !remote->connected || !remote->ssh || !remote->attached_pid ||
        (strcmp(command, "-exec-interrupt") && strcmp(command, "-exec-interrupt --all")))
        return FALSE;
    gchar *line = native_attach_signal_line(remote->attached_pid,
        gtk_entry_get_text(GTK_ENTRY(remote->server_host)), FALSE);
    if (!line)
        return FALSE;
    if (remote->interrupt_pending)
    {
        g_free(line);
        return TRUE;
    }
    remote->interrupt_pending = remote->pause_requested = TRUE;
    gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Requesting a pause of the attached process through SSH…"));
    native_debug_update(session);
    native_attach_control_write(session, line, FALSE);
    return TRUE;
}

/* EN: Consume session's owned SSH acknowledgement line; release resumes queued commands only after confirmation.
 * RU: Обрабатывает подтверждение line собственного SSH-канала session; снятие паузы продолжает очередь только после подтверждения. */
static gboolean native_attach_interrupt_reply(NativeDebug *session, const gchar *line)
{
    gboolean release = !strcmp(line, "GEANY-GDB-SSH-RESUME-OK") || !strcmp(line, "GEANY-GDB-SSH-RESUME-FAILED");
    gboolean success = !strcmp(line, "GEANY-GDB-SSH-STOP-OK") || !strcmp(line, "GEANY-GDB-SSH-RESUME-OK");
    if (!release && !success && strcmp(line, "GEANY-GDB-SSH-STOP-FAILED"))
        return FALSE;
    NativeRemote *remote = session->remote;
    if (release ? !remote->release_pending : !remote->interrupt_pending)
        return TRUE;
    if (release)
        remote->release_pending = FALSE;
    else
        remote->interrupt_pending = success && !session->stopped;
    if (!success)
    {
        remote->pause_requested = remote->clear_stop_signal = FALSE;
        remote->busy = remote->detach_needed = remote->disconnect_needed = FALSE;
        session->stop_requested = FALSE;
        insight_queue_clear_full(&session->launch_commands, g_free);
    }
    gtk_label_set_text(GTK_LABEL(remote->status), insight_tr(!success ?
        "SSH could not change the pause signal. Check signal permissions in Console." :
        session->stopped ? "Remote process paused." : "Pause signal sent; waiting for GDB to confirm the stop."));
    native_debug_update(session);
    native_debug_resume(session->window, session);
    return TRUE;
}

/* EN: Suppress only the SIGSTOP requested by session's SSH pause before the next resume or detach from record.
 * RU: Подавляет только SIGSTOP собственной SSH-паузы session из record перед продолжением или отсоединением. */
static void native_attach_stopped(NativeDebug *session, const gchar *record)
{
    NativeRemote *remote = session->remote;
    if (!remote || !remote->attached_pid || !remote->pause_requested)
        return;
    remote->interrupt_pending = FALSE;
    gchar *signal = native_debug_field(record, "signal-name");
    if (!g_strcmp0(signal, "SIGSTOP"))
    {
        remote->pause_requested = FALSE;
        remote->clear_stop_signal = TRUE;
        native_attach_release(session);
        if (!remote->busy)
            gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Remote process paused."));
    }
    g_free(signal);
}
