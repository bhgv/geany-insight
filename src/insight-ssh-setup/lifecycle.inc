/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Validate wizard steps, retain only nonsecret connection preferences and
 * cancel owned child processes on close. RU: Проверяет шаги мастера, сохраняет
 * только несекретные параметры и отменяет собственные процессы при закрытии. */

/* EN: Collect validated settings from wizard into settings; return FALSE/error
 * without connecting. RU: Собирает проверенные параметры wizard в settings;
 * возвращает FALSE/error без подключения. */
static gboolean
setup_collect (SshSetupWizard *wizard, InsightSshSetup *settings, GError **error)
{
    gchar *host = g_strdup (gtk_entry_get_text (GTK_ENTRY (wizard->host)));
    gchar *user = g_strdup (gtk_entry_get_text (GTK_ENTRY (wizard->user)));
    g_strstrip (host);
    g_strstrip (user);
    if (strcmp (host, gtk_entry_get_text (GTK_ENTRY (wizard->host))))
        gtk_entry_set_text (GTK_ENTRY (wizard->host), host);
    if (strcmp (user, gtk_entry_get_text (GTK_ENTRY (wizard->user))))
        gtk_entry_set_text (GTK_ENTRY (wizard->user), user);
    g_free (host);
    g_free (user);
    settings->host = gtk_entry_get_text (GTK_ENTRY (wizard->host));
    settings->user = gtk_entry_get_text (GTK_ENTRY (wizard->user));
    settings->port = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (wizard->port));
    settings->identity = gtk_entry_get_text (GTK_ENTRY (wizard->identity));
    settings->config = gtk_entry_get_text (GTK_ENTRY (wizard->config));
    settings->remember = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (wizard->remember));
    InsightRemote *remote = insight_remote_new (settings->host, settings->user, settings->port,
                                                settings->identity, "/", wizard->configdir, error);
    if (!remote)
        return FALSE;
    insight_remote_free (remote);
    if (*settings->config && !g_file_test (settings->config, G_FILE_TEST_IS_REGULAR))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Choose an existing SSH config file or leave it empty.");
        return FALSE;
    }
    if (wizard->debugging)
    {
        settings->program = gtk_entry_get_text (GTK_ENTRY (wizard->program));
        settings->directory = gtk_entry_get_text (GTK_ENTRY (wizard->directory));
        settings->remote_source = gtk_entry_get_text (GTK_ENTRY (wizard->remote_source));
        settings->local_source = gtk_entry_get_text (GTK_ENTRY (wizard->local_source));
        settings->server_port = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (wizard->server_port));
        settings->start_server = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (wizard->start_server));
        if (wizard->page >= 2)
        {
            if (*settings->program != '/' || strpbrk (settings->program, "\r\n")
                || (*settings->directory
                    && (*settings->directory != '/' || strpbrk (settings->directory, "\r\n"))))
            {
                g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                     "Enter an absolute program path on the server.");
                return FALSE;
            }
            if (!*settings->remote_source && *settings->local_source)
            {
                gchar *folder = g_path_get_dirname (settings->program);
                gtk_entry_set_text (GTK_ENTRY (wizard->remote_source), folder);
                g_free (folder);
                settings->remote_source = gtk_entry_get_text (GTK_ENTRY (wizard->remote_source));
            }
            if ((*settings->remote_source || *settings->local_source)
                && (!*settings->remote_source || !g_path_is_absolute (settings->local_source)
                    || !g_file_test (settings->local_source, G_FILE_TEST_IS_DIR)))
            {
                g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                     "Set both source directories, including an "
                                     "existing local folder, or leave both empty.");
                return FALSE;
            }
        }
    }
    return TRUE;
}

/* EN: Update wizard's heading, page, navigation sensitivity and final nonsecret
 * summary. RU: Обновляет заголовок, страницу, доступность переходов и
 * несекретный итог wizard. */
