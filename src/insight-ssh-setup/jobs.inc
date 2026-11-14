/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Run bounded cancellable OpenSSH operations off the GTK main thread.
 * RU: Выполняет ограниченные отменяемые операции OpenSSH вне основного потока
 * GTK. */

/* EN: Release the task in data and its owned process/input; input is never a
 * password. RU: Освобождает задание data и принадлежащие ему процесс/ввод;
 * input не содержит пароль. */
static void
setup_task_free (gpointer data)
{
    SetupTask *task = data;
    g_object_unref (task->process);
    g_free (task->input);
    g_free (task);
}

/* EN: Execute job data for task; source is unused and cancel interrupts both
 * streaming and waiting. RU: Выполняет data для task; source не используется,
 * cancel прерывает чтение и ожидание. */
static void
setup_worker (GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    SetupTask *job = data;
    GError *error = NULL;
    GOutputStream *input = g_subprocess_get_stdin_pipe (job->process);
    gboolean ok = TRUE;
    if (job->input)
        ok = g_output_stream_write_all (input, job->input, strlen (job->input), NULL, cancel, &error);
    if (ok)
        ok = g_output_stream_close (input, cancel, &error);
    GString *output = g_string_new (NULL);
    gchar buffer[4096];
    while (ok)
    {
        gssize size = g_input_stream_read (g_subprocess_get_stdout_pipe (job->process), buffer, sizeof buffer,
                                           cancel, &error);
        if (size < 0)
            ok = FALSE;
        else if (!size)
            break;
        else if (output->len + size > 65536)
        {
            g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "SSH setup output exceeds 64 KiB.");
            ok = FALSE;
        }
        else
            g_string_append_len (output, buffer, size);
    }
    if (ok)
        ok = g_subprocess_wait (job->process, cancel, &error);
    if (ok && !g_subprocess_get_successful (job->process))
    {
        gchar *valid = g_utf8_make_valid (output->str, MIN (output->len, (gsize)4096));
        g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                     *valid ? valid : "SSH setup command failed.");
        g_free (valid);
        ok = FALSE;
    }
    if (!ok)
    {
        g_subprocess_force_exit (job->process);
        g_subprocess_wait (job->process, NULL, NULL);
        g_string_free (output, TRUE);
        g_task_return_error (task, error);
    }
    else
        g_task_return_pointer (task, g_string_free (output, FALSE), g_free);
}

/* EN: Finish result for window data, ignoring callbacks after destruction;
 * source is unused. RU: Завершает result для окна data, игнорируя ответы после
 * уничтожения; source не используется. */
static void
setup_job_done (GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    GtkWidget *window = data;
    SshSetupWizard *wizard = g_object_get_data (G_OBJECT (window), "insight-ssh-setup");
    SetupTask *job = g_task_get_task_data (G_TASK (result));
    GError *error = NULL;
    gchar *output = g_task_propagate_pointer (G_TASK (result), &error);
    g_clear_object (&wizard->process);
    g_clear_object (&wizard->cancel);
    insight_ssh_auth_cleanup (wizard->auth);
    g_clear_pointer (&wizard->auth, g_free);
    if (!wizard->closed)
    {
        if (output && job->kind == SETUP_GENERATE)
        {
            gchar *from = g_build_filename (wizard->key_stage, "id_ed25519", NULL);
            GFile *file = g_file_new_for_path (from), *to = g_file_new_for_path (wizard->key_destination);
            if (g_file_move (file, to, G_FILE_COPY_NONE, NULL, NULL, NULL, &error))
            {
                gtk_entry_set_text (GTK_ENTRY (wizard->identity), wizard->key_destination);
                gtk_label_set_text (GTK_LABEL (wizard->status),
                                    insight_tr ("Private key created on this computer. "
                                                "Obtaining its public key…"));
            }
            g_object_unref (file);
            g_object_unref (to);
            g_free (from);
        }
        else if (output && job->kind == SETUP_PUBLIC)
        {
            gchar *key = insight_ssh_public_key (output, &error);
            if (key)
            {
                g_free (wizard->public_key);
                wizard->public_key = key;
                gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (wizard->public_text)), key,
                                          -1);
                gtk_label_set_text (GTK_LABEL (wizard->status),
                                    insight_tr ("Public key ready. Install it on this server or copy it "
                                                "for your administrator."));
            }
        }
        else if (output && job->kind == SETUP_INSTALL)
            gtk_label_set_text (GTK_LABEL (wizard->status),
                                insight_tr ("Public key installed. Existing server keys were "
                                            "preserved. Check the connection next."));
        else if (output && job->kind == SETUP_TEST)
        {
            wizard->verified = FALSE;
            gchar **lines = g_strsplit (output, "\n", -1);
            for (guint i = 0; lines[i]; i++)
                if (!strcmp (g_strchomp (lines[i]), "GEANY-SSH-READY"))
                    wizard->verified = TRUE;
            g_strfreev (lines);
            gtk_label_set_text (GTK_LABEL (wizard->status),
                                insight_tr (wizard->verified ? "Connection verified. Continue to connect."
                                                             : "The server did not confirm the connection."));
        }
        if (error)
        {
            gchar *valid = g_utf8_make_valid (insight_tr(error->message), -1);
            gtk_label_set_text (GTK_LABEL (wizard->status), valid);
            g_free (valid);
        }
        setup_update (wizard);
        if (output && job->kind == SETUP_GENERATE && !error)
            setup_job_start (wizard, SETUP_PUBLIC);
    }
    g_clear_error (&error);
    g_free (output);
    g_object_unref (window);
}

