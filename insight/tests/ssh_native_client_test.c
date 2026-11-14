/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise production debugger tunnel arguments with a native OpenSSH client and loopback SSH fixture.
 * RU: Проверяет аргументы туннеля отладчика с нативным OpenSSH и локальной SSH-фикстурой. */
#include "ssh_auth.h"
#include "glib_compat.h"
#include "remote_io.h"
#include <string.h>
#include <glib/gstdio.h>
#include "../../src/insight-debuggers/gdb-remote-model.inc"

/* EN: Open a real forwarded connection using production arguments; use only isolated fixture credentials.
 * RU: Открывает реальное перенаправленное соединение производственными аргументами с паролем тестовой
 * фикстуры. */
static void tunnel(void)
{
    const gchar *bin = g_getenv("INSIGHT_SSH_TEST_CLIENT_DIR");
    const gchar *config = g_getenv("INSIGHT_SSH_TEST_CONFIG");
    g_assert_nonnull(config);
    gchar *ssh = insight_remote_program("ssh", bin);
    gchar *helper = insight_remote_program("geany-insight-askpass", bin);
    g_assert_nonnull(ssh);
    g_assert_nonnull(helper);
    gchar *directory = g_dir_make_tmp("insight tunnel тест-XXXXXX", NULL);
    GSocketListener *listener = g_socket_listener_new();
    guint port = g_socket_listener_add_any_inet_port(listener, NULL, NULL);
    g_assert_cmpuint(port, >, 0);
    g_socket_listener_close(listener);
    g_object_unref(listener);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    gchar *auth =
        insight_ssh_auth_prepare(launcher, directory, "test-server", "developer", 0, "", helper, TRUE, NULL);
    g_assert_nonnull(auth);
    g_assert_true(insight_ssh_auth_seed(launcher, "isolated-тест-password", NULL));
    gchar **argv =
        native_remote_argv(ssh, "test-server", "developer", 0, "", config, "127.0.0.1", 2345, port, TRUE);
    GError *error = NULL;
    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv, &error);
    g_assert_no_error(error);
    GDataInputStream *output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(process));
    gchar *line = g_data_input_stream_read_line(output, NULL, NULL, &error);
    if (!line)
    {
        gchar diagnostic[4096] = {0};
        g_input_stream_read(g_subprocess_get_stderr_pipe(process), diagnostic, sizeof diagnostic - 1, NULL,
                            NULL);
        g_test_message("SSH tunnel diagnostics: %s", diagnostic);
    }
    g_assert_no_error(error);
    g_assert_cmpstr(line, ==, "GEANY-GDB-SSH-READY");
    GSocketClient *client = g_socket_client_new();
    g_socket_client_set_timeout(client, 10);
    GSocketConnection *connection = g_socket_client_connect_to_host(client, "127.0.0.1", port, NULL, &error);
    g_assert_no_error(error);
    const gchar message[] = "debugger-forwarding";
    g_assert_true(g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(connection)), message,
                                            sizeof message, NULL, NULL, &error));
    g_assert_no_error(error);
    gchar received[sizeof message] = {0};
    gsize count = 0;
    g_assert_true(g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(connection)), received,
                                          sizeof received, &count, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(count, ==, sizeof message);
    g_assert_cmpmem(received, count, message, sizeof message);
    g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
    g_subprocess_force_exit(process);
    g_subprocess_wait(process, NULL, NULL);
    g_object_unref(connection);
    g_object_unref(client);
    g_object_unref(output);
    g_object_unref(process);
    g_object_unref(launcher);
    insight_ssh_auth_cleanup(auth);
    gchar *vault = insight_ssh_auth_path(directory);
    g_remove(vault);
    gchar *folder = g_path_get_dirname(vault);
    g_rmdir(folder);
    g_rmdir(directory);
    g_free(folder);
    g_free(vault);
    g_free(auth);
    g_free(directory);
    g_free(line);
    g_free(ssh);
    g_free(helper);
    g_strfreev(argv);
}

/* EN: Register native-client integration checks; argc/argv select GLib tests.
 * RU: Регистрирует интеграционные проверки нативного клиента; argc/argv выбирают тесты GLib. */
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ssh/native-debugger-tunnel", tunnel);
    return g_test_run();
}
