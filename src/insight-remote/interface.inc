/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Build SSH connection/transfer pages and terminal tabs.
 * RU: Создаёт страницы подключения/передачи SSH и вкладки терминалов. */
/* EN: Initialize SSH UI and its workspace callback, restoring nonsecret
 * preferences. RU: Создаёт интерфейс SSH и обработчик workspace, восстанавливая
 * несекретные параметры. */
void
insight_remote_ui_init (gboolean (*workspace) (const char *))
{
    gchar *terminal_settings = g_build_filename (app->configdir, "insight", "terminal.conf", NULL);
    insight_terminal_settings_path (terminal_settings);
    g_free (terminal_settings);
    remote_epoch++;
    remote_ui.upload_versions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    remote_ui.snapshot_cancel = g_cancellable_new ();
    remote_ui.workspace = workspace;
    remote_ui.roots = g_ptr_array_new_with_free_func (root_free);
    remote_ui.directory = g_build_filename (app->configdir, "insight", "remote", NULL);
    g_mkdir_with_parents (remote_ui.directory, 0700);
    remote_ui.config = g_build_filename (app->configdir, "insight", "ssh.conf", NULL);
    restore_roots ();
    remote_ui.sessions = g_ptr_array_new_with_free_func (session_unref);
    remote_ui.page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name (remote_ui.page, "insight-ssh-page");
    remote_ui.tabs = gtk_notebook_new ();
    gtk_widget_set_name (remote_ui.tabs, "insight-ssh-tabs");
    gtk_notebook_set_tab_pos (GTK_NOTEBOOK (remote_ui.tabs), GTK_POS_RIGHT);
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (remote_ui.tabs), TRUE);
    gtk_notebook_set_show_border (GTK_NOTEBOOK (remote_ui.tabs), FALSE);
    gtk_box_pack_start (GTK_BOX (remote_ui.page), remote_ui.tabs, TRUE, TRUE, 0);
    g_signal_connect (remote_ui.tabs, "switch-page", G_CALLBACK (session_selected), NULL);
    remote_ui.connection = form_page ("Connection");
    form_button (remote_ui.connection, "Set up SSH step by step…", "insight-ssh-setup-open",
                 G_CALLBACK (ssh_setup_open), NULL);
    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 4);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 5);
    gtk_box_pack_start (GTK_BOX (remote_ui.connection), grid, FALSE, FALSE, 0);
    remote_ui.host = gtk_entry_new ();
    remote_ui.user = gtk_entry_new ();
    remote_ui.identity = gtk_entry_new ();
    remote_ui.ssh_config = gtk_entry_new ();
    remote_ui.port = gtk_spin_button_new_with_range (1, 65535, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (remote_ui.port), 22);
    GtkWidget *fields[]
        = { remote_ui.host, remote_ui.user, remote_ui.port, remote_ui.identity, remote_ui.ssh_config };
    const gchar *labels[]
        = { "Host / alias", "Login", "Port", "Identity file", "SSH config file (optional)" },
        *names[] = { "insight-ssh-host", "insight-ssh-user", "insight-ssh-port", "insight-ssh-identity",
                     "insight-ssh-config" };
    for (guint i = 0; i < G_N_ELEMENTS (fields); i++)
    {
        gtk_widget_set_name (fields[i], names[i]);
        form_row (grid, i, labels[i], fields[i]);
    }
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_box_pack_start (GTK_BOX (remote_ui.connection), row, FALSE, FALSE, 0);
    form_button (row, "Key…", NULL, G_CALLBACK (identity_choose), NULL);
    form_button (row, "Connect shell", "insight-ssh-connect", G_CALLBACK (shell_connect), NULL);
    form_button (row, "Disconnect", "insight-ssh-disconnect", G_CALLBACK (shell_disconnect), NULL);
    form_button (row, "AI help", NULL, G_CALLBACK (ai_console), NULL);
    remote_ui.save_password = gtk_check_button_new_with_label ("Remember passwords on this computer");
    gtk_widget_set_tooltip_text (remote_ui.save_password,
                                 "Save passwords and key passphrases in private settings on this "
                                 "computer, outside project files");
    gtk_box_pack_start (GTK_BOX (remote_ui.connection), remote_ui.save_password, FALSE, FALSE, 0);
    form_button (remote_ui.connection, "Forget saved password", NULL, G_CALLBACK (forget_password), NULL);
    remote_ui.files = form_page ("Transfer");
    remote_ui.path = gtk_entry_new ();
    gtk_entry_set_width_chars (GTK_ENTRY (remote_ui.path), 1);
    gtk_widget_set_name (remote_ui.path, "insight-ssh-path");
    gtk_entry_set_placeholder_text (GTK_ENTRY (remote_ui.path),
                                    "Absolute remote file, folder or project path");
    gtk_box_pack_start (GTK_BOX (remote_ui.files), remote_ui.path, FALSE, FALSE, 0);
    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_box_pack_start (GTK_BOX (remote_ui.files), row, FALSE, FALSE, 0);
    const gchar *open_labels[] = { "Open file", "Open folder", "Open project" };
    for (guint i = 0; i < 3; i++)
        form_button (row, open_labels[i], NULL, G_CALLBACK (open_remote),
                     GINT_TO_POINTER (i == 0   ? OPEN_FILE
                                      : i == 1 ? OPEN_FOLDER
                                               : PROJECT_READ));
    remote_ui.automatic = gtk_check_button_new_with_label ("Auto upload on save");
    gtk_box_pack_start (GTK_BOX (remote_ui.files), remote_ui.automatic, FALSE, FALSE, 0);
    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_box_pack_start (GTK_BOX (remote_ui.files), row, FALSE, FALSE, 0);
    form_button (row, "Save remote", NULL, G_CALLBACK (save_remote), NULL);
    form_button (row, "Stop transfer", NULL, G_CALLBACK (stop_transfer), NULL);
    GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start (GTK_BOX (remote_ui.files), separator, FALSE, FALSE, 4);
    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 4);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 5);
    gtk_box_pack_start (GTK_BOX (remote_ui.files), grid, FALSE, FALSE, 0);
    remote_ui.transfer_local = gtk_entry_new ();
    form_row (grid, 0, "Local source / new destination", remote_ui.transfer_local);
    gtk_widget_set_name (remote_ui.transfer_local, "insight-scp-local");
    remote_ui.transfer_remote = gtk_entry_new ();
    form_row (grid, 1, "Remote source / destination", remote_ui.transfer_remote);
    gtk_widget_set_name (remote_ui.transfer_remote, "insight-scp-remote");
    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_box_pack_start (GTK_BOX (remote_ui.files), row, FALSE, FALSE, 0);
    remote_ui.transfer_folder = gtk_check_button_new_with_label ("Folder");
    gtk_widget_set_name (remote_ui.transfer_folder, "insight-scp-folder");
    gtk_box_pack_start (GTK_BOX (row), remote_ui.transfer_folder, FALSE, FALSE, 0);
    remote_ui.transfer_mode = gtk_combo_box_text_new ();
    gtk_widget_set_name (remote_ui.transfer_mode, "insight-scp-packing");
    const gchar *packing[] = { "No packing", "gz (file)", "tar.gz (file/folder)" };
    for (guint i = 0; i < 3; i++)
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (remote_ui.transfer_mode), packing[i]);
    gtk_combo_box_set_active (GTK_COMBO_BOX (remote_ui.transfer_mode), 0);
    gtk_box_pack_start (GTK_BOX (row), remote_ui.transfer_mode, FALSE, FALSE, 0);
    form_button (row, "Choose…", "insight-scp-choose", G_CALLBACK (scp_choose), NULL);
    row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_box_pack_start (GTK_BOX (remote_ui.files), row, FALSE, FALSE, 0);
    form_button (row, "Send", "insight-scp-send", G_CALLBACK (scp_start), GINT_TO_POINTER (TRUE));
    form_button (row, "Receive", "insight-scp-receive", G_CALLBACK (scp_start), GINT_TO_POINTER (FALSE));
    form_button (row, "Stop", "insight-scp-stop", G_CALLBACK (stop_transfer), NULL);
    remote_ui.transfer_status = gtk_label_new ("Archives are transferred as files. Receive never "
                                               "overwrites an existing local destination.");
    gtk_widget_set_name (remote_ui.transfer_status, "insight-scp-status");
    gtk_label_set_selectable (GTK_LABEL (remote_ui.transfer_status), TRUE);
    gtk_label_set_line_wrap (GTK_LABEL (remote_ui.transfer_status), TRUE);
    gtk_label_set_xalign (GTK_LABEL (remote_ui.transfer_status), 0);
    gtk_box_pack_start (GTK_BOX (remote_ui.files), remote_ui.transfer_status, FALSE, FALSE, 0);
    remote_ui.status = gtk_label_new ("SSH/SFTP — configure a connection, transfer files, or add a terminal");
    gtk_widget_set_name (remote_ui.status, "insight-ssh-status");
    gtk_label_set_xalign (GTK_LABEL (remote_ui.status), 0);
    gtk_label_set_line_wrap (GTK_LABEL (remote_ui.status), TRUE);
    gtk_label_set_line_wrap_mode (GTK_LABEL (remote_ui.status), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_selectable (GTK_LABEL (remote_ui.status), TRUE);
    gtk_label_set_width_chars (GTK_LABEL (remote_ui.status), 1);
    GtkWidget *plus = gtk_button_new_from_icon_name ("list-add-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name (plus, "insight-ssh-new-terminal");
    gtk_widget_set_tooltip_text (plus, "New SSH terminal");
    g_signal_connect (plus, "clicked", G_CALLBACK (session_add), NULL);
    gtk_notebook_set_action_widget (GTK_NOTEBOOK (remote_ui.tabs), plus, GTK_PACK_END);
    session_add (NULL, NULL);
    gtk_notebook_set_current_page (GTK_NOTEBOOK (remote_ui.tabs), 0);
    GtkWidget *actions = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0),
              *diagnostic = gtk_button_new_from_icon_name ("go-jump-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name (diagnostic, "insight-ssh-open-diagnostic");
    gtk_widget_set_tooltip_text (diagnostic, "Open selected file:line diagnostic in local editor");
    g_signal_connect (diagnostic, "clicked", G_CALLBACK (open_diagnostic), NULL);
    gtk_box_pack_start (GTK_BOX (actions), insight_panel_help_button ("panel-ssh.md"), FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (actions), diagnostic, FALSE, FALSE, 0);
    gtk_notebook_set_action_widget (GTK_NOTEBOOK (remote_ui.tabs), actions, GTK_PACK_START);
    insight_terminal_ui_add (remote_ui.page, "SSH");
    insight_terminal_ui_status (remote_ui.page, remote_ui.status);
    insight_serial_init ();
    GKeyFile *profile = g_key_file_new ();
    g_key_file_load_from_file (profile, remote_ui.config, G_KEY_FILE_NONE, NULL);
    profile_load (profile);
    g_key_file_unref (profile);
    g_signal_connect (remote_ui.automatic, "toggled", G_CALLBACK (profile_toggle), NULL);
    g_signal_connect (remote_ui.save_password, "toggled", G_CALLBACK (profile_toggle), NULL);
    remote_ui.signals[0]
        = g_signal_connect_after (geany_object, "document-save", G_CALLBACK (saved_document), NULL);
    remote_ui.signals[1] = g_signal_connect_after (geany_object, "project-save", G_CALLBACK (project_changed),
                                                   GINT_TO_POINTER (1));
    remote_ui.signals[2]
        = g_signal_connect_after (geany_object, "project-open", G_CALLBACK (project_changed), NULL);
    remote_ui.signals[3]
        = g_signal_connect_after (geany_object, "project-close", G_CALLBACK (project_closed), NULL);
    if (app->project && app->project->file_name)
    {
        gchar *native = utils_get_locale_from_utf8 (app->project->file_name);
        GKeyFile *keyfile = g_key_file_new ();
        if (g_key_file_load_from_file (keyfile, native, G_KEY_FILE_NONE, NULL))
            project_changed (NULL, keyfile, NULL);
        g_key_file_unref (keyfile);
        g_free (native);
    }
    GtkWidget *menus[] = {
        gtk_menu_item_get_submenu (GTK_MENU_ITEM (ui_lookup_widget (main_widgets.window, "file1"))),
        gtk_menu_item_get_submenu (GTK_MENU_ITEM (ui_lookup_widget (main_widgets.window, "menu_project1")))
    };
    for (guint i = 0; i < 2; i++)
    {
        GtkWidget *item = gtk_menu_item_new_with_label ("SSH files / folders / projects…");
        gtk_container_add (GTK_CONTAINER (menus[i]), item);
        g_signal_connect (item, "activate", G_CALLBACK (show_remote), NULL);
        gtk_widget_show (item);
    }
}
