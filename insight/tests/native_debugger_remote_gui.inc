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

/* EN: Select window's C++ sub-location matching function text and enabled state; return FALSE while refreshing.
 * RU: Выбирает подпозицию C++ window по функции function и состоянию enabled; при обновлении возвращает FALSE. */
static gboolean profile_cpp_location(GtkWidget *window, const gchar *function, const gchar *enabled)
{
    GtkWidget *view = find(window, "insight-gdb-breakpoints");
    if (!view)
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *name = NULL, *state = NULL;
        gtk_tree_model_get(model, &row, 9, &name, 2, &state, -1);
        gboolean matches = name && strstr(name, function) && !g_strcmp0(state, enabled);
        g_free(state);
        g_free(name);
        if (matches)
        {
            gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), &row);
            return TRUE;
        }
    }
    return FALSE;
}

/* EN: Select and activate window's main stack frame after PID attach; return FALSE until frames are available.
 * RU: Выбирает и открывает кадр main в window после подключения по PID; до загрузки стека возвращает FALSE. */
static gboolean remote_attach_main_frame(GtkWidget *window)
{
    GtkWidget *view = find(window, "insight-gdb-stack");
    if (!view || !gtk_widget_is_sensitive(view))
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *name = NULL;
        gtk_tree_model_get(model, &row, 1, &name, -1);
        gboolean matches = !g_strcmp0(name, "main");
        g_free(name);
        if (matches)
        {
            GtkTreePath *path = gtk_tree_model_get_path(model, &row);
            gtk_tree_view_row_activated(GTK_TREE_VIEW(view), path, gtk_tree_view_get_column(GTK_TREE_VIEW(view), 0));
            gtk_tree_path_free(path);
            return TRUE;
        }
    }
    return FALSE;
}

/* EN: Fill the profile breakpoint editor from title in data, testing a nonzero saved ignore count.
 * RU: Заполняет редактор точки по заголовку data, проверяя сохранение ненулевого числа пропусков. */
