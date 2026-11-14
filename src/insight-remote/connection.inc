/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Configure SSH clients and credential ownership.
 * RU: Настраивает SSH-клиенты и владение паролями. */

static void
root_free (gpointer data)
{
    RemoteRoot *r = data;
    if (!r)
        return;
    insight_remote_free (r->remote);
    g_free (r->directory);
    g_free (r->config);
    g_free (r->ssh_config);
    g_free (r);
}
/* EN: Return an independent owned copy of root r for an asynchronous operation.
 * RU: Возвращает независимую копию корня r для асинхронной операции. */
static RemoteRoot *
root_copy (const RemoteRoot *r)
{
    RemoteRoot *copy = g_new0 (RemoteRoot, 1);
    const InsightRemote *s = r->remote;
    copy->remote = insight_remote_new (s->host, s->user, s->port, s->identity, s->remote, s->local, NULL);
    copy->directory = g_strdup (r->directory);
    copy->config = g_strdup (r->config);
    copy->ssh_config = g_strdup (r->ssh_config);
    copy->auto_upload = r->auto_upload;
    return copy;
}
/* EN: Remove disposable authentication files beneath owned path.
 * RU: Удаляет временные файлы авторизации из собственного path. */
static void
auth_cleanup (const char *path)
{
    if (!path)
        return;
    GDir *dir = g_dir_open (path, 0, NULL);
    if (dir)
    {
        const char *name;
        while ((name = g_dir_read_name (dir)))
        {
            gchar *file = g_build_filename (path, name, NULL);
            g_remove (file);
            g_free (file);
        }
        g_dir_close (dir);
    }
    g_rmdir (path);
}
/* EN: Release transfer t, its snapshot, authentication directory and process references.
 * RU: Освобождает задание t, снимок, каталог авторизации и ссылки процесса. */
static void
transfer_free (Transfer *t)
{
    if (!t)
        return;
    root_free (t->root);
    if (t->snapshot)
        g_remove (t->snapshot);
    auth_cleanup (t->auth);
    g_free (t->auth);
    g_free (t->origin);
    g_free (t->target);
    g_free (t->snapshot);
    if (t->project)
        g_key_file_unref (t->project);
    g_clear_object (&t->process);
    g_clear_object (&t->cancel);
    g_free (t);
}
/* EN: Display message in the SSH panel and retain it in an idle terminal.
 * RU: Показывает message в панели SSH и сохраняет в свободном терминале. */
static void
status (const char *message)
{
    if (remote_ui.closing)
        return;
    gtk_label_set_text (GTK_LABEL (remote_ui.status), message);
    gtk_widget_set_tooltip_text (remote_ui.status, message);
    ui_set_statusbar (TRUE, "SSH: %s", message);
    /* Keep a selectable, persistent explanation even in a narrow panel. */
    if (remote_ui.output && (!remote_ui.selected || !remote_ui.selected->process))
    {
        gchar *line = g_strdup_printf ("\r\n[SSH] %s\r\n", message);
        insight_terminal_feed (remote_ui.output, line, strlen (line));
        g_free (line);
    }
}
/* EN: Display message in session s or the shared panel when s is NULL.
 * RU: Показывает message в сеансе s либо общей панели при NULL. */
static void
session_status (ShellSession *s, const gchar *message)
{
    if (remote_ui.closing)
        return;
    if (!s)
    {
        status (message);
        return;
    }
    if (s == remote_ui.selected)
    {
        status (message);
        return;
    }
    if (!s->process)
    {
        gchar *line = g_strdup_printf ("\r\n[SSH] %s\r\n", message);
        insight_terminal_feed (s->output, line, strlen (line));
        g_free (line);
    }
}
/* EN: Resolve program beside the application or through supported OpenSSH/PATH locations; return owned path or NULL.
 * RU: Ищет program рядом с приложением либо в OpenSSH/PATH; возвращает путь или NULL. */
