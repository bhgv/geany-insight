/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Run cancellable file transfers and staging.
 * RU: Выполняет отменяемую передачу файлов и подготовку. */

static void
remove_staging (const gchar *path)
{
    if (!path)
        return;
    if (g_file_test (path, G_FILE_TEST_IS_DIR) && !g_file_test (path, G_FILE_TEST_IS_SYMLINK))
    {
        GDir *dir = g_dir_open (path, 0, NULL);
        const gchar *name;
        if (dir)
        {
            while ((name = g_dir_read_name (dir)))
            {
                gchar *child = g_build_filename (path, name, NULL);
                remove_staging (child);
                g_free (child);
            }
            g_dir_close (dir);
        }
        g_rmdir (path);
    }
    else
        g_remove (path);
}
/* EN: Release job data and remove its owned staging/authentication files.
 * RU: Освобождает задание data и удаляет собственные временные файлы и авторизацию. */
static void
scp_free (gpointer data)
{
    ScpJob *j = data;
    remove_staging (j->temporary);
    auth_cleanup (j->auth);
    root_free (j->root);
    g_free (j->local);
    g_free (j->temporary);
    g_free (j->stage);
    g_free (j->auth);
    g_free (j->client);
    g_free (j->python);
    g_free (j->helper);
    g_clear_object (&j->spawn);
    g_clear_object (&j->process);
    g_clear_object (&j->cancel);
    g_mutex_clear (&j->lock);
    g_free (j);
}
/* EN: Cancel the active transfer worker and its owned child without touching terminals.
 * RU: Отменяет активную передачу и её процесс без изменения терминалов. */
static void
scp_cancel (void)
{
    ScpJob *j = remote_ui.scp;
    if (!j)
        return;
    g_cancellable_cancel (j->cancel);
    g_mutex_lock (&j->lock);
    if (j->process)
        g_subprocess_force_exit (j->process);
    g_mutex_unlock (&j->lock);
}
typedef struct
{
    GSubprocess *process;
    GCancellable *cancel;
    GFileOutputStream *output;
    GError *error;
} ScpOutput;

/* EN: Copy output for stream job data on a worker thread and return NULL.
 * RU: Копирует вывод задания data в рабочем потоке и возвращает NULL. */
static gpointer
scp_copy_output (gpointer data)
{
    ScpOutput *copy = data;
    if (g_output_stream_splice (G_OUTPUT_STREAM (copy->output), g_subprocess_get_stdout_pipe (copy->process),
                                G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET, copy->cancel, &copy->error)
        < 0)
        g_subprocess_force_exit (copy->process);
    return NULL;
}

/* EN: Run argv for job j, optionally stream stdout to binary; return FALSE/error on cancellation or failure.
 * RU: Выполняет argv задания j, при необходимости пишет stdout в binary; ошибка/отмена дают FALSE/error. */