static gboolean profile_point_rule(gpointer data)
{
    const gchar *title = data;
    GtkWidget *editor = window_named(title);
    GtkWidget *entry = editor ? find(editor, "insight-gdb-breakpoint-rule") : NULL;
    if (!entry)
        return G_SOURCE_CONTINUE;
    gtk_entry_set_text(GTK_ENTRY(entry), !strcmp(title, "Breakpoint condition") ? "1" : "3");
    gtk_dialog_response(GTK_DIALOG(editor), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* EN: Advance the remote scenario for window/tabs, including reconnect, detach, Stop and refused SSH.
 * RU: Продвигает удалённый сценарий window/tabs с переподключением, detach, Stop и отказом SSH. */
static gboolean native_remote_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    gboolean extended = g_strcmp0(g_getenv("INSIGHT_GDB_REMOTE_MODE"), "1") != 0;
    static guint previous_phase = G_MAXUINT;
    static gboolean profile_reopened, attach_tested;
    if (previous_phase != phase)
    {
        g_print("C/C++ remote GTK: mode=%s phase=%u\n", extended ? "extended" : "remote", phase);
        previous_phase = phase;
    }
    if (phase == 1)
    {
        GtkWidget *mode = find(window, "insight-gdb-connection-mode");
        if (mode && gtk_widget_is_sensitive(mode))
        {
            if (profile_reopened)
            {
                g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(mode)), ==, extended ? 2 : 1);
                g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(find(window, "insight-gdb-ssh-host"))),
                    ==, g_getenv("INSIGHT_GDB_SSH_HOST"));
                g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(find(window, "insight-gdb-remote-executable"))),
                    ==, g_getenv("INSIGHT_GDB_REMOTE_EXECUTABLE"));
                g_assert(profile_cpp_location(window, "profile_twice<int>", "n"));
                g_assert(profile_cpp_location(window, "profile_twice<double>", "y"));
                g_assert(point_column(window, 2, "n"));
                g_assert(point_column(window, 3, "1"));
                g_assert(point_column(window, 5, "3"));
                g_assert(find(window, "marker = Awaiting stop"));
                g_assert(click(window, "Forget saved profile"));
                g_assert(click(window, "Save debugger profile"));
                gtk_entry_set_text(GTK_ENTRY(find(window, "insight-gdb-ssh-host")), "changed-host");
                g_assert(click(window, "Restore debugger profile"));
                g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(find(window, "insight-gdb-ssh-host"))),
                    ==, g_getenv("INSIGHT_GDB_SSH_HOST"));
                phase = 24;
                return G_SOURCE_CONTINUE;
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(mode), extended ? 2 : 1);
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
            gtk_entry_set_text(GTK_ENTRY(find(window, "GDB watch expression")), "marker");
            g_assert(click(window, "Add Watch"));
            gtk_entry_set_text(GTK_ENTRY(find(window, "Function or file:line")), "main");
            if (click(window, "Add Breakpoint"))
                phase = 20;
        }
    }
    else if (phase == 20 && point_column(window, 2, "y") &&
             gtk_widget_is_sensitive(find(window, "Breakpoint condition")))
    {
        phase = 21;
        g_timeout_add(100, profile_point_rule, "Breakpoint condition");
        click(window, "Breakpoint condition");
    }
    else if (phase == 21 && point_column(window, 3, "1") && click(window, "Enable / Disable"))
        phase = 22;
    else if (phase == 22 && point_column(window, 2, "n") &&
             gtk_widget_is_sensitive(find(window, "Ignore first hits")))
    {
        phase = 23;
        g_timeout_add(100, profile_point_rule, "Ignore first hits");
        click(window, "Ignore first hits");
    }
    else if (phase == 23 && point_column(window, 5, "3"))
    {
        gtk_entry_set_text(GTK_ENTRY(find(window, "Function or file:line")), "profile_twice");
        if (click(window, "Add Breakpoint"))
            phase = 30;
    }
    else if (phase == 30 && profile_cpp_location(window, "profile_twice<int>", "y") &&
             click(window, "Enable / Disable"))
        phase = 31;
    else if (phase == 31 && profile_cpp_location(window, "profile_twice<int>", "n") &&
             profile_cpp_location(window, "profile_twice<double>", "y") && click(window, "Save debugger profile"))
    {
        g_assert(remote_test_status(window, "Debugger profile saved"));
        profile_reopened = TRUE;
        gtk_widget_destroy(window);
        phase = 0;
    }
    else if (phase == 24 && profile_cpp_location(window, "profile_twice<int>", "n") &&
             profile_cpp_location(window, "profile_twice<double>", "y") && point_column(window, 2, "n") && point_column(window, 5, "3") &&
             click(window, "Enable / Disable"))
        phase = 25;
    else if (phase == 25 && point_column(window, 2, "y") &&
             gtk_widget_is_sensitive(find(window, "Ignore first hits")))
    {
        phase = 26;
        g_timeout_add(100, point_rule, "Ignore first hits");
        click(window, "Ignore first hits");
    }
    else if (phase == 26 && point_column(window, 5, "0"))
        phase = 2;
    else if (phase == 2 && click(window, "Connect gdbserver via SSH"))
        phase++;
    else if (phase == 3 && remote_test_status(window, "SSH gdbserver connected"))
    {
        if (!extended)
        {
            g_assert(!gtk_widget_is_sensitive(find(window, "Run")));
            g_assert(!gtk_widget_is_sensitive(find(window, "Restart")));
            g_assert(!gtk_widget_is_sensitive(find(window, "Attach remote process")));
        }
        if (click(window, extended ? "Run" : "Continue"))
            phase++;
    }
    else if (phase == 4 && source_markers() && click(window, "Step Over"))
        phase++;
    else if (phase == 5 && has_local(window, "marker", "41"))
    {
        GtkWidget *entry = find(window, "GDB watch expression");
        if (entry && gtk_widget_is_sensitive(entry))
        {
            g_assert(click(window, "Save debugger profile"));
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
    else if (phase == 9 && remote_test_status(window, "Remote disconnected"))
    {
        if (!extended)
            phase = 13;
        else if (click(window, "Connect gdbserver via SSH"))
            phase++;
    }
    else if (phase == 10 && remote_test_status(window, "SSH gdbserver connected") && click(window, "Run"))
        phase++;
    else if (phase == 11 && source_markers() && click(window, "Stop"))
        phase++;
    else if (phase == 12 && click(window, "Detach and close SSH"))
        phase++;
    else if (phase == 13 && extended && !attach_tested && remote_test_status(window, "Remote disconnected"))
    {
        if (click(window, "Connect gdbserver via SSH"))
            phase = 40;
    }
    else if (phase == 40 && remote_test_status(window, "SSH gdbserver connected"))
    {
        GtkWidget *pid = find(window, "insight-gdb-attach-pid");
        g_assert(gtk_widget_is_sensitive(pid));
        gtk_entry_set_text(GTK_ENTRY(pid), "2147483647");
        if (click(window, "Attach remote process"))
            phase = 41;
    }
    else if (phase == 41 && remote_test_status(window, "Attach failed; SSH connection remains open"))
    {
        remote_test_entry(window, "insight-gdb-attach-pid", "INSIGHT_GDB_ATTACH_PID");
        if (click(window, "Attach remote process"))
            phase = 42;
    }
    else if (phase == 42 && remote_test_status(window, "Remote process attached") &&
             gtk_widget_is_sensitive(find(window, "Continue")))
    {
        g_assert(!gtk_widget_is_sensitive(find(window, "Attach remote process")));
        g_assert(!gtk_widget_is_sensitive(find(window, "insight-gdb-attach-pid")));
        if (remote_attach_main_frame(window))
            phase = 46;
    }
    else if (phase == 46 && has_local(window, "marker", "41") && click(window, "Continue"))
        phase = 43;
    else if (phase == 43 && click(window, "Pause"))
        phase = 44;
    else if (phase == 44 && click(window, "Detach and close SSH"))
        phase = 45;
    else if (phase == 45 && remote_test_status(window, "Remote disconnected"))
    {
        attach_tested = TRUE;
        phase = 13;
    }
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
            g_assert(!gtk_widget_is_sensitive(find(window, "Run")));
            g_print("C/C++ remote GTK: profiles, SSH forwarding, source mapping, breakpoints, locals, watches, pause, PID attach/retry, detach, reconnect and Stop passed.\n");
            passed = TRUE;
            gtk_widget_destroy(window);
            gtk_main_quit();
            return G_SOURCE_REMOVE;
        }
    }
    (void)tabs;
    return G_SOURCE_CONTINUE;
}