static gchar *
client_program (const gchar *program)
{
    gchar *bin = NULL;
#ifdef G_OS_WIN32
    gchar *prefix = win32_get_installation_dir ();
    bin = g_build_filename (prefix, "bin", NULL);
    g_free (prefix);
#else
    gchar *executable = g_file_read_link ("/proc/self/exe", NULL);
    if (executable)
    {
        bin = g_path_get_dirname (executable);
        g_free (executable);
    }
#endif
    gchar *path = insight_remote_program (program, bin);
    g_free (bin);
    return path;
}
/* EN: Return the owned common credential identifier for endpoint r.
 * RU: Возвращает общий идентификатор пароля для сервера r. */
static gchar *
credentials_target (const InsightRemote *r)
{
    return insight_ssh_auth_target (r->host, r->user, r->port);
}

/* EN: Create a launcher for root with shared private credentials; auth receives its owned cleanup directory.
 * RU: Создаёт процесс для root с общими закрытыми паролями; auth получает каталог последующей очистки. */
static GSubprocessLauncher *
launcher (RemoteRoot *root, gchar **auth)
{
    GSubprocessLauncher *spawn = g_subprocess_launcher_new (
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
#ifdef G_OS_WIN32
    gchar *installation = win32_get_installation_dir ();
    gchar *client_bin = g_build_filename (installation, "bin", NULL);
    gchar *search_path = g_strconcat (client_bin, ";", g_getenv ("PATH") ? g_getenv ("PATH") : "", NULL);
    g_subprocess_launcher_setenv (spawn, "PATH", search_path, TRUE);
    g_free (search_path);
    g_free (client_bin);
    g_free (installation);
#endif
    gchar *helper = client_program ("geany-insight-askpass");
    GError *error = NULL;
    gboolean remember = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (remote_ui.save_password));
    *auth = insight_ssh_auth_prepare (spawn, app->configdir, root->remote->host, root->remote->user,
                                      root->remote->port, root->remote->identity, helper, remember, &error);
    if (remember && *auth)
        insight_ssh_auth_migrate (root->config, app->configdir, root->remote->host, root->remote->user,
                                  root->remote->port);
    if (error)
    {
        status (error->message);
        g_clear_error (&error);
    }
    g_object_set_data (G_OBJECT (spawn), "insight-ssh-askpass",
                       GINT_TO_POINTER (helper != NULL && *auth != NULL));
    g_free (helper);
    return spawn;
}
/* EN: Insert root's optional SSH config in owned argv before its endpoint and return owned arguments.
 * RU: Добавляет SSH-конфигурацию root в принадлежащий argv перед сервером и возвращает аргументы. */
static gchar **
client_config (gchar **argv, const RemoteRoot *root)
{
    if (!root->ssh_config || !*root->ssh_config)
        return argv;
    GPtrArray *args = g_ptr_array_new_with_free_func (g_free);
    for (guint i = 0; argv[i]; i++)
    {
        if (!strcmp (argv[i], "--"))
        {
            g_ptr_array_add (args, g_strdup ("-F"));
#ifdef G_OS_WIN32
            g_ptr_array_add(args, insight_canonicalize_filename(root->ssh_config, NULL));
#else
            g_ptr_array_add (args, g_strdup (root->ssh_config));
#endif
        }
        g_ptr_array_add (args, g_strdup (argv[i]));
    }
    g_strfreev (argv);
    g_ptr_array_add (args, NULL);
    return (gchar **)g_ptr_array_free (args, FALSE);
}

/* EN: Spawn shell/SFTP for root through spawn; return an owned process or error without a hidden password
 * prompt. RU: Запускает оболочку/SFTP root через spawn; возвращает процесс либо error без невидимого запроса
 * пароля. */
static GSubprocess *
spawn_client (GSubprocessLauncher *spawn, RemoteRoot *root, gboolean sftp, GError **error)
{
    const gchar *name = sftp ? "sftp" : "ssh";
    gchar *client = client_program (name);
    if (!client)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Install the OpenSSH client '%s'.", name);
        return NULL;
    }
    gchar **argv = client_config (insight_remote_argv (root->remote, sftp), root);
    g_free (argv[0]);
    argv[0] = client;
    if (!g_object_get_data (G_OBJECT (spawn), "insight-ssh-askpass"))
    {
        g_free (argv[1]);
        argv[1] = g_strdup ("-oBatchMode=yes");
    }
    GSubprocess *process = g_subprocess_launcher_spawnv (spawn, (const gchar *const *)argv, error);
    g_strfreev (argv);
    return process;
}
