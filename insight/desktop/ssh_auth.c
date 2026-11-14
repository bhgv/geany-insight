/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Configure private per-computer SSH password storage and disposable
 * askpass authentication sessions. RU: Настраивает закрытое хранилище паролей
 * SSH на компьютере и временные сеансы авторизации askpass. */
#include "ssh_auth.h"
#include "glib_compat.h"
#include "remote_credentials.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>

/* EN: Return the owned shared credential identity for trimmed host/user and
 * port; zero means the default SSH port. RU: Возвращает общий идентификатор для
 * host/user без внешних пробелов и port; ноль означает стандартный порт SSH. */
gchar *
insight_ssh_auth_target (const gchar *host, const gchar *user, guint port)
{
    gchar *server = g_strdup (host ? host : ""), *login = g_strdup (user ? user : "");
    g_strstrip (server);
    g_strstrip (login);
    gchar *identity = g_strdup_printf ("%s:%u:%s", server, port ? port : 22, login);
    gchar *target = g_compute_checksum_for_string (G_CHECKSUM_SHA256, identity, -1);
    g_free (identity);
    g_free (server);
    g_free (login);
    return target;
}

/* EN: Return the owned credential identity for private key identity,
 * independent of its remote endpoint. RU: Возвращает идентификатор пароля
 * закрытого ключа identity независимо от удалённого сервера. */
gchar *
insight_ssh_auth_key_target (const gchar *identity)
{
    gchar *absolute = insight_canonicalize_filename (identity, NULL);
    gchar *text = g_strconcat ("private-key:", absolute, NULL);
    gchar *target = g_compute_checksum_for_string (G_CHECKSUM_SHA256, text, -1);
    g_free (text);
    g_free (absolute);
    return target;
}

/* EN: Return the owned private credential filename beneath application
 * configdir, never a project filename. RU: Возвращает имя закрытого хранилища в
 * configdir приложения, никогда не имя файла проекта. */
gchar *
insight_ssh_auth_path (const gchar *configdir)
{
    return g_build_filename (configdir, "insight", "ssh-credentials.conf", NULL);
}

/* EN: Remove only the files in an owned disposable authentication directory;
 * NULL is harmless. RU: Удаляет только файлы собственного временного directory
 * авторизации; NULL допустим. */
void
insight_ssh_auth_cleanup (const gchar *directory)
{
    if (!directory)
        return;
    GDir *dir = g_dir_open (directory, 0, NULL);
    if (dir)
    {
        const gchar *name;
        while ((name = g_dir_read_name (dir)))
        {
            gchar *path = g_build_filename (directory, name, NULL);
            g_remove (path);
            g_free (path);
        }
        g_dir_close (dir);
    }
    g_rmdir (directory);
}

/* EN: Prepare launcher for host/user/port using helper and identity. Return its
 * owned cleanup directory or NULL/error. remember stores credentials beneath
 * configdir; otherwise even typed passwords live only in this disposable
 * session. RU: Готовит launcher для host/user/port через helper и identity.
 * Возвращает каталог очистки либо NULL/error. remember сохраняет данные в
 * configdir; иначе даже введённые пароли существуют только в этом временном
 * сеансе. */
gchar *
insight_ssh_auth_prepare (GSubprocessLauncher *launcher, const gchar *configdir, const gchar *host,
                          const gchar *user, guint port, const gchar *identity, const gchar *helper,
                          gboolean remember, GError **error)
{
    gchar *folder = g_build_filename (configdir, "insight", NULL);
    if (g_mkdir_with_parents (folder, 0700))
    {
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Cannot create SSH settings: %s",
                     g_strerror (errno));
        g_free (folder);
        return NULL;
    }
    gchar *session = g_build_filename (folder, "ssh-auth-XXXXXX", NULL);
    g_free (folder);
    if (!g_mkdtemp (session))
    {
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                     "Cannot create SSH authentication session: %s", g_strerror (errno));
        g_free (session);
        return NULL;
    }
    gchar *path
        = remember ? insight_ssh_auth_path (configdir) : g_build_filename (session, "credentials.conf", NULL);
    gchar *target = insight_ssh_auth_target (host, user, port);
    g_subprocess_launcher_setenv (launcher, "GEANY_INSIGHT_SSH_CONFIG", path, TRUE);
    g_subprocess_launcher_setenv (launcher, "GEANY_INSIGHT_SSH_TARGET", target, TRUE);
    g_subprocess_launcher_setenv (launcher, "GEANY_INSIGHT_SSH_AUTH_SESSION", session, TRUE);
    g_subprocess_launcher_setenv (launcher, "GEANY_INSIGHT_SSH_SAVE_PASSWORD", "1", TRUE);
#ifdef G_OS_WIN32
    gchar *absolute_identity = identity && *identity ? insight_canonicalize_filename(identity, NULL) : g_strdup("");
    g_subprocess_launcher_setenv(launcher, "GEANY_INSIGHT_SSH_KEY", absolute_identity, TRUE);
    g_free(absolute_identity);
#else
    g_subprocess_launcher_setenv (launcher, "GEANY_INSIGHT_SSH_KEY", identity ? identity : "", TRUE);
