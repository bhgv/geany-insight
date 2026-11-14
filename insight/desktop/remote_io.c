/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Validate remote endpoints and construct literal OpenSSH transfer arguments.
 * RU: Проверяет удалённые адреса и формирует буквальные аргументы передачи OpenSSH. */
#include "remote_io.h"
#include "glib_compat.h"
#include <string.h>
/* EN: Return whether path is bounded UTF-8 without line breaks.
 * RU: Проверяет ограниченный UTF-8 path без переводов строк. */
static gboolean valid_path(const char *path)
{
    return path && *path && strlen(path) <= 16384 && g_utf8_validate(path, -1, NULL) && !strchr(path, '\n') &&
           !strchr(path, '\r');
}
/* EN: Validate s as a literal login or host alias selected by host.
 * RU: Проверяет s как буквальный логин либо псевдоним сервера по host. */
static gboolean identifier(const char *s, gboolean host)
{
    if (!s || !*s || (!g_ascii_isalnum(*s) && !(host && *s == ':') && *s != '_') || strlen(s) > 255)
        return FALSE;
    for (; *s; s++)
        if (!g_ascii_isalnum(*s) && !strchr(host ? "._-:" : "._-", *s))
            return FALSE;
    return TRUE;
}
/* EN: Validate host/user, port (zero uses SSH config), identity and remote/local paths; return an owned
 * endpoint or error. RU: Проверяет host/user, port (ноль — SSH config), identity и пути remote/local;
 * возвращает сервер либо error. */
InsightRemote *insight_remote_new(const char *host, const char *user, guint port, const char *identity,
                                  const char *remote, const char *local, GError **error)
{
    if (!identifier(host, TRUE) || (user && *user && !identifier(user, FALSE)) || port > 65535 ||
        !valid_path(remote) || *remote != '/' || !valid_path(local) || !g_path_is_absolute(local) ||
        (identity && *identity && !valid_path(identity)))
    {
        g_set_error_literal(
            error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
            "Use a host/SSH alias, valid user and port, absolute remote path and local cache path");
        return NULL;
    }
    InsightRemote *endpoint = g_new0(InsightRemote, 1);
    endpoint->host = g_strdup(host);
    endpoint->user = g_strdup(user ? user : "");
    endpoint->port = port;
    endpoint->identity = g_strdup(identity ? identity : "");
    endpoint->remote = g_strdup(remote);
    endpoint->local = insight_canonicalize_filename(local, NULL);
    return endpoint;
}
/* EN: Release endpoint r and its owned strings; NULL is harmless.
 * RU: Освобождает сервер r и его строки; NULL допустим. */
void insight_remote_free(InsightRemote *r)
{
    if (!r)
        return;
    g_free(r->host);
    g_free(r->user);
    g_free(r->identity);
    g_free(r->remote);
    g_free(r->local);
    g_free(r);
}
/* EN: Resolve program beside directory, in PATH or Windows system OpenSSH; return an owned absolute path or
 * NULL. RU: Ищет program в directory, PATH либо системном OpenSSH Windows; возвращает абсолютный путь либо
 * NULL. */
gchar *insight_remote_program(const gchar *program, const gchar *directory)
{
#ifdef G_OS_WIN32
    gchar *name = g_strconcat(program, ".exe", NULL);
#else
    gchar *name = g_strdup(program);
#endif
    gchar *path = directory ? g_build_filename(directory, name, NULL) : NULL;
    g_free(name);
    if (path && g_file_test(path, G_FILE_TEST_IS_REGULAR) && g_file_test(path, G_FILE_TEST_IS_EXECUTABLE))
    {
        gchar *absolute = insight_canonicalize_filename(path, NULL);
        g_free(path);
        return absolute;
    }
    g_free(path);
    path = g_find_program_in_path(program);
#ifdef G_OS_WIN32
    if (!path && (!strcmp(program, "ssh") || !strcmp(program, "sftp") || !strcmp(program, "scp") ||
                  !strcmp(program, "ssh-keygen")))
    {
        const gchar *root = g_getenv("SystemRoot");
        if (root)
        {
            name = g_strconcat(program, ".exe", NULL);
            const gchar *folders[] = {"Sysnative", "System32"};
            for (guint i = 0; i < G_N_ELEMENTS(folders) && !path; i++)
            {
                gchar *candidate = g_build_filename(root, folders[i], "OpenSSH", name, NULL);
                if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR) &&
                    g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE))
                    path = candidate;
                else
                    g_free(candidate);
            }
            g_free(name);
        }
    }
#endif
    if (path && !g_path_is_absolute(path))
    {
        gchar *absolute = insight_canonicalize_filename(path, NULL);
        g_free(path);
        path = absolute;
    }
    return path;
}
/* EN: Return owned SSH/SFTP arguments for endpoint r; Windows local key paths are absolute before child cwd
 * changes. RU: Возвращает аргументы SSH/SFTP сервера r; локальный ключ Windows получает абсолютный путь до
 * смены каталога процесса. */
