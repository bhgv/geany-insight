/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Persist nonsecret SSH preferences and bridge the guided connection flow.
 * RU: Сохраняет несекретные параметры SSH и подключает мастер соединения. */
/* EN: Store nonsecret connection fields in key, migrating legacy passwords into
 * private per-computer storage. RU: Сохраняет несекретные поля в key, перенося
 * прежние пароли в закрытое хранилище компьютера. */
static void profile_store_key(GKeyFile *key)
{
    GError *error = NULL;
    if (!insight_ssh_auth_migrate_key(key, app->configdir, &error))
    {
        status(error->message);
        g_clear_error(&error);
    }
    g_key_file_set_string(key, "insight-ssh", "host", gtk_entry_get_text(GTK_ENTRY(remote_ui.host)));
    g_key_file_set_string(key, "insight-ssh", "user", gtk_entry_get_text(GTK_ENTRY(remote_ui.user)));
    g_key_file_set_string(key, "insight-ssh", "ssh-config",
                          gtk_entry_get_text(GTK_ENTRY(remote_ui.ssh_config)));
    g_key_file_set_integer(key, "insight-ssh", "port",
                           gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote_ui.port)));
    g_key_file_set_string(key, "insight-ssh", "identity", gtk_entry_get_text(GTK_ENTRY(remote_ui.identity)));
    g_key_file_set_string(key, "insight-ssh", "remote-path", gtk_entry_get_text(GTK_ENTRY(remote_ui.path)));
    g_key_file_set_boolean(key, "insight-ssh", "save-password",
                           gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.save_password)));
    g_key_file_set_boolean(key, "insight-ssh", "auto-upload",
                           gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.automatic)));
}
/* EN: Write nonsecret form preferences to the current profile; retain an invalid existing file unchanged.
 * RU: Записывает несекретные параметры формы в профиль; сохраняет повреждённый существующий файл без
 * изменений. */
static void profile_save(void)
{
    if (remote_ui.loading || remote_ui.closing)
        return;
    GKeyFile *key = g_key_file_new();
    if (g_file_test(remote_ui.config, G_FILE_TEST_EXISTS) &&
        !g_key_file_load_from_file(key, remote_ui.config, G_KEY_FILE_KEEP_COMMENTS, NULL))
    {
        g_key_file_unref(key);
        status("SSH profile not saved: invalid project/config file");
        return;
    }
    profile_store_key(key);
    gsize length;
    gchar *data = g_key_file_to_data(key, &length, NULL);
    GFile *file = g_file_new_for_path(remote_ui.config);
    GError *error = NULL;
    if (!g_file_replace_contents(file, data, length, NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL, &error))
        status(error ? error->message : "SSH profile write failed");
    g_clear_error(&error);
    g_object_unref(file);
    g_free(data);
    g_key_file_unref(key);
}
/* EN: Apply auto-upload/save preference changes; widget/data are unused.
 * RU: Применяет изменения автоматической передачи/сохранения; widget/data не используются. */
static void profile_toggle(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    if (remote_ui.loading)
        return;
    gboolean enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.automatic));
    for (guint i = 0; i < remote_ui.roots->len; i++)
    {
        RemoteRoot *root = g_ptr_array_index(remote_ui.roots, i);
        if (endpoint_matches(root->remote))
        {
            root->auto_upload = enabled;
            root_metadata(root);
        }
    }
    if (remote_ui.active && endpoint_matches(remote_ui.active->root->remote))
        remote_ui.active->root->auto_upload = enabled;
    profile_save();
}
/* EN: Restore nonsecret form fields from key, defaulting new connections to remembered passwords.
 * RU: Восстанавливает несекретные поля из key, включая сохранение паролей для новых соединений. */
static void profile_load(GKeyFile *key)
{
    remote_ui.loading = TRUE;
    const char *names[] = {"host", "user", "identity", "remote-path", "ssh-config"};
    GtkWidget *fields[] = {remote_ui.host, remote_ui.user, remote_ui.identity, remote_ui.path,
                           remote_ui.ssh_config};
    for (guint i = 0; i < G_N_ELEMENTS(fields); i++)
    {
        gchar *value = g_key_file_get_string(key, "insight-ssh", names[i], NULL);
        gtk_entry_set_text(GTK_ENTRY(fields[i]), value ? value : "");
        g_free(value);
    }
    gint port = g_key_file_get_integer(key, "insight-ssh", "port", NULL);
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(remote_ui.port),
        g_key_file_has_key(key, "insight-ssh", "port", NULL) && port >= 0 && port <= 65535 ? port : 22);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(remote_ui.save_password),
                                 (!g_key_file_has_key(key, "insight-ssh", "save-password", NULL) ||
                                  g_key_file_get_boolean(key, "insight-ssh", "save-password", NULL)));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(remote_ui.automatic),
                                 g_key_file_get_boolean(key, "insight-ssh", "auto-upload", NULL));
    remote_ui.loading = FALSE;
}
/* EN: Handle project key load/save selected by data; object is unused; update matching local roots.
 * RU: Обрабатывает загрузку/сохранение key проекта по data; object не используется; обновляет локальные
 * корни. */
