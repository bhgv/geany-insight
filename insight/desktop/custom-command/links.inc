/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Parse built-in and bounded named-group diagnostic formats into validated local source locations.
 * RU: Разбирает встроенные и ограниченные форматы диагностики с именованными группами в проверенные локальные позиции. */

/* EN: Compile optional pattern with required file/line names and resource limits; empty/NULL means built-ins.
 * Return owned regex or NULL/error; no source files are read.
 * RU: Компилирует необязательный pattern с обязательными file/line и границами работы; пустой/NULL означает встроенные форматы.
 * Возвращает собственный regex либо NULL/error; исходники не читаются. */
GRegex *insight_custom_diagnostic_regex(const gchar *pattern, GError **error)
{
    if (!pattern || !*pattern)
        return NULL;
    if (!field(pattern, 1024))
    {
        fail(error, "Diagnostic pattern must be one UTF-8 line up to 1024 bytes");
        return NULL;
    }
    GRegex *validated = g_regex_new(pattern, 0, 0, error);
    if (!validated)
        return NULL;
    if (g_regex_get_string_number(validated, "file") < 0 || g_regex_get_string_number(validated, "line") < 0)
    {
        g_regex_unref(validated);
        fail(error, "Diagnostic pattern requires named groups file and line");
        return NULL;
    }
    g_regex_unref(validated);
    gchar *bounded = g_strconcat("(*LIMIT_MATCH=10000)(*LIMIT_RECURSION=256)", pattern, NULL);
    GRegex *regex = g_regex_new(bounded, 0, 0, error);
    g_free(bounded);
    return regex;
}

/* EN: Release owned link data; NULL is safe.
 * RU: Освобождает собственные данные link data; NULL безопасен. */
void insight_custom_link_free(gpointer data)
{
    InsightCustomLink *link = data;
    if (!link)
        return;
    g_free(link->path);
    g_free(link->message);
    g_free(link);
}

/* EN: Strip bounded ANSI SGR sequences from input, retaining all other bytes; return owned stripped text.
 * RU: Удаляет ограниченные ANSI SGR из input, сохраняя остальные байты; возвращает собственный очищенный текст. */
static gchar *diagnostic_plain(const gchar *input)
{
    GString *plain = g_string_new(NULL);
    for (const gchar *p = input; *p;)
    {
        if ((guchar)p[0] == 27 && p[1] == '[')
        {
            const gchar *q = p + 2;
            while (q - p < 64 && (g_ascii_isdigit(*q) || *q == ';' || *q == ':'))
                q++;
            if (*q == 'm')
            {
                p = q + 1;
                continue;
            }
        }
        g_string_append_c(plain, *p++);
    }
    gchar *text = g_string_free(plain, FALSE);
    g_strstrip(text);
    return text;
}

/* EN: Validate a decimal coordinate text; optional permits absent/empty text as zero, otherwise require 1..G_MAXINT.
 * RU: Проверяет десятичную координату text; optional разрешает отсутствие/пустоту как ноль, иначе требуется 1..G_MAXINT. */
static gboolean diagnostic_number(const gchar *text, gboolean optional, guint *value)
{
    *value = 0;
    if (!text || !*text)
        return optional;
    for (const gchar *p = text; *p; p++)
        if (!g_ascii_isdigit(*p))
            return FALSE;
    guint64 number = g_ascii_strtoull(text, NULL, 10);
    if (!number || number > G_MAXINT)
        return FALSE;
    *value = number;
    return TRUE;
}

/* EN: Validate captured filename/coordinates/message against directory; msvc enables relative backslash normalization.
 * Inputs are borrowed; return owned local link or NULL without filesystem access.
 * RU: Проверяет file/number/column/message относительно directory; msvc включает нормализацию относительных обратных слэшей.
 * Ввод заимствован; возвращает собственную локальную ссылку либо NULL без доступа к файловой системе. */