static void
setup_update (SshSetupWizard *wizard)
{
    guint check = wizard->debugging ? 3 : 2, last = check + 1;
    const gchar *title = wizard->page == 0       ? "Server and login"
                         : wizard->page == 1     ? "SSH key (optional)"
                         : wizard->page == check ? "Check the connection"
                         : wizard->page == last  ? "Ready to connect"
                                                 : "Remote program";
    gchar *heading = g_strdup_printf ("%u / %u — %s", wizard->page + 1, last + 1, insight_tr (title));
    gtk_label_set_text (GTK_LABEL (wizard->heading), heading);
    g_free (heading);
    gchar *page = g_strdup_printf ("step%u", wizard->page);
    gtk_stack_set_visible_child_name (GTK_STACK (wizard->stack), page);
    g_free (page);
    gboolean busy = wizard->process != NULL;
    gtk_widget_set_sensitive (wizard->stack, !busy);
    gtk_widget_set_sensitive (wizard->back, !busy && wizard->page > 0);
    gtk_widget_set_sensitive (wizard->next, !busy && (wizard->page < check || wizard->verified));
    gtk_button_set_label (GTK_BUTTON (wizard->next), insight_tr (wizard->page == last ? "Connect" : "Next"));
    if (wizard->page == last)
    {
        gchar *summary
            = wizard->debugging
                  ? g_strdup_printf ("%s@%s:%d\n%s\n%s", gtk_entry_get_text (GTK_ENTRY (wizard->user)),
                                     gtk_entry_get_text (GTK_ENTRY (wizard->host)),
                                     gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (wizard->port)),
                                     gtk_entry_get_text (GTK_ENTRY (wizard->program)),
                                     insight_tr ("Connect will prepare remote debugging. Add a "
                                                 "breakpoint, then press Run."))
                  : g_strdup_printf ("%s@%s:%d\n%s", gtk_entry_get_text (GTK_ENTRY (wizard->user)),
                                     gtk_entry_get_text (GTK_ENTRY (wizard->host)),
                                     gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (wizard->port)),
                                     insight_tr ("Connect will open your SSH terminal."));
        gtk_label_set_text (GTK_LABEL (wizard->summary), summary);
        g_free (summary);
    }
}

/* EN: Invalidate verification after changes to control in wizard data; key
 * changes also discard the displayed public key. RU: Сбрасывает проверку при
 * изменении control в wizard data; изменение ключа также очищает открытый ключ
 * на экране. */
static void
setup_changed (GtkWidget *control, gpointer data)
{
    SshSetupWizard *wizard = data;
    wizard->verified = FALSE;
    if (control == wizard->identity)
    {
        g_clear_pointer (&wizard->public_key, g_free);
        gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (wizard->public_text)), "", -1);
    }
    setup_update (wizard);
}

/* EN: Persist only host/login/port/key/config and remember choice from settings
 * beneath wizard's configdir. RU: Сохраняет только адрес/логин/порт/ключ/конфиг
 * и выбор remember из settings в configdir wizard. */
static gboolean
setup_save (SshSetupWizard *wizard, const InsightSshSetup *settings, GError **error)
{
    GKeyFile *key = g_key_file_new ();
    g_key_file_set_string (key, "ssh", "host", settings->host);
    g_key_file_set_string (key, "ssh", "user", settings->user);
    g_key_file_set_string (key, "ssh", "identity", settings->identity);
    g_key_file_set_string (key, "ssh", "config", settings->config);
    g_key_file_set_integer (key, "ssh", "port", settings->port);
    g_key_file_set_boolean (key, "ssh", "remember", settings->remember);
    gchar *path = g_build_filename (wizard->configdir, "insight", "ssh-connection.conf", NULL);
    gchar *text = g_key_file_to_data (key, NULL, NULL);
    GFile *file = g_file_new_for_path (path);
    gboolean ok = g_file_replace_contents (file, text, strlen (text), NULL, FALSE, G_FILE_CREATE_PRIVATE,
                                           NULL, NULL, error);
    g_object_unref (file);
    g_free (text);
    g_free (path);
    g_key_file_unref (key);
    return ok;
}

