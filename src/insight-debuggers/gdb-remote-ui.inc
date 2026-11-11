/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Provide the local/SSH connection form, explicit connect and non-killing detach actions.
 * RU: Создаёт форму локального/SSH-подключения, явное подключение и отсоединение без kill. */

/* EN: Connect or detach session data from button; reject incomplete paths before opening SSH.
 * RU: Подключает или отсоединяет сеанс data по button; проверяет пути до открытия SSH. */
static void native_remote_clicked(GtkButton *button, gpointer data)
{
    NativeDebug *session = data;
    NativeRemote *remote = session->remote;
    if (GTK_WIDGET(button) == remote->disconnect)
    {
        if (remote->busy && !remote->connected)
        {
            insight_queue_clear_full(&session->launch_commands, g_free);
            native_remote_drop(session);
            if (session->pending)
                remote->pending = NATIVE_REMOTE_DISCONNECT;
            else
            {
                native_remote_restore(session);
                native_debug_resume(session->window, session);
            }
            gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("SSH connection cancelled."));
            native_debug_update(session);
            return;
        }
        if (!session->ready || session->pending || session->writing)
            return;
        remote->busy = TRUE;
        remote->detach_needed = session->running || session->stopped;
        remote->disconnect_needed = !remote->detach_needed;
        if (session->running)
            native_debug_send(session->window, "-exec-interrupt --all");
        else
            native_debug_resume(session->window, session);
        native_debug_update(session);
        return;
    }
    if (!session->ready || session->pending || session->writing || session->running || session->stopped ||
        remote->ssh || !native_remote_selected(session))
        return;
    const gchar *host = gtk_entry_get_text(GTK_ENTRY(remote->host));
    const gchar *server = gtk_entry_get_text(GTK_ENTRY(remote->server_host));
    const gchar *identity = gtk_entry_get_text(GTK_ENTRY(remote->identity));
    const gchar *config = gtk_entry_get_text(GTK_ENTRY(remote->config));
    const gchar *from = gtk_entry_get_text(GTK_ENTRY(remote->remote_source));
    const gchar *to = gtk_entry_get_text(GTK_ENTRY(remote->local_source));
    const gchar *sysroot = gtk_entry_get_text(GTK_ENTRY(remote->sysroot));
    gboolean valid = !strpbrk(sysroot, "\r\n") && native_remote_host_valid(host) && native_remote_host_valid(server) &&
        (!*identity || g_file_test(identity, G_FILE_TEST_IS_REGULAR)) &&
        (!*config || g_file_test(config, G_FILE_TEST_IS_REGULAR)) &&
        ((!*from && !*to) || (*from && g_path_is_absolute(to) && g_file_test(to, G_FILE_TEST_IS_DIR)));
    if (!valid)
    {
        gtk_label_set_text(GTK_LABEL(remote->status), insight_tr(
            "Enter valid SSH/server hosts, existing optional key/config files, and both source roots or neither."));
        return;
    }
    remote->extended = gtk_combo_box_get_active(GTK_COMBO_BOX(remote->mode)) == 2;
    native_remote_spawn(session);
}

/* EN: Refresh mode-dependent controls for session data when combo changes; combo is unused.
 * RU: Обновляет зависящие от режима элементы сеанса data; combo не используется. */
static void native_remote_mode(GtkComboBox *combo, gpointer data)
{
    (void)combo;
    native_debug_update(data);
}

/* EN: Add a labelled entry with initial text/name to grid at row, returning the created widget.
 * RU: Добавляет подписанное поле с text/name в grid на row, возвращая созданный элемент. */
static GtkWidget *native_remote_entry(GtkGrid *grid, gint row, const gchar *label,
                                      const gchar *text, const gchar *name)
{
    GtkWidget *caption = gtk_label_new(insight_tr(label));
    gtk_widget_set_halign(caption, GTK_ALIGN_START);
    gtk_grid_attach(grid, caption, 0, row, 1, 1);
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 4096);
    gtk_entry_set_text(GTK_ENTRY(entry), text);
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_widget_set_name(entry, name);
    gtk_grid_attach(grid, entry, 1, row, 1, 1);
    return entry;
}

/* EN: Add a labelled TCP port spinner to grid at row with initial value and stable widget name.
 * RU: Добавляет подписанный TCP-порт в grid на row с начальным value и постоянным именем name. */