#endif
    if (helper)
    {
#ifdef G_OS_WIN32
        /* EN: Win32 OpenSSH reads askpass through ANSI getenv. Keep that value ASCII and resolve it in a trusted cwd.
         * RU: Win32 OpenSSH читает askpass через ANSI getenv. Оставляем ASCII-имя и задаём доверенный рабочий каталог. */
        gchar *helper_directory = g_path_get_dirname(helper);
        gchar *helper_name = g_path_get_basename(helper);
        gchar *relative_helper = g_strconcat(".\\", helper_name, NULL);
        g_subprocess_launcher_set_cwd(launcher, helper_directory);
        g_subprocess_launcher_setenv(launcher, "SSH_ASKPASS", relative_helper, TRUE);
        g_free(relative_helper);
        g_free(helper_name);
        g_free(helper_directory);
#else
        g_subprocess_launcher_setenv (launcher, "SSH_ASKPASS", helper, TRUE);
#endif
        g_subprocess_launcher_setenv (launcher, "SSH_ASKPASS_REQUIRE", "force", TRUE);
        if (!g_getenv ("DISPLAY"))
            g_subprocess_launcher_setenv (launcher, "DISPLAY", ":0", TRUE);
    }
    g_free (path);
    g_free (target);
    return session;
}

/* EN: Seed launcher's private askpass store with password, without adding
 * secrets to child arguments or logs. RU: Помещает password в закрытое
 * хранилище launcher без добавления секретов в аргументы процесса и журнал. */
gboolean
insight_ssh_auth_seed (GSubprocessLauncher *launcher, const gchar *password, GError **error)
{
    return insight_remote_password_save (g_subprocess_launcher_getenv (launcher, "GEANY_INSIGHT_SSH_CONFIG"),
                                         g_subprocess_launcher_getenv (launcher, "GEANY_INSIGHT_SSH_TARGET"),
                                         password, error);
}

/* EN: Move this endpoint's legacy password into the per-computer vault after a
 * successful private write. RU: Переносит старый пароль сервера из legacy в
 * хранилище компьютера после успешной закрытой записи. */
void
insight_ssh_auth_migrate (const gchar *legacy, const gchar *configdir, const gchar *host, const gchar *user,
                          guint port)
{
    gchar *path = insight_ssh_auth_path (configdir), *target = insight_ssh_auth_target (host, user, port);
    if (g_strcmp0 (path, legacy))
    {
        gchar *current = insight_remote_password_load (path, target, NULL);
        gchar *old = insight_remote_password_load (legacy, target, NULL);
        if (old && (current || insight_remote_password_save (path, target, old, NULL)))
            insight_remote_password_forget (legacy, target, NULL);
        g_free (current);
        g_free (old);
    }
    g_free (path);
    g_free (target);
}

/* EN: Forget host/user/port's password and the selected identity's passphrase
 * from configdir; propagate write errors. RU: Удаляет пароль host/user/port и
 * пароль ключа identity из configdir; передаёт ошибки записи. */
gboolean
insight_ssh_auth_forget (const gchar *configdir, const gchar *host, const gchar *user, guint port,
                         const gchar *identity, GError **error)
{
    gchar *path = insight_ssh_auth_path (configdir), *target = insight_ssh_auth_target (host, user, port);
    gboolean ok
        = !g_file_test (path, G_FILE_TEST_EXISTS) || insight_remote_password_forget (path, target, error);
    if (ok && identity && *identity)
    {
        gchar *key = insight_ssh_auth_key_target (identity);
        ok = insight_remote_password_forget (path, key, error);
        g_free (key);
    }
    g_free (path);
    g_free (target);
    return ok;
}

/* EN: Move legacy password groups from key to configdir's private file; remove
 * groups only after all copies succeed. RU: Переносит старые группы паролей key
 * в закрытый файл configdir; удаляет группы только после успешного копирования
 * всех.
 */
gboolean
insight_ssh_auth_migrate_key (GKeyFile *key, const gchar *configdir, GError **error)
{
    gchar *path = insight_ssh_auth_path (configdir), *folder = g_path_get_dirname (path);
    gboolean ok = g_mkdir_with_parents (folder, 0700) == 0;
    if (!ok)
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
                     "Cannot create private SSH settings: %s", g_strerror (errno));
    g_free (folder);
    gchar **groups = g_key_file_get_groups (key, NULL);
    const gchar *prefix = "insight-ssh-password-";
    for (guint i = 0; ok && groups[i]; i++)
    {
        if (!g_str_has_prefix (groups[i], prefix))
            continue;
        const gchar *target = groups[i] + strlen (prefix);
        gchar *password = g_key_file_get_string (key, groups[i], "password", NULL);
        gchar *current = insight_remote_password_load (path, target, NULL);
        if (password && !current)
            ok = insight_remote_password_save (path, target, password, error);
        g_free (current);
        g_free (password);
    }
    if (ok)
        for (guint i = 0; groups[i]; i++)
            if (g_str_has_prefix (groups[i], prefix))
                g_key_file_remove_group (key, groups[i], NULL);
    g_strfreev (groups);
    g_free (path);
    return ok;
}