static InsightCustomLink *diagnostic_location(const gchar *file, const gchar *number, const gchar *column,
    const gchar *message, const gchar *directory, gboolean msvc)
{
    gchar *path = g_strdup(file ? file : "");
    g_strstrip(path);
    gsize length = strlen(path);
    if (length >= 2 && path[0] == '"' && path[length - 1] == '"')
    {
        path[length - 1] = 0;
        memmove(path, path + 1, length - 1);
        length -= 2;
    }
    guint line = 0, col = 0;
    gboolean valid = length > 0 && length <= 16384 && strcmp(path, "-") && !strpbrk(path, "\r\n\033") &&
        !strstr(path, "://") && !(path[0] == '<' && path[length - 1] == '>') &&
        diagnostic_number(number, FALSE, &line) && diagnostic_number(column, TRUE, &col);
#ifndef G_OS_WIN32
    if ((g_ascii_isalpha(path[0]) && path[1] == ':') || (path[0] == '\\' && path[1] == '\\'))
        valid = FALSE;
#endif
    InsightCustomLink *link = NULL;
    if (valid)
    {
        if (msvc)
            for (gchar *p = path; *p; p++)
                if (*p == '\\')
                    *p = G_DIR_SEPARATOR;
        link = g_new0(InsightCustomLink, 1);
        link->path = insight_canonicalize_filename(path, directory);
        link->line = line;
        link->column = col;
        link->message = g_strdup(message ? message : "");
    }
    g_free(path);
    return link;
}

/* EN: Parse input using an optional custom regex with file/line/column/message names, or built-in formats when NULL.
 * directory is the captured absolute launch directory. Return owned link or NULL/no match; regex limits report error.
 * RU: Разбирает input необязательным regex с file/line/column/message либо встроенными форматами при NULL.
 * directory — зафиксированный абсолютный каталог запуска. Собственная ссылка либо NULL/нет совпадения; границы regex сообщаются в error. */
InsightCustomLink *insight_custom_source_link_pattern(const gchar *input, const gchar *directory,
    GRegex *regex, GError **error)
{
    if (!input || strlen(input) > 65536 || !g_utf8_validate(input, -1, NULL) ||
        !field(directory, 16384) || !g_path_is_absolute(directory))
        return NULL;
    gchar *text = diagnostic_plain(input);
    InsightCustomLink *link = NULL;
    if (regex)
    {
        GMatchInfo *match = NULL;
        if (g_regex_match_full(regex, text, -1, 0, 0, &match, error))
        {
            gchar *file = g_match_info_fetch_named(match, "file");
            gchar *number = g_match_info_fetch_named(match, "line");
            gchar *column = g_regex_get_string_number(regex, "column") >= 0 ? g_match_info_fetch_named(match, "column") : NULL;
            gchar *message = g_regex_get_string_number(regex, "message") >= 0 ? g_match_info_fetch_named(match, "message") : g_strdup(text);
            link = diagnostic_location(file, number, column, message, directory, FALSE);
            g_free(file);
            g_free(number);
            g_free(column);
            g_free(message);
        }
        if (match)
            g_match_info_free(match);
    }
    else
    {
        static gsize ready;
        static GRegex *patterns[3];
        if (g_once_init_enter(&ready))
        {
            patterns[0] = g_regex_new("^(.+?):([0-9]+)(?::([0-9]+))?:[ \\t]*(.*)$", G_REGEX_OPTIMIZE, 0, NULL);
            patterns[1] = g_regex_new("^(.+?)\\(([0-9]+)(?:,([0-9]+))?\\)[ \\t]*:[ \\t]*(.*)$", G_REGEX_OPTIMIZE, 0, NULL);
            patterns[2] = g_regex_new("^[ \\t]*File \"(.+)\", line ([0-9]+)(?:,.*)?$", G_REGEX_OPTIMIZE, 0, NULL);
            g_once_init_leave(&ready, 1);
        }
        for (guint i = 0; i < 3 && !link; i++)
        {
            GMatchInfo *match = NULL;
            if (g_regex_match(patterns[i], text, 0, &match))
            {
                gchar *file = g_match_info_fetch(match, 1);
                gchar *number = g_match_info_fetch(match, 2);
                gchar *column = i < 2 ? g_match_info_fetch(match, 3) : NULL;
                gchar *message = i < 2 ? g_match_info_fetch(match, 4) : g_strdup(text);
                link = diagnostic_location(file, number, column, message, directory, i == 1);
                g_free(file);
                g_free(number);
                g_free(column);
                g_free(message);
            }
            if (match)
                g_match_info_free(match);
        }
    }
    g_free(text);
    return link;
}

/* EN: Parse one built-in GCC/MSVC/Python diagnostic; no match returns NULL, with no file access.
 * RU: Разбирает одну встроенную диагностику GCC/MSVC/Python; без совпадения NULL, без доступа к файлам. */
InsightCustomLink *insight_custom_source_link(const gchar *input, const gchar *directory)
{
    return insight_custom_source_link_pattern(input, directory, NULL, NULL);
}
