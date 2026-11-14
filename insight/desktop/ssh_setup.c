/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Validate setup inputs and construct fixed SSH/key-install/debug-server
 * operations with literal arguments. RU: Проверяет ввод мастера и формирует
 * команды SSH, установки ключа и сервера отладки с буквальными аргументами. */
#include "ssh_setup.h"
#include <string.h>

/* EN: Return owned SSH argv for remote using client/config and one command;
 * interactive permits visible askpass. RU: Возвращает argv SSH для remote через
 * client/config и одну command; interactive разрешает видимый askpass. */
gchar **
insight_ssh_setup_argv (const InsightRemote *remote, const gchar *client, const gchar *config,
                        const gchar *command, gboolean interactive)
{
    GPtrArray *args = g_ptr_array_new_with_free_func (g_free);
    gchar **base = insight_remote_argv (remote, FALSE);
    for (guint i = 0; base[i]; i++)
    {
        if (!i)
            g_ptr_array_add (args, g_strdup (client));
        else if (!strcmp (base[i], "-tt"))
            g_ptr_array_add (args, g_strdup ("-T"));
        else if (!strcmp (base[i], "-oBatchMode=no") && !interactive)
            g_ptr_array_add (args, g_strdup ("-oBatchMode=yes"));
        else
        {
            if (!strcmp(base[i], "--"))
            {
                g_ptr_array_add(args, g_strdup("-oControlMaster=no"));
                g_ptr_array_add(args, g_strdup("-oControlPath=none"));
                g_ptr_array_add(args, g_strdup("-oRemoteCommand=none"));
            }
            if (!strcmp (base[i], "--") && config && *config)
            {
                g_ptr_array_add (args, g_strdup ("-F"));
                g_ptr_array_add (args, g_strdup (config));
            }
            g_ptr_array_add (args, g_strdup (base[i]));
        }
    }
    g_strfreev (base);
    g_ptr_array_add (args, g_strdup (command));
    g_ptr_array_add (args, NULL);
    return (gchar **)g_ptr_array_free (args, FALSE);
}

/* EN: Return canonical public key text from text, omitting comments; validate
 * the base64 blob's matching algorithm. RU: Возвращает канонический открытый
 * ключ из text без комментария; проверяет соответствие алгоритма данным base64.
 */
gchar *
insight_ssh_public_key (const gchar *text, GError **error)
{
    gchar *copy = g_strdup (text ? text : "");
    g_strstrip (copy);
    gchar **parts = g_strsplit_set (copy, " \t", -1);
    const gchar *kind = NULL, *blob = NULL;
    for (guint i = 0; parts[i] && !blob; i++)
        if (*parts[i])
        {
            if (!kind)
                kind = parts[i];
            else
                blob = parts[i];
        }
    gboolean valid = strlen (copy) <= 32768 && !strpbrk (copy, "\r\n") && kind && blob && *kind
                     && strlen (kind) < 128
                     && strspn (kind, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRST"
                                      "UVWXYZ0123456789@._+-")
                            == strlen (kind)
                     && strspn (blob, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRST"
                                      "UVWXYZ0123456789+/=")
                            == strlen (blob);
    gsize size = 0;
    guchar *decoded = valid ? g_base64_decode (blob, &size) : NULL;
    guint32 length = size >= 4 ? ((guint32)decoded[0] << 24) | ((guint32)decoded[1] << 16)
                                     | ((guint32)decoded[2] << 8) | decoded[3]
                               : 0;
    valid = valid && length == strlen (kind) && size > 4 + length && !memcmp (decoded + 4, kind, length);
    gchar *result = valid ? g_strdup_printf ("%s %s", kind, blob) : NULL;
    if (!result)
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "Choose a valid SSH private key to obtain its public key.");
    g_free (decoded);
    g_strfreev (parts);
    g_free (copy);
    return result;
}