static gboolean
scp_child (ScpJob *j, const gchar *const *argv, const gchar *binary, GError **error)
{
    if (g_cancellable_set_error_if_cancelled (j->cancel, error))
        return FALSE;
    ScpOutput copy = { 0 };
    if (binary)
    {
        GFile *file = g_file_new_for_path (binary);
        copy.output = g_file_create (file, G_FILE_CREATE_PRIVATE, j->cancel, error);
        g_object_unref (file);
        if (!copy.output)
            return FALSE;
    }
    g_subprocess_launcher_set_flags (
        j->spawn, G_SUBPROCESS_FLAGS_STDOUT_PIPE
                      | (binary ? G_SUBPROCESS_FLAGS_STDERR_PIPE : G_SUBPROCESS_FLAGS_STDERR_MERGE));
    GSubprocess *p = g_subprocess_launcher_spawnv (j->spawn, argv, error);
    if (!p)
    {
        g_clear_object (&copy.output);
        return FALSE;
    }
    g_mutex_lock (&j->lock);
    j->process = g_object_ref (p);
    if (g_cancellable_is_cancelled (j->cancel))
        g_subprocess_force_exit (p);
    g_mutex_unlock (&j->lock);
    GThread *writer = NULL;
    if (binary)
    {
        copy.process = p;
        copy.cancel = j->cancel;
        writer = g_thread_new ("scp-output", scp_copy_output, &copy);
    }
    GInputStream *stream = binary ? g_subprocess_get_stderr_pipe (p) : g_subprocess_get_stdout_pipe (p);
    GString *log = g_string_new (NULL);
    gchar buffer[4096];
    gboolean ok = TRUE;
    while (ok)
    {
        gssize n = g_input_stream_read (stream, buffer, sizeof buffer, j->cancel, error);
        if (n < 0)
        {
            ok = FALSE;
            break;
        }
        if (!n)
            break;
        if (log->len + (gsize)n > 262144)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                                 "Transfer diagnostics exceed 256 KiB");
            ok = FALSE;
            break;
        }
        g_string_append_len (log, buffer, n);
    }
    if (!ok)
        g_subprocess_force_exit (p);
    if (writer)
    {
        g_thread_join (writer);
        g_object_unref (copy.output);
        if (copy.error)
        {
            if (ok)
                g_propagate_error (error, copy.error);
            else
                g_error_free (copy.error);
            ok = FALSE;
        }
    }
    if (ok)
        ok = g_subprocess_wait (p, j->cancel, error);
    if (ok && !g_subprocess_get_successful (p))
    {
        gchar *valid = g_utf8_make_valid (log->str, MIN (log->len, (gsize)8192));
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Transfer failed: %s",
                     *valid ? valid : "OpenSSH/packing process exited unsuccessfully");
        g_free (valid);
        ok = FALSE;
    }
    if (!ok)
    {
        g_subprocess_force_exit (p);
        g_subprocess_wait (p, NULL, NULL);
    }
    g_mutex_lock (&j->lock);
    g_clear_object (&j->process);
    g_mutex_unlock (&j->lock);
    g_string_free (log, TRUE);
    g_object_unref (p);
    return ok;
}
/* EN: Perform job data for task; object is unused and cancel interrupts transfers and packing.
 * RU: Выполняет задание data для task; object не используется, cancel прерывает передачу и упаковку. */
static void
scp_worker (GTask *task, gpointer object, gpointer data, GCancellable *cancel)
{
    (void)object;
    ScpJob *j = data;
    GError *error = NULL;
    gboolean ok = TRUE;
    if (j->upload && j->mode)
    {
        const gchar *pack[]
            = { j->python, "-I", j->helper, j->mode == 1 ? "gz" : "tar.gz", j->local, j->stage, NULL };
        ok = scp_child (j, pack, NULL, &error);
    }
    gchar **argv = NULL;
    if (ok)
    {
        if (!j->upload && j->mode)
            argv = insight_remote_archive_argv (j->root->remote, j->mode == 2, &error);
        else
            argv = insight_remote_scp_argv (j->root->remote,
                                            j->upload ? (j->mode ? j->stage : j->local) : j->stage, j->upload,
                                            j->folder && !j->mode, &error);
        ok = argv != NULL;
    }
    if (ok)
    {
        argv = client_config (argv, j->root);
        g_free (argv[0]);
        argv[0] = g_strdup (j->client);
        if (!g_object_get_data (G_OBJECT (j->spawn), "insight-ssh-askpass"))
        {
            g_free (argv[1]);
            argv[1] = g_strdup ("-oBatchMode=yes");
        }
        ok = scp_child (j, (const gchar *const *)argv, !j->upload && j->mode ? j->stage : NULL, &error);
    }
    g_strfreev (argv);
    if (ok && !j->upload)
    {
        GFile *from = g_file_new_for_path (j->stage),
              *to = g_file_new_for_path (
                  j->local); /* No overwrite, including a target created during transfer. */
        ok = g_file_move (from, to, G_FILE_COPY_NOFOLLOW_SYMLINKS, cancel, NULL, NULL, &error);
        g_object_unref (from);
        g_object_unref (to);
    }
    if (ok)
        g_task_return_boolean (task, TRUE);
    else if (error)
        g_task_return_error (task, error);
    else
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED, "Transfer failed");
}
/* EN: Consume worker result; object/data are unused; report completion only for the current job.
 * RU: Принимает результат result; object/data не используются; сообщает итог только текущего задания. */
static void
scp_done (GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object;
    (void)data;
    ScpJob *j = g_task_get_task_data (G_TASK (result));
    GError *error = NULL;
    gboolean ok = g_task_propagate_boolean (G_TASK (result), &error);
    if (remote_ui.scp == j)
    {
        remote_ui.scp = NULL;
        if (!remote_ui.closing)
        {
            const gchar *message = ok ? "Transfer completed; terminal connections retained" : error->message;
            gtk_label_set_text (GTK_LABEL (remote_ui.transfer_status), message);
            status (message);
        }
    }
    g_clear_error (&error);
}
/* EN: Start explicit upload when data is true, otherwise download; widget is unused and validation errors remain visible.
 * RU: Запускает отправку при истинном data, иначе получение; widget не используется, ошибки видны в панели. */
