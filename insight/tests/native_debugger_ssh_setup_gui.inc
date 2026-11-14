/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise the production SSH debugger wizard with an editor-owned real remote gdbserver.
 * RU: Проверяет настоящий мастер SSH-отладки с реальным gdbserver, принадлежащим редактору. */

/* EN: Advance wizard/debugger scenario for window; tabs is unused; success detaches and closes the editor
 * loop. RU: Продвигает сценарий мастера/отладки window; tabs не используется; успех отключает отладку и
 * завершает цикл. */
static gboolean native_ssh_setup_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    (void)tabs;
    static gboolean reopened;
    if (phase == 1 && reopened)
    {
        GtkWidget *start = find(window, "insight-gdb-ssh-start-server");
        if (start && point_column(window, 2, "y") &&
            gtk_widget_is_sensitive(find(window, "Connect gdbserver via SSH")))
        {
            g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(start)));
            g_assert_true(
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(find(window, "insight-gdb-ssh-remember"))));
            g_assert(click(window, "Connect gdbserver via SSH"));
            phase = 9;
        }
    }
    else if (phase == 1)
    {
        GtkWidget *location = find(window, "Function or file:line");
        if (location && gtk_widget_is_sensitive(location))
        {
            gtk_entry_set_text(GTK_ENTRY(location), "main");
            if (click(window, "Add Breakpoint"))
                phase++;
        }
    }
    else if (phase == 2 && point_column(window, 2, "y") && click(window, "insight-gdb-ssh-setup"))
        phase++;
    else if (phase == 3)
    {
        GtkWidget *wizard = window_named("SSH debugging setup");
        if (!wizard)
            return G_SOURCE_CONTINUE;
        remote_test_entry(wizard, "insight-ssh-setup-host", "INSIGHT_GDB_SSH_HOST");
        remote_test_entry(wizard, "insight-ssh-setup-user", "INSIGHT_GDB_SSH_USER");
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(find(wizard, "insight-ssh-setup-port")),
                                  atoi(g_getenv("INSIGHT_GDB_SSH_PORT")));
        gtk_dialog_response(GTK_DIALOG(wizard), GTK_RESPONSE_ACCEPT);
        remote_test_entry(wizard, "insight-ssh-setup-key", "INSIGHT_GDB_SSH_KEY");
        remote_test_entry(wizard, "insight-ssh-setup-config", "INSIGHT_GDB_SSH_CONFIG");
        gtk_dialog_response(GTK_DIALOG(wizard), GTK_RESPONSE_ACCEPT);
        remote_test_entry(wizard, "insight-ssh-setup-program", "INSIGHT_GDB_REMOTE_EXECUTABLE");
        remote_test_entry(wizard, "insight-ssh-setup-remote-source", "INSIGHT_GDB_REMOTE_SOURCE");
        remote_test_entry(wizard, "insight-ssh-setup-local-source", "INSIGHT_GDB_LOCAL_SOURCE");
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(find(wizard, "insight-ssh-setup-server-port")),
                                  atoi(g_getenv("INSIGHT_GDB_SERVER_PORT")));
        gtk_dialog_response(GTK_DIALOG(wizard), GTK_RESPONSE_ACCEPT);
        g_assert(click(wizard, "insight-ssh-setup-check"));
        phase++;
    }
    else if (phase == 4)
    {
        GtkWidget *wizard = window_named("SSH debugging setup");
        GtkWidget *next = wizard ? find(wizard, "insight-ssh-setup-next") : NULL;
        if (next && gtk_widget_is_sensitive(next))
        {
            gtk_dialog_response(GTK_DIALOG(wizard), GTK_RESPONSE_ACCEPT);
            gtk_dialog_response(GTK_DIALOG(wizard), GTK_RESPONSE_ACCEPT);
            phase++;
        }
    }
    else if (phase == 5 && remote_test_status(window, "SSH gdbserver connected") && click(window, "Run"))
        phase++;
    else if (phase == 6 && source_markers() && click(window, "Step Over"))
        phase++;
    else if (phase == 7 && has_local(window, "marker", "41") && click(window, "Detach and close SSH"))
        phase++;
    else if (phase == 8 && remote_test_status(window, "Remote disconnected"))
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(find(window, "insight-gdb-ssh-start-server")), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(find(window, "insight-gdb-ssh-remember")), FALSE);
        gtk_entry_set_text(GTK_ENTRY(find(window, "insight-gdb-remote-directory")), "/changed");
        g_assert(click(window, "Restore debugger profile"));
        g_assert_true(
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(find(window, "insight-gdb-ssh-start-server"))));
        g_assert_true(
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(find(window, "insight-gdb-ssh-remember"))));
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(find(window, "insight-gdb-remote-directory"))), ==, "");
        reopened = TRUE;
        gtk_widget_destroy(window);
        phase = 0;
    }
    else if (phase == 9 && remote_test_status(window, "SSH gdbserver connected") && click(window, "Run"))
        phase++;
    else if (phase == 10 && source_markers() && click(window, "Step Over"))
        phase++;
    else if (phase == 11 && has_local(window, "marker", "41") && click(window, "Detach and close SSH"))
        phase++;
    else if (phase == 12 && remote_test_status(window, "Remote disconnected"))
    {
        g_print("C/C++ SSH wizard: verification, owned gdbserver, tunnel, Run, breakpoint, locals, detach, "
                "saved options, reopen and reconnect passed.\n");
        passed = TRUE;
        gtk_widget_destroy(window);
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