gchar **insight_remote_argv(const InsightRemote *r, gboolean sftp)
{
    GPtrArray *args = g_ptr_array_new();
    g_ptr_array_add(args, g_strdup(sftp ? "sftp" : "ssh"));
    g_ptr_array_add(args, g_strdup("-oBatchMode=no"));
    g_ptr_array_add(args, g_strdup("-oConnectTimeout=15"));
    g_ptr_array_add(args, g_strdup("-oServerAliveInterval=15"));
    g_ptr_array_add(args, g_strdup("-oServerAliveCountMax=3"));
    if (*r->user)
        g_ptr_array_add(args, g_strdup_printf("-oUser=%s", r->user));
    if (*r->identity)
    {
        g_ptr_array_add(args, g_strdup("-i"));
#ifdef G_OS_WIN32
        g_ptr_array_add(args, insight_canonicalize_filename(r->identity, NULL));
#else
        g_ptr_array_add(args, g_strdup(r->identity));
#endif
    }
    if (r->port)
    {
        g_ptr_array_add(args, g_strdup(sftp ? "-P" : "-p"));
        g_ptr_array_add(args, g_strdup_printf("%u", r->port));
    }
    if (sftp)
    {
        g_ptr_array_add(args, g_strdup("-b"));
        g_ptr_array_add(args, g_strdup("-"));
    }
    else
        g_ptr_array_add(args, g_strdup("-tt"));
    g_ptr_array_add(args, g_strdup("--"));
    g_ptr_array_add(args,
                    sftp && strchr(r->host, ':') ? g_strdup_printf("[%s]", r->host) : g_strdup(r->host));
    g_ptr_array_add(args, NULL);
    return (gchar **)g_ptr_array_free(args, FALSE);
}
/* EN: Build owned SCP arguments for r and absolute local; upload/folder choose direction and recursion; invalid paths return NULL/error.
 * RU: Формирует SCP-аргументы для r и абсолютного local; upload/folder задают направление и рекурсию; неверный путь даёт NULL/error. */
gchar **insight_remote_scp_argv(const InsightRemote *r, const gchar *local, gboolean upload, gboolean folder,
                                GError **error)
{
    if (!valid_path(local) || !g_path_is_absolute(local))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "SCP requires an absolute local path");
        return NULL;
    }
    for (const gchar *p = r->remote; *p; p++)
        if (!g_ascii_isalnum(*p) && !strchr("/._-", *p))
        {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "SCP remote paths may contain letters, digits, / . _ - only. Use Open remote "
                                "via SFTP for other names.");
            return NULL;
        }
    gchar **base = insight_remote_argv(r, TRUE);
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(args, g_strdup("scp"));
    /* Reuse authentication/keepalive options, excluding SFTP batch/host. */
    guint n = g_strv_length(base);
    for (guint i = 1; i < n - 4; i++)
        g_ptr_array_add(args, g_strdup(base[i]));
    if (folder)
        g_ptr_array_add(args, g_strdup("-r"));
    g_ptr_array_add(args, g_strdup("--"));
    gchar *host = strchr(r->host, ':') ? g_strdup_printf("[%s]", r->host) : g_strdup(r->host),
          *remote = g_strdup_printf("%s:%s", host, r->remote);
    g_free(host);
    g_ptr_array_add(args, g_strdup(upload ? local : remote));
    g_ptr_array_add(args, g_strdup(upload ? remote : local));
    g_free(remote);
    g_strfreev(base);
    g_ptr_array_add(args, NULL);
    return (gchar **)g_ptr_array_free(args, FALSE);
}
/* EN: Build owned SSH archive arguments for r; tar selects tar.gz instead of gzip; reject root paths with NULL/error.
 * RU: Формирует SSH-аргументы архива для r; tar выбирает tar.gz вместо gzip; корневой путь даёт NULL/error. */
gchar **insight_remote_archive_argv(const InsightRemote *r, gboolean tar, GError **error)
{
    if (!valid_path(r->remote) || !strcmp(r->remote, "/"))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Choose a remote file or folder below the root directory");
        return NULL;
    }
    gchar **base = insight_remote_argv(r, FALSE);
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; base[i]; i++)
        g_ptr_array_add(args, g_strdup(!strcmp(base[i], "-tt") ? "-T" : base[i]));
    g_strfreev(base);
    gchar *command;
    if (tar)
    {
        gchar *dir = g_path_get_dirname(r->remote), *name = g_path_get_basename(r->remote),
              *a = g_shell_quote(dir), *b = g_shell_quote(name);
        command = g_strdup_printf("tar -czf - -C %s -- %s", a, b);
        g_free(a);
        g_free(b);
        g_free(dir);
        g_free(name);
    }
    else
    {
        gchar *path = g_shell_quote(r->remote);
        command = g_strdup_printf("gzip -c -- %s", path);
        g_free(path);
    }
    g_ptr_array_add(args, command);
    g_ptr_array_add(args, NULL);
    return (gchar **)g_ptr_array_free(args, FALSE);
}
/* OpenSSH's quoted pathname parser protects glob characters itself. Escape
 * quotes/backslashes only; extra glob escapes would become literal slashes. */