/* EN: Handle navigation/connection response for wizard data; dialog is the
 * owned wizard window. RU: Обрабатывает переходы/подключение response для
 * wizard data; dialog — окно мастера. */
static void
setup_response (GtkDialog *dialog, gint response, gpointer data)
{
    SshSetupWizard *wizard = data;
    if (response == GTK_RESPONSE_CANCEL || response == GTK_RESPONSE_DELETE_EVENT)
    {
        gtk_widget_destroy (GTK_WIDGET (dialog));
        return;
    }
    if (wizard->process)
        return;
    if (response == GTK_RESPONSE_REJECT && wizard->page)
        wizard->page--;
    else if (response == GTK_RESPONSE_ACCEPT)
    {
        InsightSshSetup settings = { 0 };
        GError *error = NULL;
        if (!setup_collect (wizard, &settings, &error))
        {
            gtk_label_set_text (GTK_LABEL (wizard->status), insight_tr(error->message));
            g_clear_error (&error);
            return;
        }
        guint last = wizard->debugging ? 4 : 3;
        if (wizard->page == last && wizard->verified)
        {
            if (!setup_save (wizard, &settings, &error))
            {
                gtk_label_set_text (GTK_LABEL (wizard->status), insight_tr(error->message));
                g_clear_error (&error);
                return;
            }
            wizard->done (&settings, wizard->data);
            gtk_widget_destroy (GTK_WIDGET (dialog));
            return;
        }
        if (wizard->page < last && (wizard->page < last - 1 || wizard->verified))
            wizard->page++;
    }
    setup_update (wizard);
}

/* EN: Mark window's wizard data closed and stop its owned child process
 * immediately. RU: Помечает wizard data окна window закрытым и немедленно
 * останавливает собственный процесс. */
static void
setup_closed (GtkWidget *window, gpointer data)
{
    (void)window;
    SshSetupWizard *wizard = data;
    wizard->closed = TRUE;
    if (wizard->cancel)
        g_cancellable_cancel (wizard->cancel);
    if (wizard->process)
        g_subprocess_force_exit (wizard->process);
}

/* EN: Free wizard data after its window and asynchronous callbacks release
 * ownership. RU: Освобождает wizard data после освобождения окна и асинхронных
 * обработчиков. */
static void
setup_free (gpointer data)
{
    SshSetupWizard *wizard = data;
    insight_ssh_auth_cleanup (wizard->auth);
    insight_ssh_auth_cleanup (wizard->key_stage);
    g_clear_object (&wizard->cancel);
    g_clear_object (&wizard->process);
    g_free (wizard->configdir);
    g_free (wizard->client_bin);
    g_free (wizard->public_key);
    g_free (wizard->auth);
    g_free (wizard->key_stage);
    g_free (wizard->key_destination);
    g_free (wizard);
}

/* EN: Show a terminal/debugging wizard for parent using configdir/client_bin
 * and borrowed initial settings. done/data receive the final validated settings
 * only after explicit Connect; return the GTK-owned window. RU: Открывает
 * мастер терминала/отладки для parent с configdir/client_bin и исходными
 * initial. done/data получают проверенные параметры только по явному Connect;
 * возвращает окно GTK. */
