/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Drive the actual native debugger through a real SSH tunnel and gdbserver in the loopback fixture.
 * RU: Управляет настоящим отладчиком через реальный SSH-туннель и gdbserver в loopback-фикстуре. */

/* EN: Set window's named entry from environment variable key; missing fixture configuration is fatal.
 * RU: Заполняет поле window name из переменной окружения key; отсутствие настройки фикстуры фатально. */
static void remote_test_entry(GtkWidget *window, const gchar *name, const gchar *key)
{
    GtkWidget *entry = find(window, name);
    const gchar *value = g_getenv(key);
    g_assert(entry && value);
    gtk_entry_set_text(GTK_ENTRY(entry), value);
}

/* EN: Return whether window's connection status contains text.
 * RU: Возвращает, содержит ли статус подключения window строку text. */
static gboolean remote_test_status(GtkWidget *window, const gchar *text)
{
    GtkWidget *label = find(window, "insight-gdb-remote-status");
    return label && strstr(gtk_label_get_text(GTK_LABEL(label)), text);
}

/* EN: Advance the remote scenario for window/tabs, including reconnect, detach, Stop and refused SSH.
 * RU: Продвигает удалённый сценарий window/tabs с переподключением, detach, Stop и отказом SSH. */
static gboolean native_remote_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    if (phase == 1)
    {
        GtkWidget *mode = find(window, "insight-gdb-connection-mode");
        if (mode && gtk_widget_is_sensitive(mode))
        {
            gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 2);
            remote_test_entry(window, "insight-gdb-ssh-host", "INSIGHT_GDB_SSH_HOST");
            remote_test_entry(window, "insight-gdb-ssh-user", "INSIGHT_GDB_SSH_USER");
            remote_test_entry(window, "insight-gdb-ssh-key", "INSIGHT_GDB_SSH_KEY");
            remote_test_entry(window, "insight-gdb-ssh-config", "INSIGHT_GDB_SSH_CONFIG");
            remote_test_entry(window, "insight-gdb-remote-executable", "INSIGHT_GDB_REMOTE_EXECUTABLE");
            remote_test_entry(window, "insight-gdb-remote-source", "INSIGHT_GDB_REMOTE_SOURCE");
            remote_test_entry(window, "insight-gdb-local-source", "INSIGHT_GDB_LOCAL_SOURCE");
            GtkWidget *port = find(window, "insight-gdb-ssh-port");
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(port), atoi(g_getenv("INSIGHT_GDB_SSH_PORT")));
            port = find(window, "insight-gdb-server-port");
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(port), atoi(g_getenv("INSIGHT_GDB_SERVER_PORT")));
            gtk_entry_set_text(GTK_ENTRY(find(window, "insight-gdb-sysroot")), "/");
            gtk_entry_set_text(GTK_ENTRY(find(window, "Function or file:line")), "main");
            if (click(window, "Add Breakpoint"))
                phase++;
        }
    }
    else if (phase == 2 && click(window, "Connect gdbserver via SSH"))
        phase++;
    else if (phase == 3 && remote_test_status(window, "SSH gdbserver connected") && click(window, "Run"))
        phase++;
    else if (phase == 4 && source_markers() && click(window, "Step Over"))
        phase++;
    else if (phase == 5 && has_local(window, "marker", "41"))
    {
        GtkWidget *entry = find(window, "GDB watch expression");
        if (entry && gtk_widget_is_sensitive(entry))
        {
            gtk_entry_set_text(GTK_ENTRY(entry), "marker");
            if (click(window, "Add Watch"))
                phase++;
        }
    }
    else if (phase == 6 && find(window, "marker = 41"))
    {
        /* EN: A paused target has Pause disabled; Continue followed by Pause is tested on the loop below.
         * RU: На паузе кнопка Pause выключена; Continue и Pause проверяются ниже на цикле программы. */
        if (click(window, "Continue"))
            phase = 7;
    }
    else if (phase == 7 && click(window, "Pause"))
        phase++;
    else if (phase == 8 && click(window, "Detach and close SSH"))
        phase++;
    else if (phase == 9 && remote_test_status(window, "Remote disconnected") &&
             click(window, "Connect gdbserver via SSH"))
        phase++;
    else if (phase == 10 && remote_test_status(window, "SSH gdbserver connected") && click(window, "Run"))
        phase++;
    else if (phase == 11 && source_markers() && click(window, "Stop"))
        phase++;
    else if (phase == 12 && click(window, "Detach and close SSH"))
        phase++;
    else if (phase == 13 && remote_test_status(window, "Remote disconnected"))
    {
        GtkWidget *port = find(window, "insight-gdb-ssh-port");
        if (port && gtk_widget_is_sensitive(port))
        {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(port), atoi(g_getenv("INSIGHT_GDB_REFUSED_PORT")));
            if (click(window, "Connect gdbserver via SSH"))
                phase++;
        }
    }
    else if (phase == 14 && remote_test_status(window, "SSH tunnel ended"))
    {
        GtkWidget *connect = find(window, "Connect gdbserver via SSH");
        if (connect && gtk_widget_is_sensitive(connect))
        {
            g_print("C/C++ remote GTK: SSH forwarding, source mapping, breakpoints, locals, watches, pause, detach, reconnect and Stop passed.\n");
            passed = TRUE;
            gtk_widget_destroy(window);
            gtk_main_quit();
            return G_SOURCE_REMOVE;
        }
    }
    (void)tabs;
    return G_SOURCE_CONTINUE;
}