/* EN: Return an owned SFTP-quoted path; glob is retained for compatibility because the parser already quotes wildcards.
 * RU: Возвращает path в кавычках SFTP; glob сохранён для совместимости, парсер уже защищает шаблоны. */
static gchar *quote(const char *path, gboolean glob)
{
    (void)glob;
    GString *s = g_string_new("\"");
    for (const char *p = path; *p; p++)
    {
        if (*p == '\\' || *p == '"')
            g_string_append_c(s, '\\');
        g_string_append_c(s, *p);
    }
    g_string_append_c(s, '"');
    return g_string_free(s, FALSE);
}
/* EN: Build an owned SFTP batch for remote/local and recursive folder; invalid paths return NULL/error.
 * RU: Формирует SFTP-задание remote/local с рекурсией folder; неверные пути дают NULL/error. */
gchar *insight_remote_download(const char *remote, const char *local, gboolean folder, GError **error)
{
    if (!valid_path(remote) || *remote != '/' || !valid_path(local))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid SFTP path");
        return NULL;
    }
    gchar *a = quote(remote, TRUE), *b = quote(local, FALSE),
          *batch = g_strdup_printf("get %s%s %s\nbye\n", folder ? "-R " : "", a, b);
    g_free(a);
    g_free(b);
    return batch;
}
/* EN: Build an owned staged upload for local/remote using nonce, then rename; invalid values return NULL/error.
 * RU: Формирует передачу local/remote через временный nonce и переименование; неверные значения дают NULL/error. */
gchar *insight_remote_upload(const char *local, const char *remote, const char *nonce, GError **error)
{
    if (!valid_path(local) || !valid_path(remote) || *remote != '/' || !identifier(nonce, FALSE))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid SFTP upload path");
        return NULL;
    }
    gchar *temporary = g_strdup_printf("%s.geany-insight-%s.tmp", remote, nonce), *a = quote(local, TRUE),
          *b = quote(temporary, FALSE), *c = quote(remote, FALSE);
    gchar *batch = g_strdup_printf("put %s %s\nrename %s %s\nbye\n", a, b, b, c);
    g_free(a);
    g_free(b);
    g_free(c);
    g_free(temporary);
    return batch;
}
/* EN: Map local into endpoint r; return an owned server path only within its cache root, otherwise NULL.
 * RU: Сопоставляет local серверу r; возвращает путь сервера только внутри корня кэша, иначе NULL. */
gchar *insight_remote_path(const InsightRemote *r, const char *local)
{
    gchar *canonical = insight_canonicalize_filename(local, NULL);
    gsize prefix = strlen(r->local);
    gchar *result = NULL;
    if (!strncmp(canonical, r->local, prefix) && G_IS_DIR_SEPARATOR(canonical[prefix]))
    {
        gchar *relative = g_strdup(canonical + prefix + 1);
        for (char *p = relative; *p; p++)
            if (G_IS_DIR_SEPARATOR(*p))
                *p = '/';
        result = g_strdup_printf("%s%s%s", r->remote, g_str_has_suffix(r->remote, "/") ? "" : "/", relative);
        g_free(relative);
    }
    g_free(canonical);
    return result;
}
/* EN: Return an owned lexical POSIX path for path relative to base, without accessing a remote filesystem.
 * RU: Возвращает лексический POSIX-путь path относительно base без обращения к удалённой файловой системе. */
gchar *insight_remote_normalize(const char *base, const char *path)
{
    gchar *joined = *path == '/' ? g_strdup(path) : g_strdup_printf("%s/%s", base, path),
          **parts = g_strsplit(joined, "/", -1);
    GPtrArray *stack = g_ptr_array_new();
    for (guint i = 0; parts[i]; i++)
    {
        if (!*parts[i] || !strcmp(parts[i], "."))
            continue;
        if (!strcmp(parts[i], ".."))
        {
            if (stack->len)
                g_ptr_array_remove_index(stack, stack->len - 1);
        }
        else
            g_ptr_array_add(stack, parts[i]);
    }
    GString *out = g_string_new("");
    for (guint i = 0; i < stack->len; i++)
    {
        g_string_append_c(out, '/');
        g_string_append(out, g_ptr_array_index(stack, i));
    }
    if (!out->len)
        g_string_append_c(out, '/');
    g_ptr_array_free(stack, TRUE);
    g_strfreev(parts);
    g_free(joined);
    return g_string_free(out, FALSE);
}