GtkWidget *
insight_ssh_setup_show (GtkWindow *parent, const gchar *configdir, const gchar *client_bin,
                        const InsightSshSetup *initial, gboolean debugging, InsightSshSetupDone done,
                        gpointer data)
{
    SshSetupWizard *wizard = g_new0 (SshSetupWizard, 1);
    wizard->configdir = g_strdup (configdir);
    wizard->client_bin = g_strdup (client_bin);
    wizard->debugging = debugging;
    wizard->done = done;
    wizard->data = data;
    wizard->window = gtk_dialog_new_with_buttons (
        insight_tr (debugging ? "SSH debugging setup" : "SSH connection setup"), parent,
        GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, insight_tr ("Cancel"), GTK_RESPONSE_CANCEL, insight_tr ("Back"),
        GTK_RESPONSE_REJECT, insight_tr ("Next"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_widget_set_name (wizard->window, "insight-ssh-setup");
    gtk_window_set_default_size (GTK_WINDOW (wizard->window), 650, 660);
    g_object_set_data_full (G_OBJECT (wizard->window), "insight-ssh-setup", wizard, setup_free);
    wizard->next = gtk_dialog_get_widget_for_response (GTK_DIALOG (wizard->window), GTK_RESPONSE_ACCEPT);
    wizard->back = gtk_dialog_get_widget_for_response (GTK_DIALOG (wizard->window), GTK_RESPONSE_REJECT);
    gtk_widget_set_name (wizard->next, "insight-ssh-setup-next");
    gtk_widget_set_name (wizard->back, "insight-ssh-setup-back");
    GtkWidget *box = gtk_dialog_get_content_area (GTK_DIALOG (wizard->window));
    wizard->heading = setup_note (box, "SSH setup");
    wizard->stack = gtk_stack_new ();
    gtk_box_pack_start (GTK_BOX (box), wizard->stack, TRUE, TRUE, 0);
    setup_pages (wizard, initial);
    wizard->status = setup_note (box, "Choose your server to begin. Creating or installing a "
                                      "key requires an explicit click.");
    GtkWidget *entries[]
        = { wizard->host,    wizard->user,      wizard->identity,      wizard->password,    wizard->config,
            wizard->program, wizard->directory, wizard->remote_source, wizard->local_source };
    for (guint i = 0; i < G_N_ELEMENTS (entries); i++)
        if (entries[i])
            g_signal_connect (entries[i], "changed", G_CALLBACK (setup_changed), wizard);
    g_signal_connect (wizard->port, "value-changed", G_CALLBACK (setup_changed), wizard);
    g_signal_connect (wizard->remember, "toggled", G_CALLBACK (setup_changed), wizard);
    if (debugging)
    {
        g_signal_connect (wizard->server_port, "value-changed", G_CALLBACK (setup_changed), wizard);
        g_signal_connect (wizard->start_server, "toggled", G_CALLBACK (setup_changed), wizard);
    }
    if (!initial->host || !*initial->host)
    {
        gchar *path = g_build_filename (configdir, "insight", "ssh-connection.conf", NULL);
        GKeyFile *key = g_key_file_new ();
        if (g_key_file_load_from_file (key, path, G_KEY_FILE_NONE, NULL))
        {
            const gchar *names[] = { "host", "user", "identity", "config" };
            GtkWidget *fields[] = { wizard->host, wizard->user, wizard->identity, wizard->config };
            for (guint i = 0; i < 4; i++)
            {
                gchar *value = g_key_file_get_string (key, "ssh", names[i], NULL);
                gtk_entry_set_text (GTK_ENTRY (fields[i]), value ? value : "");
                g_free (value);
            }
            gint port = g_key_file_get_integer (key, "ssh", "port", NULL);
            if (port > 0 && port <= 65535)
                gtk_spin_button_set_value (GTK_SPIN_BUTTON (wizard->port), port);
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (wizard->remember),
                                          g_key_file_get_boolean (key, "ssh", "remember", NULL));
        }
        g_key_file_unref (key);
        g_free (path);
    }
    g_signal_connect (wizard->window, "response", G_CALLBACK (setup_response), wizard);
    g_signal_connect (wizard->window, "destroy", G_CALLBACK (setup_closed), wizard);
    gtk_widget_show_all (wizard->window);
    setup_update (wizard);
    return wizard->window;
}