static void
scp_start (GtkWidget *widget, gpointer data)
{
    (void)widget;
    if (remote_ui.scp)
    {
        status ("A file transfer is already running. Stop it before starting another.");
        return;
    }
    const gchar *local = gtk_entry_get_text (GTK_ENTRY (remote_ui.transfer_local)),
                *remote = gtk_entry_get_text (GTK_ENTRY (remote_ui.transfer_remote));
    gboolean upload = GPOINTER_TO_INT (data),
             folder = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (remote_ui.transfer_folder));
    gint mode = gtk_combo_box_get_active (GTK_COMBO_BOX (remote_ui.transfer_mode));
    GError *error = NULL;
    if (!g_path_is_absolute (local) || (!upload && g_file_test (local, G_FILE_TEST_EXISTS))
        || (upload && !g_file_test (local, folder ? G_FILE_TEST_IS_DIR : G_FILE_TEST_IS_REGULAR))
        || (folder && mode == 1))
    {
        status ("Choose an absolute local path and matching File/Folder mode. Receive requires a new "
                "destination. gz accepts one file.");
        return;
    }
    ScpJob *j = g_new0 (ScpJob, 1);
    g_mutex_init (&j->lock);
    j->cancel = g_cancellable_new ();
    j->root = form_root (remote, local, remote_ui.directory, &error);
    j->upload = upload;
    j->folder = folder;
    j->mode = mode;
    j->local = g_strdup (local);
    if (j->root)
    {
        j->client = client_program (!upload && mode ? "ssh" : "scp");
        if (!j->client)
            g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                 "Install the OpenSSH scp/ssh clients");
    }
    if (j->root && !error)
    {
        j->spawn = launcher (j->root, &j->auth);
        gchar *parent = upload ? g_strdup (remote_ui.directory) : g_path_get_dirname (local);
        gchar *template = g_build_filename (parent, ".geany-transfer-XXXXXX", NULL);
        g_free (parent);
        j->temporary = g_mkdtemp (template);
        if (!j->temporary)
        {
            g_free (template);
            g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "Cannot create private transfer staging directory");
        }
        else
        {
            j->stage = g_build_filename (j->temporary,
                                         mode == 1   ? "payload.gz"
                                         : mode == 2 ? "payload.tar.gz"
                                                     : "payload",
                                         NULL);
        }
    }
    if (!error && upload && mode)
    {
        j->python = client_program ("python3");
        if (!j->python)
            j->python = client_program ("python");
        j->helper = g_build_filename (app->datadir, "insight", "tools", "remote_pack.py", NULL);
        if (!j->python || !g_file_test (j->helper, G_FILE_TEST_IS_REGULAR))
            g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                 "Archive upload requires Python 3 and the installed remote_pack.py helper");
    }
    if (error || !j->root)
    {
        status (error ? error->message : "Invalid transfer settings");
        g_clear_error (&error);
        scp_free (j);
        return;
    }
    remote_ui.scp = j;
    gtk_label_set_text (GTK_LABEL (remote_ui.transfer_status),
                        "Transferring… Stop cancels packing and transfer; SSH sessions stay connected.");
    GTask *task = g_task_new (NULL, j->cancel, scp_done, NULL);
    g_task_set_task_data (task, j, scp_free);
    g_task_run_in_thread (task, scp_worker);
    g_object_unref (task);
}
/* EN: Choose a local file/folder for the transfer form; widget/data are unused.
 * RU: Выбирает локальный файл/каталог для передачи; widget/data не используются. */
static void
scp_choose (GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    gboolean folder = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (remote_ui.transfer_folder));
    GtkWidget *d = gtk_file_chooser_dialog_new (
        "Choose local source", GTK_WINDOW (main_widgets.window),
        folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel",
        GTK_RESPONSE_CANCEL, "Choose", GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run (GTK_DIALOG (d)) == GTK_RESPONSE_ACCEPT)
    {
        gchar *path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (d));
        gtk_entry_set_text (GTK_ENTRY (remote_ui.transfer_local), path);
        g_free (path);
    }
    gtk_widget_destroy (d);
}
