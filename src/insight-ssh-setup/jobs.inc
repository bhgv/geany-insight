/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Run bounded cancellable OpenSSH operations off the GTK main thread.
 * RU: Выполняет ограниченные отменяемые операции OpenSSH вне основного потока
 * GTK. */

/* EN: Stop a network operation after its bounded deadline; data retains the wizard window.
 * RU: Останавливает сетевую операцию по истечении срока; data удерживает окно мастера. */
static gboolean setup_timeout(gpointer data)
{
    SshSetupWizard *wizard = g_object_get_data(G_OBJECT(data), "insight-ssh-setup");
    wizard->timer = 0;
    wizard->timed_out = TRUE;
    if (wizard->cancel)
        g_cancellable_cancel(wizard->cancel);
    if (wizard->process)
        g_subprocess_force_exit(wizard->process);
    return G_SOURCE_REMOVE;
}

/* EN: Retain output/error for wizard's last check and explain a sudo terminal requirement without blaming SSH authentication.
 * RU: Сохраняет output/error последней проверки wizard и объясняет запрос терминала sudo без обвинения SSH-авторизации. */
static void setup_check_diagnostics(SshSetupWizard *wizard, const gchar *output, const GError *error)
{
    gchar *raw = setup_failure_output(error ? error->message : (output ? output : ""));
    gboolean sudo_terminal = strstr(raw, "sudo:") &&
        (strstr(raw, "terminal is required") || strstr(raw, "no tty present"));
    const gchar *hint = sudo_terminal ? insight_tr(
        "SSH reached the server, but remote startup requested sudo without a terminal. Use an interactive SSH console. For debugging, remote startup scripts must also allow noninteractive commands.") : "";
    gchar *text = *hint ? g_strconcat(raw, "\n\n", hint, NULL) : g_strdup(raw);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(wizard->diagnostics)), text, -1);
    if (sudo_terminal)
        gtk_label_set_text(GTK_LABEL(wizard->status), hint);
    g_free(text);
    g_free(raw);
}

/* EN: Finish result for window data, ignoring callbacks after destruction;
 * source is unused. RU: Завершает result для окна data, игнорируя ответы после
 * уничтожения; source не используется. */
static void setup_job_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    GtkWidget *window = data;
    SshSetupWizard *wizard = g_object_get_data(G_OBJECT(window), "insight-ssh-setup");
    SetupTask *job = g_task_get_task_data(G_TASK(result));
    GError *error = NULL;
    gchar *output = g_task_propagate_pointer(G_TASK(result), &error);
    if (wizard->timed_out)
    {
        g_clear_pointer(&output, g_free);
        g_clear_error(&error);
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                            "SSH operation timed out. Check the network and server, then retry or go Back.");
    }
    if (wizard->timer)
        g_source_remove(wizard->timer);
    wizard->timer = 0;
    g_clear_object(&wizard->process);
    g_clear_object(&wizard->cancel);
    insight_ssh_auth_cleanup(wizard->auth);
    g_clear_pointer(&wizard->auth, g_free);
    if (!wizard->closed)
    {
        if (output && job->kind == SETUP_GENERATE)
        {
            gchar *from = g_build_filename(wizard->key_stage, "id_ed25519", NULL);
            GFile *file = g_file_new_for_path(from), *to = g_file_new_for_path(wizard->key_destination);
            if (g_file_move(file, to, G_FILE_COPY_NONE, NULL, NULL, NULL, &error))
            {
                gtk_entry_set_text(GTK_ENTRY(wizard->identity), wizard->key_destination);
                gtk_label_set_text(GTK_LABEL(wizard->status),
                                   insight_tr("Private key created on this computer. "
                                              "Obtaining its public key…"));
            }
            g_object_unref(file);
            g_object_unref(to);
            g_free(from);
        }
        else if (output && job->kind == SETUP_PUBLIC)
        {
            gchar *key = insight_ssh_public_key(output, &error);
            if (key)
            {
                g_free(wizard->public_key);
                wizard->public_key = key;
                gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(wizard->public_text)), key,
                                         -1);
                gtk_label_set_text(GTK_LABEL(wizard->status),
                                   insight_tr("Public key ready. Install it on this server or copy it "
                                              "for your administrator."));
            }
        }
        else if (output && job->kind == SETUP_INSTALL)
            gtk_label_set_text(GTK_LABEL(wizard->status),
                               insight_tr("Public key installed. Existing server keys were "
                                          "preserved. Check the connection next."));
        else if (output && job->kind == SETUP_TEST)
        {
            wizard->verified = FALSE;
            gchar **lines = g_strsplit(output, "\n", -1);
            for (guint i = 0; lines[i]; i++)
                if (!strcmp(g_strchomp(lines[i]), "GEANY-SSH-READY"))
                    wizard->verified = TRUE;
            g_strfreev(lines);
            gtk_label_set_text(
                GTK_LABEL(wizard->status),
                insight_tr(
                    wizard->verified
                        ? (wizard->debugging
                               ? "Verified: SSH login, executable file and working directory. Next: Connect."
                               : "Connection verified. Continue to connect.")
                        : "The server did not confirm the connection."));
        }
        if (error)
        {
            gchar *valid = g_utf8_make_valid(
                insight_tr(
                    wizard->timed_out
                        ? "SSH operation timed out. Check the network and server, then retry or go Back."
                        : error->message),
                -1);
            gtk_label_set_text(GTK_LABEL(wizard->status), valid);
            g_free(valid);
        }
        if (job->kind == SETUP_TEST)
            setup_check_diagnostics(wizard, output, error);
        setup_update(wizard);
        if (output && job->kind == SETUP_GENERATE && !error)
            setup_job_start(wizard, SETUP_PUBLIC);
    }
    g_clear_error(&error);
    g_free(output);
    g_object_unref(window);
}

