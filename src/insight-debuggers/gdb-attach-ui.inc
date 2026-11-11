/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Attach the SSH debugger to an existing remote process without launching a replacement.
 * RU: Подключает SSH-отладчик к существующему удалённому процессу без запуска новой программы. */

/* EN: Update session's attach controls for idle GDB; an existing target or plain remote mode forbids attach.
 * RU: Обновляет подключение session при свободном GDB idle; активная цель и обычный remote запрещают подключение. */
static void native_attach_update(NativeDebug *session, gboolean idle)
{
    NativeRemote *remote = session->remote;
    gboolean available = idle && remote->connected && remote->extended &&
        !session->running && !session->stopped;
    gchar *command = native_attach_command(gtk_entry_get_text(GTK_ENTRY(remote->pid)));
    gtk_widget_set_sensitive(remote->pid, available);
    gtk_widget_set_sensitive(remote->attach, available && command != NULL);
    g_free(command);
}

/* EN: Refresh attach availability for session in data after entry changes; no command is sent.
 * RU: Обновляет доступность подключения session в data после изменения entry; команд не отправляет. */
static void native_attach_changed(GtkEditable *entry, gpointer data)
{
    NativeDebug *session = data;
    (void)entry;
    native_debug_update(session);
}

/* EN: Attach session in data to the entered PID when button is activated; errors keep the SSH connection alive.
 * RU: Подключает session в data к введённому PID по button; ошибки оставляют SSH-подключение открытым. */
static void native_attach_clicked(GtkButton *button, gpointer data)
{
    NativeDebug *session = data;
    NativeRemote *remote = session->remote;
    (void)button;
    if (!session->ready || session->pending || session->writing || session->running || session->stopped ||
        remote->busy || !remote->connected || !remote->extended)
        return;
    gchar *command = native_attach_command(gtk_entry_get_text(GTK_ENTRY(remote->pid)));
    if (!command)
    {
        gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Enter a remote PID from 1 to 2147483647."));
        return;
    }
    remote->pending = NATIVE_REMOTE_ATTACH;
    gtk_label_set_text(GTK_LABEL(remote->status), insight_tr("Attaching to remote process…"));
    native_debug_send(session->window, command);
    g_free(command);
}

/* EN: Build session's labelled PID controls; the PID is deliberately excluded from persistent profiles.
 * RU: Создаёт подписанные элементы PID для session; PID намеренно не сохраняется в профиле. */
static GtkWidget *native_attach_widget(NativeDebug *session)
{
    NativeRemote *remote = session->remote;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *caption = gtk_label_new(insight_tr("Remote process PID (--multi)"));
    remote->pid = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(remote->pid), 64);
    gtk_entry_set_input_purpose(GTK_ENTRY(remote->pid), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_width_chars(GTK_ENTRY(remote->pid), 12);
    gtk_widget_set_name(remote->pid, "insight-gdb-attach-pid");
    remote->attach = gtk_button_new_with_label(insight_tr("Attach remote process"));
    gtk_widget_set_name(remote->attach, "insight-gdb-attach");
    gtk_widget_set_tooltip_text(remote->pid, insight_tr(
        "PID on the gdbserver machine. Connect using extended-remote first; the remote account must have tracing permission."));
    g_signal_connect(remote->pid, "changed", G_CALLBACK(native_attach_changed), session);
    g_signal_connect(remote->pid, "activate", G_CALLBACK(native_attach_clicked), session);
    g_signal_connect(remote->attach, "clicked", G_CALLBACK(native_attach_clicked), session);
    gtk_box_pack_start(GTK_BOX(box), caption, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), remote->pid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), remote->attach, FALSE, FALSE, 0);
    return box;
}
