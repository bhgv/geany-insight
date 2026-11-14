/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Handle terminal PTYs, asynchronous SSH reads and writes.
 * RU: Управляет PTY терминалов и асинхронным вводом-выводом SSH. */

static GInputStream *
shell_input (GSubprocess *process)
{
    GInputStream *stream = g_object_get_data (G_OBJECT (process), "insight-pty-input");
    return stream ? stream : g_subprocess_get_stdout_pipe (process);
}
/* EN: Return process's borrowed PTY output stream, or stdin when no PTY is present.
 * RU: Возвращает поток записи PTY process либо stdin при отсутствии PTY. */
static GOutputStream *
shell_output (GSubprocess *process)
{
    GOutputStream *stream = g_object_get_data (G_OBJECT (process), "insight-pty-output");
    return stream ? stream : g_subprocess_get_stdin_pipe (process);
}
#ifndef G_OS_WIN32
/* EN: Close the encoded file descriptor in data when its owner is released.
 * RU: Закрывает закодированный в data дескриптор при освобождении владельца. */
static void
close_pty (gpointer data)
{
    close (GPOINTER_TO_INT (data) - 1);
}
/* EN: Create the SSH child's controlling terminal; data is unused.
 * RU: Создаёт управляющий терминал дочернего SSH; data не используется. */
static void
child_terminal (gpointer data)
{
    (void)data;
    setsid ();
    ioctl (0, TIOCSCTTY, 0);
}
/* EN: Attach a terminal sized from view to spawn; return master descriptor or -1/error.
 * RU: Подключает терминал размера view к spawn; возвращает дескриптор либо -1/error. */
static gint
prepare_pty (GSubprocessLauncher *spawn, GtkWidget *view, GError **error)
{
    gint master = posix_openpt (O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0)
        goto fail;
    if (grantpt (master) < 0 || unlockpt (master) < 0)
    {
        close (master);
        goto fail;
    }
    gchar *name = ptsname (master);
    gint slave = name ? open (name, O_RDWR | O_NOCTTY) : -1;
    if (slave < 0)
    {
        close (master);
        goto fail;
    }
    struct termios term;
    if (tcgetattr (slave, &term) < 0)
    {
        close (slave);
        close (master);
        goto fail;
    }
    cfmakeraw (&term);
    tcsetattr (slave, TCSANOW, &term);
    gint rows, cols;
    insight_terminal_size (view, &rows, &cols);
    struct winsize size = { rows, cols, 0, 0 };
    ioctl (slave, TIOCSWINSZ, &size);
    gint out = dup (slave), err = dup (slave);
    if (out < 0 || err < 0)
    {
        if (out >= 0)
            close (out);
        if (err >= 0)
            close (err);
        close (slave);
        close (master);
        goto fail;
    }
    fcntl (master, F_SETFD, FD_CLOEXEC);
    g_subprocess_launcher_set_flags (spawn, G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_take_stdin_fd (spawn, slave);
    g_subprocess_launcher_take_stdout_fd (spawn, out);
    g_subprocess_launcher_take_stderr_fd (spawn, err);
    g_subprocess_launcher_set_child_setup (spawn, child_terminal, NULL, NULL);
    return master;
fail:
    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Cannot create SSH PTY: %s",
                 g_strerror (errno));
    return -1;
}
#endif
/* EN: Resize session data's PTY to rows/cols when its SSH process still exists.
 * RU: Меняет размер PTY сеанса data на rows/cols при существующем SSH-процессе. */
static void
shell_resize (gint rows, gint cols, gpointer data)
{
    ShellSession *s = data;
    if (!s->process)
        return;
#ifndef G_OS_WIN32
    gpointer stored = g_object_get_data (G_OBJECT (s->process), "insight-pty-fd");
    if (stored)
    {
        struct winsize size = { rows, cols, 0, 0 };
        ioctl (GPOINTER_TO_INT (stored) - 1, TIOCSWINSZ, &size);
    }
#else
    (void)rows;
    (void)cols;
#endif
}
/* EN: Feed size bytes into session s's terminal emulator.
 * RU: Передаёт size байт bytes в эмулятор терминала сеанса s. */
static void
shell_append (ShellSession *s, const gchar *bytes, gsize size)
{
    insight_terminal_feed (s->output, bytes, size);
}
/* EN: Finalize session s only after process has both exited and reached stream EOF.
 * RU: Завершает сеанс s после выхода process и достижения конца потока. */
