/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Stop an attached process by PID over the owned SSH channel when gdbserver shares that host.
 * RU: Останавливает подключённый процесс по PID через собственный SSH-канал, если gdbserver на том же хосте. */

typedef struct
{
    GtkWidget *window;
    GSubprocess *ssh;
    gchar *line;
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
        session->remote->busy = session->remote->detach_needed = session->remote->disconnect_needed = FALSE;
        insight_queue_clear_full(&session->launch_commands, g_free);
        session->stop_requested = FALSE;
        gtk_label_set_text(GTK_LABEL(session->remote->status), error ? error->message :
            insight_tr("Could not send the remote pause request."));
        native_debug_update(session);
    }
    g_clear_error(&error);
    g_free(request->line);
    g_object_unref(request->ssh);
    g_object_unref(request->window);
    g_free(request);
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
        gtk_entry_get_text(GTK_ENTRY(remote->server_host)));
    if (!line)
        return FALSE;
    if (remote->interrupt_pending)
    {
        g_free(line);
        return TRUE;
    }
    NativeInterruptWrite *request = g_new0(NativeInterruptWrite, 1);
    request->window = g_object_ref(session->window);
    request->ssh = g_object_ref(remote->ssh);
    request->line = line;
    remote->interrupt_pending = remote->pause_requested = TRUE;
    gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Requesting a pause of the attached process through SSH…"));
    native_debug_update(session);
    g_output_stream_write_all_async(g_subprocess_get_stdin_pipe(remote->ssh), line, strlen(line),
        G_PRIORITY_DEFAULT, remote->cancel, native_attach_interrupt_written, request);
    return TRUE;
}

/* EN: Consume the owned SSH stream's pause acknowledgement line for session; never claim GDB stopped before its event.
 * RU: Обрабатывает подтверждение line собственного SSH-канала session; не объявляет остановку до события GDB. */
static gboolean native_attach_interrupt_reply(NativeDebug *session, const gchar *line)
{
    gboolean success = !strcmp(line, "GEANY-GDB-SSH-STOPPED");
    if (!success && strcmp(line, "GEANY-GDB-SSH-STOP-FAILED"))
        return FALSE;
    NativeRemote *remote = session->remote;
    if (!remote->interrupt_pending)
        return TRUE;
    remote->interrupt_pending = success && !session->stopped;
    if (!success)
    {
        remote->pause_requested = FALSE;
        remote->busy = remote->detach_needed = remote->disconnect_needed = FALSE;
        session->stop_requested = FALSE;
        insight_queue_clear_full(&session->launch_commands, g_free);
    }
    gtk_label_set_text(GTK_LABEL(remote->status), insight_tr(!success ?
        "SSH could not pause the process. Check signal permissions in Console." :
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
        if (!remote->busy)
            gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Remote process paused."));
    }
    g_free(signal);
}
