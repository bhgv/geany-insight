/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Drive the production SSH wizard with real key generation and a deterministic isolated SSH endpoint.
 * RU: Проверяет настоящий мастер SSH с созданием ключа и изолированным предсказуемым SSH-клиентом. */
#include "../../src/insight-ssh-setup.c"

/* EN: Keep this isolated widget fixture's captions in English for reproducible captures.
 * RU: Оставляет английские подписи изолированного виджета для воспроизводимых снимков. */
const gchar *
insight_tr (const gchar *text)
{
    return text;
}

/* EN: Process GTK events until wizard's owned worker finishes, with a ten-second failure deadline.
 * RU: Обрабатывает события GTK до завершения задания wizard с пределом десять секунд. */
static void
settle (SshSetupWizard *wizard)
{
    gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
    do
    {
        while (g_main_context_iteration (NULL, FALSE))
            ;
        g_usleep (1000);
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
    } while (wizard->process);
}

/* EN: Save an optional JPEG of the real wizard window to path from the named environment variable.
 * RU: Сохраняет JPEG настоящего окна wizard по path из переменной окружения name при её наличии. */
static void
capture (SshSetupWizard *wizard, const gchar *name)
{
    const gchar *path = g_getenv (name);
    if (!path)
        return;
    for (guint i = 0; i < 50; i++)
    {
        while (g_main_context_iteration (NULL, FALSE))
            ;
        g_usleep (2000);
    }
    GdkWindow *window = gtk_widget_get_window (wizard->window);
    GdkPixbuf *image = gdk_pixbuf_get_from_window (window, 0, 0, gdk_window_get_width (window),
                                                   gdk_window_get_height (window));
    g_assert_nonnull (image);
    g_assert_true (gdk_pixbuf_save (image, path, "jpeg", NULL, "quality", "88", NULL));
    g_object_unref (image);
}

/* EN: Record explicit Connect settings into the boolean data without retaining borrowed strings.
 * RU: Фиксирует явное подключение settings в логическом data без сохранения заимствованных строк. */
static void
connected (const InsightSshSetup *settings, gpointer data)
{
    g_assert_cmpstr (settings->host, ==, "test-server");
    g_assert_true (settings->remember);
    *((gboolean *)data) = TRUE;
}

/* EN: Remove only the fixture-owned tree path after all asynchronous processes finish.
 * RU: Удаляет только собственное дерево теста path после завершения асинхронных процессов. */
static void
cleanup_tree(const gchar *path)
{
    if (g_file_test(path, G_FILE_TEST_IS_DIR) && !g_file_test(path, G_FILE_TEST_IS_SYMLINK))
    {
        GDir *directory = g_dir_open(path, 0, NULL);
        const gchar *name;
        while (directory && (name = g_dir_read_name(directory)))
        {
            gchar *child = g_build_filename(path, name, NULL);
            cleanup_tree(child);
            g_free(child);
        }
        if (directory)
            g_dir_close(directory);
        g_rmdir(path);
    }
    else
        g_remove(path);
}

/* EN: Exercise terminal/debugging pages, authentication, key reuse, stale verification and cancellation.
 * RU: Проверяет страницы терминала/отладки, пароли, повторное использование ключа, сброс проверки и отмену.
 */