/* EN: Return an owned idempotent public-key install command for optional
 * absolute directory; NULL uses ~/.ssh. The public key arrives through stdin,
 * never shell interpolation; invalid paths return NULL/error. RU: Возвращает
 * повторяемую команду установки в абсолютный directory; NULL означает ~/.ssh.
 * Открытый ключ поступает через stdin без подстановки в оболочку; неверный путь
 * даёт NULL/error. */
gchar *
insight_ssh_install_key_command (const gchar *directory, GError **error)
{
    if (directory && (*directory != '/' || strlen (directory) > 4096 || strpbrk (directory, "\r\n")))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid SSH key directory.");
        return NULL;
    }
    gchar *folder = directory ? g_shell_quote (directory) : g_strdup ("\"$HOME/.ssh\"");
    gchar *command
        = g_strdup_printf ("umask 077; IFS= read -r key || exit 1; kind=${key%%%% *}; blob=${key#* "
                           "}; folder=%s; "
                           "mkdir -p \"$folder\" && chmod 700 \"$folder\" || exit 1; "
                           "file=\"$folder/authorized_keys\"; touch \"$file\" && chmod 600 "
                           "\"$file\" || exit 1; "
                           "if ! awk -v kind=\"$kind\" -v blob=\"$blob\" "
                           "' {for(i=1;i<NF;i++) if($i==kind && $(i+1)==blob) found=1} END {exit "
                           "!found}' \"$file\"; "
                           "then printf '%%s\\n' \"$key\" >> \"$file\" || exit 1; fi; "
                           "printf 'GEANY-SSH-KEY-INSTALLED\\n'",
                           folder);
    g_free (folder);
    return command;
}

/* EN: Build the remote test for optional absolute program; return NULL/error
 * for invalid paths. RU: Формирует проверку удалённой абсолютной program при её
 * наличии; неверный путь даёт NULL/error. */
gchar *
insight_ssh_check_command (const gchar *program, GError **error)
{
    if (program && *program && (*program != '/' || strlen (program) > 4096 || strpbrk (program, "\r\n")))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Choose an absolute remote program path.");
        return NULL;
    }
    if (program && *program)
    {
        gchar *quoted = g_shell_quote (program);
        gchar *command = g_strdup_printf ("command -v gdbserver >/dev/null || { printf 'Install "
                                          "gdbserver on this server.\\n' >&2; exit 2; }; "
                                          "test -x %s || { printf 'Remote program does not exist "
                                          "or is not executable.\\n' >&2; exit 3; }; "
                                          "printf 'GEANY-SSH-READY\\n'",
                                          quoted);
        g_free (quoted);
        return command;
    }
    return g_strdup ("printf 'GEANY-SSH-READY\\n'");
}

/* EN: Return an owned foreground gdbserver command for absolute directory/port;
 * invalid input gives NULL/error. RU: Возвращает команду gdbserver на переднем
 * плане для абсолютных directory/port; неверный ввод даёт NULL/error. */
gchar *
insight_ssh_server_command (const gchar *directory, guint port, GError **error)
{
    if (!directory || *directory != '/' || strlen (directory) > 4096 || strpbrk (directory, "\r\n") || !port
        || port > 65535)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "Choose an absolute remote directory and a valid debug port.");
        return NULL;
    }
    gchar *quoted = g_shell_quote (directory);
    gchar *command = g_strdup_printf ("cd %s || exit 1; "
                                      "env LC_ALL=C gdbserver --multi 127.0.0.1:%u & server=$!; "
                                      "(while IFS= read -r ignored; do :; done; kill \"$server\" 2>/dev/null) <&0 & watcher=$!; "
                                      "trap 'kill \"$server\" \"$watcher\" 2>/dev/null' 0 HUP INT TERM; "
                                      "wait \"$server\"; result=$?; kill \"$watcher\" 2>/dev/null; "
                                      "wait \"$watcher\" 2>/dev/null; exit \"$result\"",
                                      quoted, port);
    g_free (quoted);
    return command;
}