static void
shell_complete (ShellSession *s, GSubprocess *process)
{
    if (remote_ui.closing || s->process != process
        || !g_object_get_data (G_OBJECT (process), "insight-ssh-eof")
        || !g_object_get_data (G_OBJECT (process), "insight-ssh-exited"))
        return;
    const gchar *error = g_object_get_data (G_OBJECT (process), "insight-ssh-error");
    gchar *message = NULL;
    if (error)
        message = g_strdup_printf ("SSH failed: %s", error);
    else if (g_subprocess_get_if_exited (process))
    {
        gint code = g_subprocess_get_exit_status (process);
        message = code ? g_strdup_printf ("SSH client exited with status %d. See its output above.", code)
                       : g_strdup ("SSH session ended");
    }
    else
        message = g_strdup ("SSH client terminated. See its output above.");
    g_cancellable_cancel (s->cancel);
    g_clear_object (&s->cancel);
    g_clear_object (&s->process);
    auth_cleanup (s->auth);
    g_clear_pointer (&s->auth, g_free);
    s->writing = FALSE;
    s->pending_bytes = 0;
    insight_queue_clear_full (&s->pending, (GDestroyNotify)g_bytes_unref);
    session_status (s, message);
    g_free (message);
}
/* EN: Consume asynchronous read result from object for retained session data; queue the next read while current.
 * RU: Принимает result чтения object для удерживаемого сеанса data; продолжает чтение текущего процесса. */
static void
shell_read (GObject *object, GAsyncResult *result, gpointer data)
{
    GSubprocess *process = data;
    ShellSession *s = g_object_get_data (G_OBJECT (process), "insight-ssh-session");
    GError *error = NULL;
    GBytes *bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (object), result, &error);
    if (!remote_ui.closing && s->process == process)
    {
        if (bytes && g_bytes_get_size (bytes))
        {
            if (!g_object_get_data (G_OBJECT (process), "insight-ssh-output-seen"))
            {
                g_object_set_data (G_OBJECT (process), "insight-ssh-output-seen", GINT_TO_POINTER (1));
                session_status (s, "SSH client running — authentication and server output follow below");
            }
            gsize size;
            const gchar *text = g_bytes_get_data (bytes, &size);
            shell_append (s, text, size);
            g_input_stream_read_bytes_async (shell_input (process), 8192, G_PRIORITY_DEFAULT, s->cancel,
                                             shell_read, g_object_ref (process));
        }
        else
        {
            if (error
                && !(g_object_get_data (G_OBJECT (process), "insight-pty-fd")
                     && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_FAILED)))
            {
                g_object_set_data_full (G_OBJECT (process), "insight-ssh-error", g_strdup (error->message),
                                        g_free);
                g_subprocess_force_exit (process);
            }
            g_object_set_data (G_OBJECT (process), "insight-ssh-eof", GINT_TO_POINTER (1));
            shell_complete (s, process);
        }
    }
    if (bytes)
        g_bytes_unref (bytes);
    g_clear_error (&error);
    g_object_unref (process);
}
/* EN: Reap process object using result for retained session data and finalize when output is drained.
 * RU: Завершает ожидание object по result для сеанса data и завершает после чтения вывода. */
static void
shell_finished (GObject *object, GAsyncResult *result, gpointer data)
{
    (void)data;
    GSubprocess *process = G_SUBPROCESS (object);
    ShellSession *s = g_object_get_data (object, "insight-ssh-session");
    GError *error = NULL;
    gboolean ok = g_subprocess_wait_finish (process, result, &error);
    if (!remote_ui.closing && s->process == process)
    {
        if (!ok)
            g_object_set_data_full (G_OBJECT (process), "insight-ssh-error",
                                    g_strdup (error ? error->message : "Cannot wait for SSH client"), g_free);
        g_object_set_data (G_OBJECT (process), "insight-ssh-exited", GINT_TO_POINTER (1));
        shell_complete (s, process);
    }
    g_clear_error (&error);
}
/* EN: Cancel session s's own streams/process and discard pending terminal input.
 * RU: Отменяет потоки/процесс сеанса s и удаляет ожидающий ввод терминала. */
static void
session_disconnect (ShellSession *s)
{
    if (s->process)
    {
        g_cancellable_cancel (s->cancel);
        g_subprocess_force_exit (s->process);
        g_clear_object (&s->process);
        g_clear_object (&s->cancel);
        auth_cleanup (s->auth);
        g_clear_pointer (&s->auth, g_free);
    }
    s->writing = FALSE;
    s->pending_bytes = 0;
    insight_queue_clear_full (&s->pending, (GDestroyNotify)g_bytes_unref);
}
/* EN: Disconnect the selected SSH terminal; widget/data are unused.
 * RU: Отключает выбранный терминал SSH; widget/data не используются. */
static void
shell_disconnect (GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    if (!remote_ui.selected)
        return;
    session_disconnect (remote_ui.selected);
    session_status (remote_ui.selected, "SSH shell disconnected");
}
/* EN: Connect the selected terminal using current form settings; widget/data are unused; report errors visibly.
 * RU: Подключает выбранный терминал по форме; widget/data не используются; показывает ошибки. */
