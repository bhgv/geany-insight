/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Run SSH in an ordinary console with direct keyboard authentication and optional retention after exit.
 * RU: Запускает SSH в обычной консоли с авторизацией с клавиатуры и возможностью оставить окно после выхода. */
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#ifndef G_OS_WIN32
#include <sys/wait.h>
#endif
#ifdef G_OS_WIN32
#include <windows.h>
#include <glib/gwin32.h>
#endif

#ifdef G_OS_WIN32
/* EN: Keep this waiting console owner alive on Ctrl+C/Break; SSH handles its own control events.
 * RU: Сохраняет ожидающий процесс консоли при Ctrl+C/Break; SSH обрабатывает свои управляющие события. */
static BOOL WINAPI console_control(DWORD event)
{
    return event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT;
}
#endif

/* EN: Run absolute client with argument text, inheriting console I/O; return its exit code or 127 with diagnostics.
 * RU: Запускает абсолютный client с текстом arguments и вводом/выводом консоли; возвращает код либо 127 с диагностикой. */
static gint run_client(const gchar *client, const gchar *arguments)
{
#ifdef G_OS_WIN32
    gchar *line = g_strdup_printf("\"%s\" %s", client, arguments);
    gunichar2 *application = g_utf8_to_utf16(client, -1, NULL, NULL, NULL);
    gunichar2 *command = g_utf8_to_utf16(line, -1, NULL, NULL, NULL);
    STARTUPINFOW start = {0};
    PROCESS_INFORMATION process = {0};
    start.cb = sizeof start;
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    gboolean ok = application && command && CreateProcessW((LPCWSTR)application, (LPWSTR)command,
        NULL, NULL, TRUE, 0, NULL, NULL, &start, &process);
    DWORD failure = ok ? 0 : GetLastError();
    g_free(application);
    g_free(command);
    g_free(line);
    if (!ok)
    {
        gchar *message = g_win32_error_message(failure);
        fprintf(stderr, "Cannot start SSH: %s\n", message);
        g_free(message);
        return 127;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 127;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (gint)code;
#else
    gchar *line = g_strconcat("ssh ", arguments, NULL);
    gchar **argv = NULL;
    GError *error = NULL;
    gint status = 0;
    gboolean ok = g_shell_parse_argv(line, NULL, &argv, &error);
    if (ok)
    {
        g_free(argv[0]);
        argv[0] = g_strdup(client);
        ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_CHILD_INHERITS_STDIN, NULL, NULL,
                          NULL, NULL, &status, &error);
    }
    if (!ok)
        fprintf(stderr, "Cannot start SSH: %s\n", error ? error->message : "invalid command");
    g_clear_error(&error);
    g_strfreev(argv);
    g_free(line);
    return ok ? (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)) : 127;
#endif
}

/* EN: Run argv[1] with argv[2]; argv[3]=close enables automatic closure. Otherwise wait for Enter after every outcome.
 * Passwords are entered by SSH, not passed in arguments, read from the editor vault or intercepted by askpass.
 * RU: Запускает argv[1] с argv[2]; argv[3]=close включает автозакрытие. Иначе ожидает Enter при любом исходе.
 * Пароли вводятся в SSH, не передаются аргументами, не читаются из хранилища и не перехватываются askpass. */
int main(int argc, char **argv)
{
#ifdef G_OS_WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(console_control, TRUE);
    g_auto(GStrv) unicode = g_win32_get_command_line();
    argv = unicode;
    argc = g_strv_length(argv);
#endif
    if (argc != 4 || !g_path_is_absolute(argv[1]))
        return 127;
    g_unsetenv("SSH_ASKPASS");
    g_unsetenv("SSH_ASKPASS_REQUIRE");
    printf("SSH console / SSH-консоль\nClient / Клиент: %s\n", argv[1]);
    fflush(stdout);
    gint code = run_client(argv[1], argv[2]);
    printf("\nSSH exit code / Код завершения SSH: %d\n", code);
    if (strcmp(argv[3], "close"))
    {
        printf("Press Enter to close / Нажмите Enter для закрытия...");
        fflush(stdout);
        while (getchar() != '\n' && !feof(stdin))
        {
        }
    }
    return code;
}