/* EN: Start kind for wizard with a private askpass session; validate before
 * spawning and never log passwords. RU: Запускает kind для wizard с закрытым
 * сеансом askpass; проверяет ввод до запуска и не журналирует пароли. */
static void setup_job_start(SshSetupWizard *wizard, SetupJob kind)
{
    if (wizard->process || wizard->closed)
        return;
    wizard->timed_out = FALSE;
    if (kind == SETUP_TEST)
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(wizard->diagnostics)), "", -1);
    wizard->verified = FALSE;
    InsightSshSetup settings = {0};
    GError *error = NULL;
    if (!setup_collect(wizard, &settings, &error))
        goto failed;
    gchar *ssh = insight_remote_program("ssh", wizard->client_bin);
    gchar *keygen = insight_remote_program("ssh-keygen", wizard->client_bin);
    gchar *helper = insight_remote_program("geany-insight-askpass", wizard->client_bin);
    gchar **argv = NULL;
    gchar *input = NULL, *command = NULL;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    wizard->auth =
        insight_ssh_auth_prepare(launcher, wizard->configdir, settings.host, settings.user, settings.port,
                                 settings.identity, helper, settings.remember, &error);
    if (!error && *gtk_entry_get_text(GTK_ENTRY(wizard->password)))
    {
        if (insight_ssh_auth_seed(launcher, gtk_entry_get_text(GTK_ENTRY(wizard->password)), &error) &&
            settings.remember)
            gtk_entry_set_text(GTK_ENTRY(wizard->password), "");
    }
    if (!error && (kind == SETUP_GENERATE || kind == SETUP_PUBLIC))
    {
        if (!keygen)
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                "Install the OpenSSH ssh-keygen program.");
        else if (kind == SETUP_GENERATE)
        {
            gchar *path = g_build_filename(wizard->key_stage, "id_ed25519", NULL);
#ifdef G_OS_WIN32
            /* EN: GLib's Windows quoting drops empty argv values; answer keygen's two empty prompts instead.
             * RU: Экранирование GLib Windows теряет пустые argv; отвечаем на два запроса keygen пустыми
             * строками. */
            const gchar *args[] = {keygen, "-q", "-t", "ed25519", "-f", path, NULL};
            input = g_strdup("\n\n");
            g_subprocess_launcher_setenv(launcher, "GEANY_INSIGHT_SSH_KEYGEN_EMPTY_PASSPHRASE", "1", TRUE);
#else
            const gchar *args[] = {keygen, "-q", "-t", "ed25519", "-N", "", "-f", path, NULL};
#endif
            argv = g_strdupv((gchar **)args);
            g_free(path);
        }
        else if (!*settings.identity || !g_file_test(settings.identity, G_FILE_TEST_IS_REGULAR))
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "Choose a valid SSH private key to obtain its public key.");
        else
        {
            gchar *absolute_identity = insight_canonicalize_filename(settings.identity, NULL);
            const gchar *args[] = {keygen, "-y", "-f", absolute_identity, NULL};
            argv = g_strdupv((gchar **)args);
            g_free(absolute_identity);
        }
    }
    else if (!error)
    {
        if (!ssh)
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Install the OpenSSH client.");
        else if (kind == SETUP_INSTALL && !wizard->public_key)
            g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "Obtain or create a public key first.");
        else
        {
            command = kind == SETUP_INSTALL ? insight_ssh_install_key_command(NULL, &error)
                      : wizard->debugging
                          ? insight_ssh_debug_check_command(settings.program, settings.directory,
                                                            settings.start_server, &error)
                          : insight_ssh_check_command(NULL, &error);
            if (kind == SETUP_INSTALL)
                input = g_strconcat(wizard->public_key, "\n", NULL);
            InsightRemote *remote =
                command ? insight_remote_new(settings.host, settings.user, settings.port, settings.identity,
                                             "/", wizard->configdir, &error)
                        : NULL;
            if (remote && command)
                argv = insight_ssh_setup_argv(remote, ssh, settings.config, command, helper != NULL);
            insight_remote_free(remote);
        }
    }
    if (!error && argv)
        wizard->process = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv, &error);
    g_strfreev(argv);
    g_free(command);
    g_object_unref(launcher);
    g_free(ssh);
    g_free(keygen);
    g_free(helper);
    if (wizard->process)
    {
        wizard->cancel = g_cancellable_new();
        if (kind == SETUP_TEST || kind == SETUP_INSTALL)
            wizard->timer = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, 120, setup_timeout,
                                                       g_object_ref(wizard->window), g_object_unref);
        SetupTask *job = g_new0(SetupTask, 1);
        job->process = g_object_ref(wizard->process);
        job->input = input;
        job->kind = kind;
        GTask *task = g_task_new(NULL, wizard->cancel, setup_job_done, g_object_ref(wizard->window));
        g_task_set_task_data(task, job, setup_task_free);
        gtk_label_set_text(GTK_LABEL(wizard->status), insight_tr("Working… You can cancel at any time."));
        setup_update(wizard);
        g_task_run_in_thread(task, setup_worker);
        g_object_unref(task);
        return;
    }
    g_free(input);
    insight_ssh_auth_cleanup(wizard->auth);
    g_clear_pointer(&wizard->auth, g_free);
failed:
    gtk_label_set_text(GTK_LABEL(wizard->status),
                       error ? error->message : insight_tr("SSH setup could not start."));
    g_clear_error(&error);
}