static void
shell_connect (GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    ShellSession *s = remote_ui.selected;
    if (!s)
        return;
    profile_save ();
    session_disconnect (s);
    GError *error = NULL;
    RemoteRoot *root = form_root ("/", remote_ui.directory, remote_ui.directory, &error);
    if (!root)
    {
        session_status (s, error ? error->message : "Invalid SSH connection");
        g_clear_error (&error);
        return;
    }
    GSubprocessLauncher *spawn = launcher (root, &s->auth);
    g_subprocess_launcher_setenv (spawn, "TERM", "xterm-256color", TRUE);
    gboolean askpass = g_object_get_data (G_OBJECT (spawn), "insight-ssh-askpass") != NULL;
    session_status (s, "Starting SSH client; waiting for the server and authentication…");
#ifndef G_OS_WIN32
    gint master = prepare_pty (spawn, s->output, &error);
    s->process = master >= 0 ? spawn_client (spawn, root, FALSE, &error) : NULL;
    if (s->process)
    {
        g_object_set_data_full (G_OBJECT (s->process), "insight-pty-fd", GINT_TO_POINTER (master + 1),
                                close_pty);
        g_object_set_data_full (G_OBJECT (s->process), "insight-pty-input",
                                g_unix_input_stream_new (dup (master), TRUE), g_object_unref);
        g_object_set_data_full (G_OBJECT (s->process), "insight-pty-output",
                                g_unix_output_stream_new (dup (master), TRUE), g_object_unref);
    }
    else if (master >= 0)
        close (master);
#else
    s->process = spawn_client (spawn, root, FALSE, &error);
#endif
    g_object_unref (spawn);
    g_free (s->endpoint);
    s->endpoint = g_strdup_printf ("%s@%s:%u", root->remote->user, root->remote->host, root->remote->port);
    insight_remote_free (s->remote);
    s->remote = insight_remote_new (root->remote->host, root->remote->user, root->remote->port,
                                    root->remote->identity, "/", root->remote->local, NULL);
    root_free (root);
    if (!s->process)
    {
        session_status (s, error ? error->message : "OpenSSH unavailable");
        g_clear_error (&error);
        auth_cleanup (s->auth);
        g_clear_pointer (&s->auth, g_free);
        return;
    }
    g_object_set_data_full (G_OBJECT (s->process), "insight-ssh-session", session_ref (s), session_unref);
    s->cancel = g_cancellable_new ();
    g_input_stream_read_bytes_async (shell_input (s->process), 8192, G_PRIORITY_DEFAULT, s->cancel,
                                     shell_read, g_object_ref (s->process));
    g_subprocess_wait_async (s->process, NULL, shell_finished, NULL);
    if (!askpass)
        session_status (s,
                        "SSH password dialog helper is missing; only key/agent authentication is available.");
    gtk_notebook_set_current_page (GTK_NOTEBOOK (remote_ui.tabs),
                                   gtk_notebook_page_num (GTK_NOTEBOOK (remote_ui.tabs), s->page));
    gtk_widget_grab_focus (s->input);
}
typedef struct
{
    GSubprocess *process;
    GBytes *text;
} ShellWrite;
static void shell_flush (ShellSession *s);
/* EN: Complete output write result from object for retained write data and flush remaining queued input.
 * RU: Завершает запись result из object для удерживаемого data и отправляет остальной ввод. */
static void
shell_write_done (GObject *object, GAsyncResult *result, gpointer data)
{
    ShellWrite *write = data;
    ShellSession *s = g_object_get_data (G_OBJECT (write->process), "insight-ssh-session");
    GError *error = NULL;
    gboolean ok = g_output_stream_write_all_finish (G_OUTPUT_STREAM (object), result, NULL, &error);
    if (!remote_ui.closing && s->process == write->process)
    {
        s->writing = FALSE;
        s->pending_bytes -= MIN (s->pending_bytes, g_bytes_get_size (write->text));
        if (!ok)
            session_status (s, error ? error->message : "SSH shell write failed");
        else
            shell_flush (s);
    }
    g_clear_error (&error);
    g_object_unref (write->process);
    g_bytes_unref (write->text);
    g_free (write);
}
/* EN: Start the next asynchronous queued write for session s unless already writing.
 * RU: Начинает следующую асинхронную запись сеанса s, если запись свободна. */
static void
shell_flush (ShellSession *s)
{
    if (!s->process || s->writing || g_queue_is_empty (&s->pending))
        return;
    ShellWrite *write = g_new0 (ShellWrite, 1);
    write->process = g_object_ref (s->process);
    write->text = g_queue_pop_head (&s->pending);
    s->writing = TRUE;
    g_output_stream_write_all_async (shell_output (s->process), g_bytes_get_data (write->text, NULL),
                                     g_bytes_get_size (write->text), G_PRIORITY_DEFAULT, s->cancel,
                                     shell_write_done, write);
}
/* EN: Queue length bytes for session data with a bounded pending-input limit.
 * RU: Ставит length байт bytes в очередь сеанса data с ограничением объёма ввода. */
static void
terminal_write (const gchar *bytes, gsize length, gpointer data)
{
    ShellSession *s = data;
    if (!s->process || remote_ui.closing)
        return;
    if (g_queue_get_length (&s->pending) >= 256 || length > 1024 * 1024
        || s->pending_bytes + length > 1024 * 1024)
    {
        session_status (s, "SSH input queue is full; wait for the client");
        return;
    }
    s->pending_bytes += length;
    g_queue_push_tail (&s->pending, g_bytes_new (bytes, length));
    shell_flush (s);
}
