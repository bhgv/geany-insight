/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify private credential lifetimes and real OpenSSH public-key enrollment in isolated directories.
 * RU: Проверяет сроки хранения паролей и установку ключей OpenSSH в изолированных каталогах. */
#include "remote_credentials.h"
#include "ssh_auth.h"
#include "ssh_setup.h"
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

/* EN: Assert private POSIX mode for path; Windows checks file existence instead.
 * RU: Проверяет закрытые права path в POSIX; в Windows проверяет наличие файла. */
static void
private_mode (const gchar *path)
{
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
#ifndef G_OS_WIN32
    GStatBuf info;
    g_assert_cmpint (g_stat (path, &info), ==, 0);
    g_assert_cmpuint (info.st_mode & 077, ==, 0);
#endif
}

/* EN: Exercise remembered/transient secrets, migration, forgetting and cross-client account identity.
 * RU: Проверяет постоянные/временные пароли, перенос, удаление и общие идентификаторы серверов. */
static void
credentials (void)
{
    gchar *directory = g_dir_make_tmp ("insight-ssh-auth-test-XXXXXX", NULL);
    gchar *target = insight_ssh_auth_target (" host ", " user ", 0);
    gchar *same = insight_ssh_auth_target ("host", "user", 22);
    g_assert_cmpstr (target, ==, same);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);
    gchar *session
        = insight_ssh_auth_prepare (launcher, directory, "host", "user", 22, "", NULL, FALSE, NULL);
    g_assert_nonnull (session);
    g_assert_true (insight_ssh_auth_seed (launcher, "test-only password", NULL));
    const gchar *transient = g_subprocess_launcher_getenv (launcher, "GEANY_INSIGHT_SSH_CONFIG");
    gchar *path = insight_ssh_auth_path (directory);
    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
    private_mode (transient);
    private_mode (session);
    insight_ssh_auth_cleanup (session);
    g_assert_false (g_file_test (transient, G_FILE_TEST_EXISTS));
    g_free (session);
    session = insight_ssh_auth_prepare (launcher, directory, "host", "user", 22, "", NULL, TRUE, NULL);
    g_assert_true (insight_ssh_auth_seed (launcher, "remembered test", NULL));
    private_mode (path);
    insight_ssh_auth_cleanup (session);
    gchar *password = insight_remote_password_load (path, target, NULL);
    g_assert_cmpstr (password, ==, "remembered test");
    g_free (password);
    GKeyFile *project = g_key_file_new ();
    gchar *other = insight_ssh_auth_target ("other", "user", 22);
    gchar *group = g_strconcat ("insight-ssh-password-", other, NULL);
    g_key_file_set_string (project, group, "password", "legacy test");
    g_key_file_set_string (project, "project", "name", "retained");
    g_assert_true (insight_ssh_auth_migrate_key (project, directory, NULL));
    g_assert_false (g_key_file_has_group (project, group));
    g_assert_true (g_key_file_has_group (project, "project"));
    password = insight_remote_password_load (path, other, NULL);
    g_assert_cmpstr (password, ==, "legacy test");
    g_free (password);
    g_assert_true (insight_ssh_auth_forget (directory, "host", "user", 22, "", NULL));
    g_assert_null (insight_remote_password_load (path, target, NULL));
    g_key_file_unref (project);
    g_object_unref (launcher);
    g_remove (path);
    gchar *folder = g_path_get_dirname (path);
    g_rmdir (folder);
    g_rmdir (directory);
    g_free (folder);
    g_free (directory);
    g_free (path);
    g_free (target);
    g_free (same);
    g_free (session);
    g_free (other);
    g_free (group);
}

/* EN: Run argv with optional public input; assert successful bounded test output and return it.
 * RU: Выполняет argv с открытым input; проверяет успешный вывод теста и возвращает его. */
static gchar *
run (const gchar *const *argv, const gchar *input)
{
    GError *error = NULL;
    GSubprocess *process = g_subprocess_newv (
        argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
        &error);
    g_assert_no_error (error);
    gchar *output = NULL, *diagnostic = NULL;
    g_assert_true (g_subprocess_communicate_utf8 (process, input, NULL, &output, &diagnostic, &error));
    g_assert_no_error (error);
    if (!g_subprocess_get_successful (process))
        g_test_message ("%s", diagnostic);
    g_assert_true (g_subprocess_get_successful (process));
    g_free (diagnostic);
    g_object_unref (process);
    return output;
}