static GtkWidget *native_remote_spinner(GtkGrid *grid, gint row, const gchar *label, guint value,
                                        const gchar *name)
{
    GtkWidget *caption = gtk_label_new(insight_tr(label));
    gtk_widget_set_halign(caption, GTK_ALIGN_START);
    gtk_grid_attach(grid, caption, 0, row, 1, 1);
    GtkWidget *spin = gtk_spin_button_new_with_range(1, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_widget_set_name(spin, name);
    gtk_grid_attach(grid, spin, 1, row, 1, 1);
    return spin;
}

/* EN: Build session's remote page; target is the local symbol file, never a deployment request.
 * RU: Создаёт удалённую страницу session; target — локальный файл символов, без отправки на сервер. */
static GtkWidget *native_remote_widget(NativeDebug *session, const gchar *target)
{
    NativeRemote *remote = session->remote = g_new0(NativeRemote, 1);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    remote->mode = gtk_combo_box_text_new();
    gtk_widget_set_name(remote->mode, "insight-gdb-connection-mode");
    const gchar *modes[] = {"Local program", "SSH: existing gdbserver", "SSH: extended-remote (--multi)"};
    for (guint i = 0; i < G_N_ELEMENTS(modes); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(remote->mode), insight_tr(modes[i]));
    gtk_combo_box_set_active(GTK_COMBO_BOX(remote->mode), 0);
    gtk_box_pack_start(GTK_BOX(page), remote->mode, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *grid = remote->form = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    remote->host = native_remote_entry(GTK_GRID(grid), 0, "SSH host / config alias", "", "insight-gdb-ssh-host");
    remote->user = native_remote_entry(GTK_GRID(grid), 1, "SSH login (empty: SSH config)", "", "insight-gdb-ssh-user");
    remote->port = native_remote_spinner(GTK_GRID(grid), 2, "SSH port (0: SSH config)", 22, "insight-gdb-ssh-port");
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(remote->port), 0, 65535);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(remote->port), 0);
    remote->identity = native_remote_entry(GTK_GRID(grid), 3, "SSH identity file (optional)", "", "insight-gdb-ssh-key");
    remote->config = native_remote_entry(GTK_GRID(grid), 4, "SSH config file (optional)", "", "insight-gdb-ssh-config");
    remote->server_host = native_remote_entry(GTK_GRID(grid), 5, "gdbserver host as seen by SSH server",
        "127.0.0.1", "insight-gdb-server-host");
    remote->server_port = native_remote_spinner(GTK_GRID(grid), 6, "gdbserver port", 2345, "insight-gdb-server-port");
    remote->executable = native_remote_entry(GTK_GRID(grid), 7, "Remote executable (--multi Run)", "", "insight-gdb-remote-executable");
    remote->remote_source = native_remote_entry(GTK_GRID(grid), 8, "Source root recorded in remote build", "", "insight-gdb-remote-source");
    remote->local_source = native_remote_entry(GTK_GRID(grid), 9, "Matching local source root", "", "insight-gdb-local-source");
    remote->sysroot = native_remote_entry(GTK_GRID(grid), 10, "Local sysroot or target:", "target:", "insight-gdb-sysroot");
    GtkWidget *symbols = gtk_label_new(target);
    gtk_label_set_selectable(GTK_LABEL(symbols), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(symbols), PANGO_ELLIPSIZE_MIDDLE);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(insight_tr("Local executable / debug symbols")), 0, 11, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), symbols, 1, 11, 1, 1);
    gtk_container_add(GTK_CONTAINER(scroll), grid);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    remote->connect = gtk_button_new_with_label(insight_tr("Connect gdbserver via SSH"));
    remote->disconnect = gtk_button_new_with_label(insight_tr("Detach and close SSH"));
    gtk_widget_set_name(remote->connect, "insight-gdb-remote-connect");
    gtk_widget_set_name(remote->disconnect, "insight-gdb-remote-disconnect");
    g_signal_connect(remote->connect, "clicked", G_CALLBACK(native_remote_clicked), session);
    g_signal_connect(remote->disconnect, "clicked", G_CALLBACK(native_remote_clicked), session);
    gtk_box_pack_start(GTK_BOX(buttons), remote->connect, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), remote->disconnect, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), buttons, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), native_profile_widget(session, target), FALSE, FALSE, 0);
    remote->status = gtk_label_new(insight_tr(
        "Start gdbserver on the remote machine first. SSH uses keys/agent or the installed askpass helper."));
    gtk_widget_set_name(remote->status, "insight-gdb-remote-status");
    gtk_label_set_line_wrap(GTK_LABEL(remote->status), TRUE);
    gtk_label_set_selectable(GTK_LABEL(remote->status), TRUE);
    gtk_box_pack_start(GTK_BOX(page), remote->status, FALSE, FALSE, 0);
    g_signal_connect(remote->mode, "changed", G_CALLBACK(native_remote_mode), session);
    return page;
}