/* EN: Start kind for wizard with a private askpass session; validate before
 * spawning and never log passwords. RU: Запускает kind для wizard с закрытым
 * сеансом askpass; проверяет ввод до запуска и не журналирует пароли. */
static void
setup_job_start (SshSetupWizard *wizard, SetupJob kind)
{
    if (wizard->process || wizard->closed)
        return;
    InsightSshSetup settings = { 0 };
    GError *error = NULL;
    if (!setup_collect (wizard, &settings, &error))
        goto failed;
    gchar *ssh = insight_remote_program ("ssh", wizard->client_bin);
    gchar *keygen = insight_remote_program ("ssh-keygen", wizard->client_bin);
    gchar *helper = insight_remote_program ("geany-insight-askpass", wizard->client_bin);
    gchar **argv = NULL;
    gchar *input = NULL, *command = NULL;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new (
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    wizard->auth
        = insight_ssh_auth_prepare (launcher, wizard->configdir, settings.host, settings.user, settings.port,
                                    settings.identity, helper, settings.remember, &error);
    if (!error && *gtk_entry_get_text(GTK_ENTRY(wizard->password)))
    {
        if (insight_ssh_auth_seed(launcher, gtk_entry_get_text(GTK_ENTRY(wizard->password)), &error)
            && settings.remember)
            gtk_entry_set_text(GTK_ENTRY(wizard->password), "");
    }
    if (!error && (kind == SETUP_GENERATE || kind == SETUP_PUBLIC))
    {
        if (!keygen)
            g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                 "Install the OpenSSH ssh-keygen program.");
        else if (kind == SETUP_GENERATE)
        {
            gchar *path = g_build_filename (wizard->key_stage, "id_ed25519", NULL);
#ifdef G_OS_WIN32
            /* EN: GLib's Windows quoting drops empty argv values; answer keygen's two empty prompts instead.
             * RU: Экранирование GLib Windows теряет пустые argv; отвечаем на два запроса keygen пустыми строками. */
            const gchar *args[] = {keygen, "-q", "-t", "ed25519", "-f", path, NULL};
            input = g_strdup("\n\n");
            g_subprocess_launcher_setenv(launcher, "GEANY_INSIGHT_SSH_KEYGEN_EMPTY_PASSPHRASE", "1", TRUE);
#else
            const gchar *args[] = { keygen, "-q", "-t", "ed25519", "-N", "", "-f", path, NULL };
#endif
            argv = g_strdupv ((gchar **)args);
            g_free (path);
        }
        else if (!*settings.identity || !g_file_test(settings.identity, G_FILE_TEST_IS_REGULAR))
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                "Choose a valid SSH private key to obtain its public key.");
        else
        {
            const gchar *args[] = { keygen, "-y", "-f", settings.identity, NULL };
            argv = g_strdupv ((gchar **)args);
        }
    }
    else if (!error)
    {
        if (!ssh)
            g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Install the OpenSSH client.");
        else if (kind == SETUP_INSTALL && !wizard->public_key)
            g_set_error_literal (&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                 "Obtain or create a public key first.");
        else
        {
            command = kind == SETUP_INSTALL
                          ? insight_ssh_install_key_command (NULL, &error)
                          : insight_ssh_check_command (wizard->debugging ? settings.program : NULL, &error);
            if (kind == SETUP_INSTALL)
                input = g_strconcat (wizard->public_key, "\n", NULL);
            InsightRemote *remote
                = command ? insight_remote_new (settings.host, settings.user, settings.port,
                                                settings.identity, "/", wizard->configdir, &error)
                          : NULL;
            if (remote && command)
                argv = insight_ssh_setup_argv (remote, ssh, settings.config, command, helper != NULL);
            insight_remote_free (remote);
        }
    }
    if (!error && argv)
        wizard->process = g_subprocess_launcher_spawnv (launcher, (const gchar *const *)argv, &error);
    g_strfreev (argv);
    g_free (command);
    g_object_unref (launcher);
    g_free (ssh);
    g_free (keygen);
    g_free (helper);
    if (wizard->process)
    {
        wizard->cancel = g_cancellable_new ();
        SetupTask *job = g_new0 (SetupTask, 1);
        job->process = g_object_ref (wizard->process);
        job->input = input;
        job->kind = kind;
        GTask *task = g_task_new (NULL, wizard->cancel, setup_job_done, g_object_ref (wizard->window));
        g_task_set_task_data (task, job, setup_task_free);
        gtk_label_set_text (GTK_LABEL (wizard->status), insight_tr ("Working… You can cancel at any time."));
        setup_update (wizard);
        g_task_run_in_thread (task, setup_worker);
        g_object_unref (task);
        return;
    }
    g_free (input);
    insight_ssh_auth_cleanup (wizard->auth);
    g_clear_pointer (&wizard->auth, g_free);
failed:
    gtk_label_set_text (GTK_LABEL (wizard->status),
                        error ? error->message : insight_tr ("SSH setup could not start."));
    g_clear_error (&error);
}