static void project_changed(GObject *object, GKeyFile *key, gpointer data)
{
    (void)object;
    if (!app->project || !app->project->file_name)
        return;
    gchar *previous = g_strdup(remote_ui.config);
    g_free(remote_ui.config);
    remote_ui.config = utils_get_locale_from_utf8(app->project->file_name);
    if (GPOINTER_TO_INT(data))
    {
        profile_store_key(key);
        gchar *base_path = project_get_base_path();
        if (base_path)
            for (guint i = 0; i < remote_ui.roots->len; i++)
            {
                RemoteRoot *root = g_ptr_array_index(remote_ui.roots, i);
                if (!g_strcmp0(root->remote->local, base_path))
                {
                    g_key_file_set_string(key, "insight-ssh", "remote-base-path", root->remote->remote);
                    break;
                }
            }
        g_free(base_path);
    }
    else if (g_key_file_has_group(key, "insight-ssh"))
        profile_load(key);
    gchar *remote = g_key_file_get_string(key, "insight-ssh", "remote-base-path", NULL),
          *base = project_get_base_path();
    if (remote && *remote && base)
    {
        RemoteRoot *old = NULL;
        for (guint i = 0; i < remote_ui.roots->len; i++)
        {
            RemoteRoot *candidate = g_ptr_array_index(remote_ui.roots, i);
            if (!g_strcmp0(candidate->remote->local, base))
            {
                old = candidate;
                break;
            }
        }
        if (old)
        {
            g_free(old->config);
            old->config = g_strdup(remote_ui.config);
            old->auto_upload = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.automatic));
            root_metadata(old);
        }
        else
        {
            RemoteRoot *root = form_root(remote, base, remote_ui.directory, NULL);
            if (root)
                g_ptr_array_add(remote_ui.roots, root);
        }
    }
    g_free(remote);
    g_free(base);
    g_free(previous);
}
/* EN: Restore global SSH preferences after project closure; object/data are unused.
 * RU: Восстанавливает общие параметры SSH после закрытия проекта; object/data не используются. */
static void project_closed(GObject *object, gpointer data)
{
    (void)object;
    (void)data;
    g_free(remote_ui.config);
    remote_ui.config = g_build_filename(app->configdir, "insight", "ssh.conf", NULL);
    GKeyFile *key = g_key_file_new();
    g_key_file_load_from_file(key, remote_ui.config, G_KEY_FILE_NONE, NULL);
    profile_load(key);
    g_key_file_unref(key);
}
/* EN: Forget this endpoint's password and selected-key passphrase; widget/data
 * are unused. RU: Удаляет пароль сервера и выбранного ключа; widget/data не
 * используются. */
static void forget_password(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    GError *error = NULL;
    gboolean ok = insight_ssh_auth_forget(app->configdir, gtk_entry_get_text(GTK_ENTRY(remote_ui.host)),
                                          gtk_entry_get_text(GTK_ENTRY(remote_ui.user)),
                                          gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote_ui.port)),
                                          gtk_entry_get_text(GTK_ENTRY(remote_ui.identity)), &error);
    status(ok ? "Saved SSH passwords removed from this computer" : error->message);
    g_clear_error(&error);
}

/* EN: Apply verified settings and open an SSH shell; data is unused and secrets
 * stay outside the form/profile. RU: Применяет проверенные settings и открывает
 * SSH; data не используется, секреты не попадают в форму/профиль. */
static void ssh_setup_applied(const InsightSshSetup *settings, gpointer data)
{
    (void)data;
    if (remote_ui.closing)
        return;
    gtk_entry_set_text(GTK_ENTRY(remote_ui.host), settings->host);
    gtk_entry_set_text(GTK_ENTRY(remote_ui.user), settings->user);
    gtk_entry_set_text(GTK_ENTRY(remote_ui.identity), settings->identity);
    gtk_entry_set_text(GTK_ENTRY(remote_ui.ssh_config), settings->config);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(remote_ui.port), settings->port);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(remote_ui.save_password), settings->remember);
    profile_save();
    shell_connect(NULL, NULL);
}

/* EN: Open the shared connection wizard from widget; data is unused.
 * RU: Открывает общий мастер подключения из widget; data не используется. */
static void ssh_setup_open(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    InsightSshSetup settings = {.host = gtk_entry_get_text(GTK_ENTRY(remote_ui.host)),
                                .user = gtk_entry_get_text(GTK_ENTRY(remote_ui.user)),
                                .port = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote_ui.port)),
                                .identity = gtk_entry_get_text(GTK_ENTRY(remote_ui.identity)),
                                .config = gtk_entry_get_text(GTK_ENTRY(remote_ui.ssh_config)),
                                .remember =
                                    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.save_password))};
    gchar *ssh = client_program("geany-insight-askpass");
    if (!ssh)
        ssh = client_program("ssh");
    gchar *bin = ssh ? g_path_get_dirname(ssh) : NULL;
    insight_ssh_setup_show(GTK_WINDOW(main_widgets.window), app->configdir, bin, &settings, FALSE,
                           ssh_setup_applied, NULL);
    g_free(bin);
    g_free(ssh);
}
