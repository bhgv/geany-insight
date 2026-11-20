/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Lay out readable server, key, program, verification and
 * connection-summary steps. RU: Размещает понятные шаги сервера, ключа,
 * программы, проверки и итогового подключения. */

/* EN: Add wrapped explanatory text to box; GTK owns the returned label.
 * RU: Добавляет пояснение text с переносом в box; GTK владеет возвращённой
 * подписью. */
static GtkWidget *setup_note(GtkWidget *box, const gchar *text)
{
    GtkWidget *label = gtk_label_new(insight_tr(text));
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 3);
    return label;
}

/* EN: Add a labelled entry to box with initial value and stable name; GTK owns
 * the result. RU: Добавляет в box подписанное поле с исходным value и
 * устойчивым name; GTK владеет результатом. */
static GtkWidget *setup_entry(GtkWidget *box, const gchar *label, const gchar *value, const gchar *name)
{
    setup_note(box, label);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 4096);
    gtk_entry_set_text(GTK_ENTRY(entry), value ? value : "");
    gtk_widget_set_name(entry, name);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    return entry;
}

/* EN: Add labelled numeric port value to box with stable name, returning the
 * GTK-owned spinner. RU: Добавляет в box подписанный порт value с устойчивым
 * name, возвращая спиннер GTK. */
static GtkWidget *setup_port(GtkWidget *box, const gchar *label, guint value, const gchar *name)
{
    setup_note(box, label);
    GtkWidget *spin = gtk_spin_button_new_with_range(1, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value ? value : 22);
    gtk_widget_set_name(spin, name);
    gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
    return spin;
}

/* EN: Add a button labelled text to box; name identifies it for
 * accessibility/tests and callback receives wizard. RU: Добавляет кнопку text в
 * box; name задаёт имя для доступности/тестов, callback получает wizard. */
static GtkWidget *setup_button(GtkWidget *box, const gchar *text, const gchar *name, GCallback callback,
                               SshSetupWizard *wizard)
{
    GtkWidget *button = gtk_button_new_with_label(insight_tr(text));
    gtk_widget_set_name(button, name);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    g_signal_connect(button, "clicked", callback, wizard);
    return button;
}

/* EN: Add a job button to box for kind using wizard; name/text are stable
 * identification and visible caption. RU: Добавляет кнопку задания kind в box
 * для wizard; name/text задают устойчивое имя и видимую подпись. */
static void setup_job_button(GtkWidget *box, const gchar *text, const gchar *name, SetupJob kind,
                             SshSetupWizard *wizard)
{
    GtkWidget *button = setup_button(box, text, name, G_CALLBACK(setup_operation), wizard);
    g_object_set_data(G_OBJECT(button), "ssh-setup-job", GUINT_TO_POINTER(kind));
}

/* EN: Add numbered page to wizard's stack and return its GTK-owned vertical
 * content box. RU: Добавляет пронумерованную страницу в стек wizard и
 * возвращает её вертикальный контейнер GTK. */
static GtkWidget *setup_page(SshSetupWizard *wizard, guint number)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gchar *name = g_strdup_printf("step%u", number);
    gtk_stack_add_named(GTK_STACK(wizard->stack), box, name);
    g_free(name);
    return box;
}

/* EN: Let wizard data choose a private key for button or a local source
 * directory when its folder flag is set. RU: Выбирает закрытый ключ для button
 * в wizard data либо локальный каталог исходников при флаге folder. */
static void setup_browse(GtkButton *button, gpointer data)
{
    SshSetupWizard *wizard = data;
    gboolean folder = g_object_get_data(G_OBJECT(button), "ssh-folder") != NULL;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        insight_tr(folder ? "Choose local source directory" : "Choose an existing private SSH key"),
        GTK_WINDOW(wizard->window),
        folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN, insight_tr("Cancel"),
        GTK_RESPONSE_CANCEL, insight_tr("Choose"), GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        gchar *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(folder ? wizard->local_source : wizard->identity), path);
        g_free(path);
    }
    gtk_widget_destroy(dialog);
}

/* EN: Build all pages for wizard from initial nonsecret settings; key creation
 * and tests require explicit button clicks. RU: Создаёт страницы wizard по
 * исходным несекретным initial; создание ключа и проверки требуют нажатия
 * кнопки. */