/* EN: Generate a real key, validate its public half and enroll without duplicate/restricted-key replacement.
 * RU: Создаёт настоящий ключ, проверяет открытую часть и установку без дубликатов/замены ограниченных ключей.
 */
static void
keys (void)
{
    gchar *keygen = g_find_program_in_path ("ssh-keygen");
    if (!keygen)
    {
        g_test_skip ("OpenSSH ssh-keygen is required");
        return;
    }
    gchar *directory = g_dir_make_tmp ("insight-ssh-key-test-XXXXXX", NULL);
    gchar *key = g_build_filename (directory, "private key", NULL);
    const gchar *generate[] = { keygen, "-q", "-t", "ed25519", "-N", "", "-f", key, NULL };
    g_free (run (generate, NULL));
    const gchar *derive[] = { keygen, "-y", "-f", key, NULL };
    gchar *public = run (derive, NULL);
    gchar *canonical = insight_ssh_public_key (public, NULL);
    g_assert_nonnull (canonical);
    gchar *commented = g_strconcat (canonical, " different comment", NULL);
    gchar *again = insight_ssh_public_key (commented, NULL);
    g_assert_cmpstr (canonical, ==, again);
    g_assert_null (insight_ssh_public_key ("ssh-ed25519 AAAA", NULL));
    g_assert_null (insight_ssh_public_key ("ssh-ed25519 AAAA\ninjected", NULL));
    gchar *folder = g_build_filename (directory, "keys with ' quote", NULL);
    g_mkdir (folder, 0700);
    gchar *authorized = g_build_filename (folder, "authorized_keys", NULL);
    gchar *restricted = g_strdup_printf ("restrict %s previous-comment\n", canonical);
    g_file_set_contents (authorized, restricted, -1, NULL);
    gchar *command = insight_ssh_install_key_command (folder, NULL);
    const gchar *install[] = { "sh", "-c", command, NULL };
    gchar *input = g_strconcat (canonical, "\n", NULL);
    g_free (run (install, input));
    g_free (run (install, input));
    gchar *contents = NULL;
    g_file_get_contents (authorized, &contents, NULL, NULL);
    g_assert_cmpstr (contents, ==, restricted);
    private_mode (authorized);
    private_mode (key);
    g_remove (authorized);
    g_free (run (install, input));
    g_free (contents);
    g_file_get_contents (authorized, &contents, NULL, NULL);
    g_assert_cmpstr (contents, ==, input);
    g_assert_null (insight_ssh_server_command ("relative", 2345, NULL));
    gchar *server = insight_ssh_server_command ("/tmp/a'b", 2345, NULL);
    g_assert_nonnull (strstr (server, "env LC_ALL=C gdbserver --multi 127.0.0.1:2345"));
    g_assert_null (insight_ssh_check_command ("/tmp/file\ncommand", NULL));
    InsightRemote *endpoint = insight_remote_new ("host", "user", 2222, key, "/", directory, NULL);
    gchar **argv = insight_ssh_setup_argv (endpoint, keygen, "/tmp/custom config", "printf test", FALSE);
    g_assert_true (g_strv_contains ((const gchar *const *)argv, "-T"));
    g_assert_true (g_strv_contains ((const gchar *const *)argv, "-F"));
    g_assert_true (g_strv_contains ((const gchar *const *)argv, "-oBatchMode=yes"));
    g_strfreev (argv);
    insight_remote_free (endpoint);
    g_remove (authorized);
    g_rmdir (folder);
    g_remove (key);
    gchar *pubfile = g_strconcat (key, ".pub", NULL);
    g_remove (pubfile);
    g_rmdir (directory);
    g_free (pubfile);
    g_free (server);
    g_free (contents);
    g_free (input);
    g_free (command);
    g_free (restricted);
    g_free (authorized);
    g_free (folder);
    g_free (again);
    g_free (commented);
    g_free (canonical);
    g_free (public);
    g_free (key);
    g_free (directory);
    g_free (keygen);
}

/* EN: Register isolated SSH model checks using argc/argv for GLib test selection.
 * RU: Регистрирует изолированные проверки SSH; argc/argv выбирают тесты GLib. */
int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/ssh/private-credentials", credentials);
    g_test_add_func ("/ssh/public-key-enrollment", keys);
    return g_test_run ();
}
