/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Create private local keys without overwriting existing keys and enroll
 * only their public halves. RU: Создаёт закрытые локальные ключи без перезаписи
 * существующих и устанавливает только открытые части. */

/* EN: Create a deterministic per-server key for wizard data; button is unused
 * and existing keys are reused. RU: Создаёт ключ сервера для wizard data;
 * button не используется, существующий ключ используется повторно. */
static void
setup_key_create (GtkButton *button, gpointer data)
{
    (void)button;
    SshSetupWizard *wizard = data;
    if (wizard->process)
        return;
    gchar *target = insight_ssh_auth_target (
        gtk_entry_get_text (GTK_ENTRY (wizard->host)), gtk_entry_get_text (GTK_ENTRY (wizard->user)),
        gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (wizard->port)));
    gchar *folder = g_build_filename (wizard->configdir, "insight", "ssh-keys", target, NULL);
    g_free (target);
    g_free (wizard->key_destination);
    wizard->key_destination = g_build_filename (folder, "id_ed25519", NULL);
    if (g_file_test (wizard->key_destination, G_FILE_TEST_EXISTS))
    {
        gtk_entry_set_text (GTK_ENTRY (wizard->identity), wizard->key_destination);
        setup_job_start (wizard, SETUP_PUBLIC);
        g_free (folder);
        return;
    }
    if (g_mkdir_with_parents (folder, 0700))
    {
        gtk_label_set_text (GTK_LABEL (wizard->status),
                            insight_tr ("Cannot create the private key directory."));
        g_free (folder);
        return;
    }
    insight_ssh_auth_cleanup (wizard->key_stage);
    g_free (wizard->key_stage);
    wizard->key_stage = g_build_filename (folder, ".new-key-XXXXXX", NULL);
    g_free (folder);
    if (!g_mkdtemp (wizard->key_stage))
    {
        gtk_label_set_text (GTK_LABEL (wizard->status),
                            insight_tr ("Cannot create the private key staging directory."));
        return;
    }
    setup_job_start (wizard, SETUP_GENERATE);
}

/* EN: Copy wizard data's public key only; button is unused and the private key
 * is never copied to the clipboard. RU: Копирует только открытый ключ wizard
 * data; button не используется, закрытый ключ не попадает в буфер обмена. */
static void
setup_key_copy (GtkButton *button, gpointer data)
{
    (void)button;
    SshSetupWizard *wizard = data;
    if (wizard->public_key)
        gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD), wizard->public_key, -1);
}

/* EN: Dispatch button's setup job for wizard data; keys and connection checks
 * share the cancellable worker. RU: Запускает задание button для wizard data;
 * ключи и проверка соединения используют общий отменяемый обработчик. */
static void
setup_operation (GtkButton *button, gpointer data)
{
    SetupJob kind = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (button), "ssh-setup-job"));
    setup_job_start (data, kind);
}

/* EN: Forget saved account/key credentials selected by wizard data; button is unused and errors stay visible.
 * RU: Удаляет сохранённые пароли сервера/ключа мастера data; button не используется, ошибки видны в окне. */
static void
setup_forget(GtkButton *button, gpointer data)
{
    (void)button;
    SshSetupWizard *wizard = data;
    GError *error = NULL;
    gboolean ok = insight_ssh_auth_forget(wizard->configdir,
        gtk_entry_get_text(GTK_ENTRY(wizard->host)), gtk_entry_get_text(GTK_ENTRY(wizard->user)),
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(wizard->port)),
        gtk_entry_get_text(GTK_ENTRY(wizard->identity)), &error);
    gtk_label_set_text(GTK_LABEL(wizard->status), ok ? insight_tr("Saved SSH passwords removed from this computer") : error->message);
    if (ok)
        gtk_entry_set_text(GTK_ENTRY(wizard->password), "");
    g_clear_error(&error);
}