static void setup_pages(SshSetupWizard *wizard, const InsightSshSetup *initial)
{
    GtkWidget *box = setup_page(wizard, 0);
    setup_note(box, "Enter the server address and your account there. Passwords "
                    "and keys work for terminals, files and debugging.");
    wizard->host = setup_entry(box, "Server address or SSH alias", initial->host, "insight-ssh-setup-host");
    wizard->user = setup_entry(box, "Login on the server (empty: use SSH config)", initial->user,
                               "insight-ssh-setup-user");
    wizard->port = setup_port(box, "SSH port (0: SSH config)", initial->port, "insight-ssh-setup-port");
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(wizard->port), 0, 65535);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(wizard->port), initial->port);
    wizard->password = setup_entry(box, "Password (empty: use a saved password, key or agent)", "",
                                   "insight-ssh-setup-password");
    gtk_entry_set_visibility(GTK_ENTRY(wizard->password), FALSE);
    wizard->remember = gtk_check_button_new_with_label(insight_tr("Remember passwords on this computer"));
    gtk_widget_set_name(wizard->remember, "insight-ssh-setup-remember");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wizard->remember), initial->remember);
    gtk_box_pack_start(GTK_BOX(box), wizard->remember, FALSE, FALSE, 0);
    setup_note(box, "Saved without encryption in a private settings file on this computer, outside project "
                    "files. Leave this off to remember only for this connection.");
    setup_button(box, "Forget saved SSH passwords", "insight-ssh-setup-forget", G_CALLBACK(setup_forget),
                 wizard);
    wizard->command = setup_entry(box, "SSH command", "ssh ", "insight-ssh-setup-command");
    gtk_entry_set_placeholder_text(GTK_ENTRY(wizard->command), "ssh user@server");
    setup_note(box, "Paste a working ssh command. This console uses normal keyboard authentication, not saved editor passwords. Enter passwords only at the SSH prompt.");
    setup_button(box, "Fill command from fields", "insight-ssh-command-fill", G_CALLBACK(setup_command_fill), wizard);
    wizard->console_close = gtk_check_button_new_with_label(insight_tr("Close console when SSH exits"));
    gtk_widget_set_name(wizard->console_close, "insight-ssh-console-close");
    gtk_box_pack_start(GTK_BOX(box), wizard->console_close, FALSE, FALSE, 0);
    setup_button(box, "Open SSH console", "insight-ssh-command-open", G_CALLBACK(setup_command_open), wizard);
    box = setup_page(wizard, 1);
    setup_note(box, "Optional: use an existing private key, or create a new one "
                    "here. The new private key has no passphrase and stays on this computer. Only the public "
                    "key is installed on the server.");
    wizard->identity = setup_entry(box, "Private key on this computer (optional)", initial->identity,
                                   "insight-ssh-setup-key");
    setup_button(box, "Choose existing private key…", "insight-ssh-setup-key-choose",
                 G_CALLBACK(setup_browse), wizard);
    setup_button(box, "Create a key for this server", "insight-ssh-setup-key-create",
                 G_CALLBACK(setup_key_create), wizard);
    setup_job_button(box, "Obtain public key", "insight-ssh-setup-key-public", SETUP_PUBLIC, wizard);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, -1, 90);
    wizard->public_text = gtk_text_view_new();
    gtk_widget_set_name(wizard->public_text, "insight-ssh-setup-public-key");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(wizard->public_text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(wizard->public_text), GTK_WRAP_CHAR);
    gtk_container_add(GTK_CONTAINER(scroll), wizard->public_text);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    setup_button(box, "Copy public key", "insight-ssh-setup-key-copy", G_CALLBACK(setup_key_copy), wizard);
    setup_job_button(box, "Install public key on this server", "insight-ssh-setup-key-install", SETUP_INSTALL,
                     wizard);
    wizard->config =
        setup_entry(box, "SSH config file (optional)", initial->config, "insight-ssh-setup-config");
    if (wizard->debugging)
    {
        box = setup_page(wizard, 2);
        setup_note(box, "The local program supplies debug symbols. Choose the "
                        "matching program on the server. The editor can start "
                        "gdbserver and create its SSH tunnel.");
        wizard->program = setup_entry(box, "Program on the server (absolute path)", initial->program,
                                      "insight-ssh-setup-program");
        wizard->directory = setup_entry(box, "Working directory on the server (empty: program folder)",
                                        initial->directory, "insight-ssh-setup-directory");
        wizard->remote_source = setup_entry(box, "Source directory used by the remote build (optional)",
                                            initial->remote_source, "insight-ssh-setup-remote-source");
        wizard->local_source = setup_entry(box, "Matching source directory on this computer (optional)",
                                           initial->local_source, "insight-ssh-setup-local-source");
        GtkWidget *choose = setup_button(box, "Choose local source directory…",
                                         "insight-ssh-setup-source-choose", G_CALLBACK(setup_browse), wizard);
        g_object_set_data(G_OBJECT(choose), "ssh-folder", GINT_TO_POINTER(1));
        wizard->server_port =
            setup_port(box, "Debug server port (different from SSH port)",
                       initial->server_port ? initial->server_port : 2345, "insight-ssh-setup-server-port");
        wizard->start_server =
            gtk_check_button_new_with_label(insight_tr("Start gdbserver automatically on the server"));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wizard->start_server), initial->start_server);
        gtk_widget_set_name(wizard->start_server, "insight-ssh-setup-start-server");
        gtk_box_pack_start(GTK_BOX(box), wizard->start_server, FALSE, FALSE, 0);
        setup_note(box, "The remote program is not uploaded automatically. Keep a "
                        "matching local copy with debug symbols. If gdbserver is "
                        "already running, turn automatic startup off.");
    }
    guint check = wizard->debugging ? 3 : 2;
    box = setup_page(wizard, check);
    setup_note(box, "Check the connection before continuing. SSH will ask you to "
                    "confirm a new server key and any missing password.");
    setup_job_button(box, "Check connection", "insight-ssh-setup-check", SETUP_TEST, wizard);
    setup_note(box, "If the check fails, read the explanation below, go back and correct the "
                    "server, login or key. No application is started by this check.");
    setup_note(box, "SSH diagnostics (kept until the next check)");
    GtkWidget *diagnostic_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(diagnostic_scroll, -1, 130);
    wizard->diagnostics = gtk_text_view_new();
    gtk_widget_set_name(wizard->diagnostics, "insight-ssh-setup-diagnostics");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(wizard->diagnostics), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(wizard->diagnostics), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(wizard->diagnostics), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(diagnostic_scroll), wizard->diagnostics);
    gtk_box_pack_start(GTK_BOX(box), diagnostic_scroll, TRUE, TRUE, 0);
    if (!wizard->debugging)
    {
        setup_note(box, "Some servers require an interactive terminal during login. You can open the SSH terminal without this command check; the connection is not marked verified.");
        setup_button(box, "Connect terminal without command check", "insight-ssh-connect-unchecked",
                     G_CALLBACK(setup_connect_unchecked), wizard);
    }
    box = setup_page(wizard, check + 1);
    wizard->summary = setup_note(box, "Connection verified.");
}