static void
wizard_flow (void)
{
    gchar *directory = g_dir_make_tmp ("insight-ssh-wizard-test-XXXXXX", NULL);
    gchar *client = g_build_filename (directory, "ssh", NULL);
    g_assert_true (g_file_set_contents (client,
                                        "#!/bin/sh\nfor last do :; done\ncase \"$last\" in\n"
                                        "*authorized_keys*) IFS= read -r public; test -n \"$public\" || exit "
                                        "2; printf 'GEANY-SSH-KEY-INSTALLED\\n';;\n"
                                        "*GEANY-SSH-READY*) printf 'GEANY-SSH-READY\\n';;\n"
                                        "*) exit 4;;\nesac\n",
                                        -1, NULL));
    g_chmod (client, 0700);
    gboolean applied = FALSE;
    InsightSshSetup initial = { .host = "test-server",
                                .user = "developer",
                                .port = 22,
                                .remember = TRUE,
                                .start_server = TRUE,
                                .server_port = 2345 };
    GtkWidget *window
        = insight_ssh_setup_show (NULL, directory, directory, &initial, FALSE, connected, &applied);
    g_object_ref_sink (window);
    SshSetupWizard *wizard = g_object_get_data (G_OBJECT (window), "insight-ssh-setup");
    g_assert_false (gtk_entry_get_visibility (GTK_ENTRY (wizard->password)));
    gtk_entry_set_text (GTK_ENTRY (wizard->password), "isolated-test-password");
    capture (wizard, "INSIGHT_SSH_SERVER_CAPTURE");
    gtk_dialog_response (GTK_DIALOG (window), GTK_RESPONSE_ACCEPT);
    g_assert_cmpuint (wizard->page, ==, 1);
    setup_key_create (NULL, wizard);
    settle (wizard);
    g_assert_nonnull (wizard->public_key);
    gchar *identity = g_strdup (gtk_entry_get_text (GTK_ENTRY (wizard->identity)));
    gchar *original = NULL;
    g_file_get_contents (identity, &original, NULL, NULL);
    setup_key_create (NULL, wizard);
    settle (wizard);
    gchar *retained = NULL;
    g_file_get_contents (identity, &retained, NULL, NULL);
    g_assert_cmpstr (original, ==, retained);
    g_free (original);
    g_free (retained);
    capture (wizard, "INSIGHT_SSH_KEYS_CAPTURE");
    setup_job_start (wizard, SETUP_INSTALL);
    settle (wizard);
    gtk_dialog_response (GTK_DIALOG (window), GTK_RESPONSE_ACCEPT);
    g_assert_false (gtk_widget_get_sensitive (wizard->next));
    setup_job_start (wizard, SETUP_TEST);
    settle (wizard);
    g_assert_true (wizard->verified);
    gtk_entry_set_text (GTK_ENTRY (wizard->user), "changed");
    g_assert_false (wizard->verified);
    gtk_entry_set_text (GTK_ENTRY (wizard->user), "developer");
    setup_job_start (wizard, SETUP_TEST);
    settle (wizard);
    gtk_dialog_response (GTK_DIALOG (window), GTK_RESPONSE_ACCEPT);
    gtk_dialog_response (GTK_DIALOG (window), GTK_RESPONSE_ACCEPT);
    g_assert_true (applied);
    g_object_unref (window);
    initial.identity = identity;
    initial.program = "/opt/example/app";
    window = insight_ssh_setup_show (NULL, directory, directory, &initial, TRUE, connected, &applied);
    g_object_ref_sink (window);
    wizard = g_object_get_data (G_OBJECT (window), "insight-ssh-setup");
    gtk_dialog_response (GTK_DIALOG (window), GTK_RESPONSE_ACCEPT);
    gtk_dialog_response (GTK_DIALOG (window), GTK_RESPONSE_ACCEPT);
    capture (wizard, "INSIGHT_SSH_DEBUG_CAPTURE");
    g_assert_true (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (wizard->start_server)));
    gtk_entry_set_text (GTK_ENTRY (wizard->program), "relative");
    gtk_dialog_response (GTK_DIALOG (window), GTK_RESPONSE_ACCEPT);
    g_assert_cmpuint (wizard->page, ==, 2);
    gtk_entry_set_text (GTK_ENTRY (wizard->program), "/opt/example/app");
    gtk_dialog_response (GTK_DIALOG (window), GTK_RESPONSE_ACCEPT);
    setup_job_start (wizard, SETUP_TEST);
    gtk_widget_destroy (window);
    settle (wizard);
    g_object_unref (window);
    g_free (identity);
    g_free (client);
    /* EN/RU: Delete only this fixture's private tree. / Удаляем только собственное дерево теста. */
    cleanup_tree(directory);
    g_free (directory);
}

/* EN: Run the actual production-widget flow when a GTK display is available; argc/argv configure GLib tests.
 * RU: Выполняет сценарий настоящего виджета при доступном GTK-дисплее; argc/argv настраивают тесты GLib. */
int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    if (!gtk_init_check (&argc, &argv))
        return 77;
    g_test_add_func ("/ssh/wizard-flow", wizard_flow);
    return g_test_run ();
}
