/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Plan and own private selection snapshots for the lifetime of a custom-command process.
 * RU: Планирует и хранит частные снимки выделения на время процесса пользовательской команды. */
#include <glib/gstdio.h>
#include <errno.h>

/* EN: Detect an actual short %s token in one parsed argument, ignoring escaped percents and long variables.
 * RU: Находит настоящий короткий %s в разобранном argument, пропуская экранированные проценты и длинные переменные. */
static gboolean selection_token(const gchar *argument)
{
    for (const gchar *at = argument; *at;)
    {
        if (*at != '%')
        {
            at++;
            continue;
        }
        if (at[1] == '%')
        {
            at += 2;
            continue;
        }
        const gchar *end = strchr(at + 1, '%');
        gboolean long_name = end && end - at > 2 && end - at <= 65;
        if (long_name)
            for (const gchar *p = at + 1; p < end; p++)
                if (!g_ascii_isalnum(*p) && *p != '_')
                    long_name = FALSE;
        if (long_name)
            at = end + 1;
        else
        {
            if (at[1] == 's')
                return TRUE;
            at += at[1] ? 2 : 1;
        }
    }
    return FALSE;
}

/* EN: Parse input without substitutions and report whether %s is requested; invalid input returns FALSE/error.
 * No files or processes are created, and percent signs inside substituted values are never examined.
 * RU: Разбирает input без подстановок и сообщает о запросе %s; неверный ввод даёт FALSE/error.
 * Не создаёт файлов или процессов и не рассматривает проценты внутри подставляемых значений. */
gboolean insight_custom_selection_requested(const gchar *input, GError **error)
{
    if (!field(input, 16384))
        return fail(error, "Invalid custom command template");
    gint count = 0;
    gchar **argv = NULL;
    if (!g_shell_parse_argv(input, &count, &argv, error))
        return FALSE;
    gboolean requested = FALSE;
    for (gint i = 0; i < count && !requested; i++)
        requested = selection_token(argv[i]);
    g_strfreev(argv);
    return requested;
}

/* EN: Copy nonempty UTF-8 text of exact length (at most 1 MiB, no NUL) and plan a unique absolute temp path.
 * Return owned snapshot or NULL/error; planning never creates a directory or file.
 * RU: Копирует непустой UTF-8 text точной длины length (до 1 MiB, без NUL), планируя уникальный абсолютный путь.
 * Возвращает собственный снимок либо NULL/error; планирование не создаёт каталога или файла. */
InsightCustomSelection *insight_custom_selection_plan(const gchar *text, gsize length, GError **error)
{
    if (!text || !length || length > 1024u * 1024u || memchr(text, 0, length) ||
        !g_utf8_validate(text, length, NULL))
    {
        fail(error, "Selection snapshot requires 1–1048576 bytes of UTF-8 text without NUL");
        return NULL;
    }
    InsightCustomSelection *snapshot = g_new0(InsightCustomSelection, 1);
    snapshot->text = g_strndup(text, length);
    snapshot->length = length;
    gchar *uuid = g_uuid_string_random();
    gchar *name = g_strconcat("geany-insight-selection-", uuid, NULL);
    gchar *root = insight_canonicalize_filename(g_get_tmp_dir(), NULL);
    snapshot->directory = g_build_filename(root, name, NULL);
    snapshot->path = g_build_filename(snapshot->directory, "selection.utf8", NULL);
    g_free(root);
    g_free(name);
    g_free(uuid);
    return snapshot;
}

/* EN: Materialize planned snapshot once, using a new private directory and file; return success or error.
 * Failed attempts retain ownership of any created resources for snapshot_free; existing paths are never overwritten.
 * RU: Однократно создаёт запланированный snapshot в новом частном каталоге/файле; возвращает успех либо error.
 * После ошибки созданные ресурсы остаются во владении snapshot_free; существующие пути не перезаписываются. */
gboolean insight_custom_selection_create(InsightCustomSelection *snapshot, GError **error)
{
    if (!snapshot || snapshot->created)
        return fail(error, "Selection snapshot is already materialized or unavailable");
    if (g_mkdir(snapshot->directory, 0700))
    {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
            "Cannot create selection snapshot directory: %s", g_strerror(errno));
        return FALSE;
    }
    snapshot->created = TRUE;
    GFile *file = g_file_new_for_path(snapshot->path);
    GFileOutputStream *stream = g_file_create(file, G_FILE_CREATE_PRIVATE, NULL, error);
    g_object_unref(file);
    if (!stream)
        return FALSE;
    gboolean ok = g_output_stream_write_all(G_OUTPUT_STREAM(stream), snapshot->text,
        snapshot->length, NULL, NULL, error);
    gboolean closed = g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, ok ? error : NULL);
    g_object_unref(stream);
    return ok && closed;
}

/* EN: Remove only snapshot's owned file and empty directory, then release memory; NULL and unmaterialized plans are safe.
 * Cleanup errors are reported as warnings; unrelated pre-existing paths and extra child-created files are never recursively removed.
 * RU: Удаляет только собственный файл snapshot и пустой каталог, затем память; NULL и нереализованные планы безопасны.
 * Ошибки очистки выводятся предупреждением; чужие пути и дополнительные файлы процесса рекурсивно не удаляются. */
void insight_custom_selection_free(InsightCustomSelection *snapshot)
{
    if (!snapshot)
        return;
    if (snapshot->created)
    {
        if (g_unlink(snapshot->path) && errno != ENOENT)
            g_warning("Cannot remove selection snapshot: %s", g_strerror(errno));
        if (g_rmdir(snapshot->directory) && errno != ENOENT)
            g_warning("Cannot remove selection snapshot directory: %s", g_strerror(errno));
    }
    g_free(snapshot->text);
    g_free(snapshot->directory);
    g_free(snapshot->path);
    g_free(snapshot);
}
