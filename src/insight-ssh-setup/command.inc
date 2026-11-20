/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Edit and launch literal SSH commands in a separate native console, without the editor askpass channel.
 * RU: Редактирует и запускает явные команды SSH в отдельной обычной консоли без канала askpass редактора. */
#ifdef G_OS_WIN32
#include <windows.h>
#include <glib/gwin32.h>
#endif

/* EN: Quote value for a generated command on this platform; return owned text, retaining Windows path separators.
 * RU: Экранирует value для команды текущей платформы; возвращает собственный текст, сохраняя разделители путей Windows. */
static gchar *setup_command_quote(const gchar *value)
{
#ifdef G_OS_WIN32
    GString *quoted = g_string_new("\"");
    guint slashes = 0;
    for (const gchar *at = value; ; at++)
    {
        if (*at == '\\')
        {
            slashes++;
            continue;
        }
        guint count = (*at == '"' || !*at) ? slashes * 2 : slashes;
        for (guint i = 0; i < count; i++)
            g_string_append_c(quoted, '\\');
        slashes = 0;
        if (!*at)
            break;
        if (*at == '"')
            g_string_append_c(quoted, '\\');
        g_string_append_c(quoted, *at);
    }
    g_string_append_c(quoted, '"');
    return g_string_free(quoted, FALSE);
#else
    return g_shell_quote(value);
#endif
}

/* EN: Fill wizard data's editable command from its nonsecret form fields; button is unused. Never include passwords.
 * RU: Заполняет редактируемую команду wizard из data несекретными полями формы; button не используется. Пароль не включается. */
static void setup_command_fill(GtkWidget *button, gpointer data)
{
    (void)button;
    SshSetupWizard *wizard = data;
    GString *line = g_string_new("ssh");
    guint port = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(wizard->port));
    if (port)
        g_string_append_printf(line, " -p %u", port);
    GtkWidget *entries[] = {wizard->user, wizard->identity, wizard->config, wizard->host};
    const gchar *options[] = {" -l ", " -i ", " -F ", " "};
    for (guint i = 0; i < G_N_ELEMENTS(entries); i++)
    {
        const gchar *value = gtk_entry_get_text(GTK_ENTRY(entries[i]));
        if (!*value)
            continue;
        gchar *quoted = setup_command_quote(value);
        g_string_append(line, options[i]);
        g_string_append(line, quoted);
        g_free(quoted);
    }
    gtk_entry_set_text(GTK_ENTRY(wizard->command), line->str);
    g_string_free(line, TRUE);
}

/* EN: Launch helper/client/arguments in a new Windows console or Unix terminal; close selects retention, errors propagate.
 * RU: Запускает helper/client/arguments в новой консоли Windows либо терминале Unix; close задаёт закрытие, ошибки передаются. */
static gboolean setup_console_spawn(const gchar *helper, const gchar *client, const gchar *arguments,
                                     gboolean close, GError **error)
{
    const gchar *mode = close ? "close" : "keep";
#ifdef G_OS_WIN32
    gchar *qhelper = setup_command_quote(helper);
    gchar *qclient = setup_command_quote(client);
    gchar *qarguments = setup_command_quote(arguments);
    gchar *line = g_strdup_printf("%s %s %s %s", qhelper, qclient, qarguments, mode);
    gunichar2 *application = g_utf8_to_utf16(helper, -1, NULL, NULL, error);
    gunichar2 *command = application ? g_utf8_to_utf16(line, -1, NULL, NULL, error) : NULL;
    STARTUPINFOW start = {0};
    PROCESS_INFORMATION process = {0};
    start.cb = sizeof start;
    gboolean ok = command && CreateProcessW((LPCWSTR)application, (LPWSTR)command, NULL, NULL,
        FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &start, &process);
    if (command && !ok)
    {
        gchar *message = g_win32_error_message(GetLastError());
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot open SSH console: %s", message);
        g_free(message);
    }
    if (ok)
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    g_free(qhelper);
    g_free(qclient);
    g_free(qarguments);
    g_free(line);
    g_free(application);
    g_free(command);
    return ok;
#else
    gchar *terminal = g_find_program_in_path("x-terminal-emulator");
    if (!terminal)
        terminal = g_find_program_in_path("xterm");
    if (!terminal)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "Install xterm or x-terminal-emulator to open a separate SSH console.");
        return FALSE;
    }
    const gchar *argv[] = {terminal, "-e", helper, client, arguments, mode, NULL};
    gboolean ok = g_spawn_async(NULL, (gchar **)argv, NULL, 0, NULL, NULL, NULL, error);
    g_free(terminal);
    return ok;
#endif
}

/* EN: Run wizard data's explicit SSH command after validation; button is unused. Console lifetime belongs to the user.
 * RU: Запускает явную команду SSH wizard из data после проверки; button не используется. Временем жизни консоли управляет пользователь. */
static void setup_command_open(GtkWidget *button, gpointer data)
{
    (void)button;
    SshSetupWizard *wizard = data;
    GError *error = NULL;
    const gchar *arguments = insight_ssh_console_arguments(gtk_entry_get_text(GTK_ENTRY(wizard->command)), &error);
    gchar *client = insight_remote_program("ssh", wizard->client_bin);
    gchar *helper = insight_remote_program("geany-insight-ssh-console", wizard->client_bin);
    if (!error && (!client || !helper))
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "Install ssh and the geany-insight-ssh-console helper beside the editor.");
    gboolean close = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wizard->console_close));
    if (!error)
        setup_console_spawn(helper, client, arguments, close, &error);
    gtk_label_set_text(GTK_LABEL(wizard->status), error ? insight_tr(error->message) : insight_tr(
        "SSH console opened. Enter the password there. This separate login does not verify the wizard connection."));
    g_clear_error(&error);
    g_free(client);
    g_free(helper);
}
