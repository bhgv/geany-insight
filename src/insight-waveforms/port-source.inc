/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Source previews, preprocessing and port declarations.
 * RU: Просмотр исходников, препроцессор и объявления портов.
 */

typedef struct
{
    gchar direction[8];
    gint left, right;
    guint width, order;
} PortInfo;

enum
{
    PORT_NAME,
    PORT_DIRECTION,
    PORT_BINDINGS,
    PORT_WIDTH_TEXT,
    PORT_WIDTH,
    PORT_LEFT,
    PORT_RIGHT,
    PORT_ORDER,
    PORT_OCCUPIED,
    PORT_DIRECTION_RANK,
    PORT_COLUMNS
};

typedef struct
{
    const gchar *source, *expanded, *preprocess_error;
    GtkTextBuffer *recipe;
    GtkTextView *recipe_view;
    GtkListStore *store;
    GHashTable *ports;
    gchar *module;
    GtkWidget *tree, *search, *compatible, *regex_toggle, *sort_choice, *reverse;
    GtkWidget *search_target, *occupancy;
    GtkTreeModelFilter *filter;
    GtkTreeSortable *sorted;
    GRegex *query_regex;
    gchar *folded_query;
    gboolean regex_invalid;
    gchar direction;
    guint required_width;
    gboolean choosing, prefer_compatible;
    gint last_device;
} PortBrowser;

/* EN: Read a source snapshot from path for the setup dialog. Return UTF-8 text or NULL.
 * RU: Читает снимок исходника по пути path для окна настройки. Возвращает UTF-8 или NULL. */
static gchar *port_browser_source(const gchar *path)
{
    gchar *source = NULL;
    gsize size;
    if (!g_file_get_contents(path, &source, &size, NULL))
        return NULL;
    if (!g_utf8_validate(source, size, NULL))
    {
        gchar *valid = g_utf8_make_valid(source, size);
        g_free(source);
        return valid;
    }
    return source;
}

/* EN: Add sources to command, using option and path for long source lists.
 * RU: Добавляет sources в command, используя option и path для длинного списка. */
static gboolean simulation_source_arguments(GPtrArray *command, GPtrArray *sources,
                                             const gchar *option, const gchar *path, GError **error)
{
    gsize bytes = 0;
    for (guint i = 0; i < sources->len; ++i)
        bytes += 2 * strlen(sources->pdata[i]) + 3;
    if (bytes < 16000)
    {
        for (guint i = 0; i < sources->len; ++i)
            g_ptr_array_add(command, sources->pdata[i]);
        return TRUE;
    }

    GString *list = g_string_sized_new(bytes / 2);
    gboolean valid = TRUE;
    for (guint i = 0; i < sources->len; ++i)
    {
        const gchar *source = sources->pdata[i];
        if (strpbrk(source, "\r\n") || (!strcmp(option, "-c") &&
            (strchr(source, '#') || strstr(source, "//") || strstr(source, "/*"))))
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                        "Cannot put this source path in the Icarus source list: %s", source);
            valid = FALSE;
            break;
        }
        g_string_append_printf(list, "%s\n", source);
    }
    if (valid)
        valid = g_file_set_contents(path, list->str, list->len, error);
    if (valid)
    {
        g_ptr_array_add(command, (gpointer)option);
        g_ptr_array_add(command, (gpointer)path);
    }
    g_string_free(list, TRUE);
    return valid;
}

/* EN: Preprocess the run sources in compiler order using the simulation include path.
 * Return owned UTF-8 HDL, or NULL and an owned diagnostic in error.
 * RU: Раскрывает макросы в исходниках run в порядке компиляции и с путём include
 * симуляции. Возвращает новый текст UTF-8 либо NULL и сообщение в error. */
static gchar *port_browser_preprocess(const WaveSimulation *run, gchar **error)
{
    GError *failure = NULL;
    gchar *expanded = NULL;
    GSubprocessLauncher *launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_set_cwd(launcher, run->include_dir);
    gchar *bin = g_path_get_dirname(run->compiler);
    const gchar *existing = g_getenv("PATH");
    gchar *path = g_strconcat(bin, G_SEARCHPATH_SEPARATOR_S, existing ? existing : "", NULL);
    g_subprocess_launcher_setenv(launcher, "PATH", path, TRUE);
    g_free(path);
    const gchar *options[] = {run->compiler, "-g2012", "-grelative-include", "-I", run->include_dir, "-E",
                              "-o",          "-"};
    GPtrArray *command = g_ptr_array_new();
#ifdef G_OS_WIN32
    /* EN: Capture the bundled helper's diagnostics directly, avoiding a second
     * Windows process launch and the driver's temporary source/define lists.
     * RU: Читаем диагностику комплектного препроцессора напрямую, без второго
     * запуска процесса Windows и временных списков исходников и макросов драйвера. */
    gchar *helper = g_build_filename(bin, "..", "lib", "ivl", "ivlpp.exe", NULL);
    gchar *directory = NULL;
    gchar *flags_file = NULL;
    if (g_file_test(helper, G_FILE_TEST_IS_REGULAR))
    {
        directory = g_dir_make_tmp("geany-verilog-ports-XXXXXX", &failure);
        if (directory)
        {
            flags_file = g_build_filename(directory, "preprocessor.flags", NULL);
            gchar *standard = g_build_filename(bin, "..", "lib", "ivl", "include", NULL);
            gchar *flags = g_strdup_printf("D:__ICARUS__=1\nI:%s\nI:%s\nrelative include:true\n",
                                           run->include_dir, standard);
            if (g_file_set_contents(flags_file, flags, -1, &failure))
            {
                g_ptr_array_add(command, helper);
                g_ptr_array_add(command, "-F");
                g_ptr_array_add(command, flags_file);
            }
            g_free(flags);
            g_free(standard);
        }
    }
#endif
    g_free(bin);
    if (!command->len)
        for (guint i = 0; i < G_N_ELEMENTS(options); i++)
            g_ptr_array_add(command, (gpointer)options[i]);
    const gchar *list_option = "-c";
#ifdef G_OS_WIN32
    if (command->pdata[0] == helper)
        list_option = "-f";
#endif
    gchar *list_directory = failure ? NULL : g_dir_make_tmp("geany-hdl-sources-XXXXXX", &failure);
    gchar *list_path = list_directory ? g_build_filename(list_directory, "sources.list", NULL) : NULL;
    GPtrArray *sources = g_ptr_array_new();
    for (guint i = 0; run->sources[i]; ++i)
        g_ptr_array_add(sources, run->sources[i]);
    if (!failure)
        simulation_source_arguments(command, sources, list_option, list_path, &failure);
    g_ptr_array_unref(sources);
    g_ptr_array_add(command, NULL);
    GSubprocess *process =
        failure ? NULL
                : g_subprocess_launcher_spawnv(launcher, (const gchar *const *)command->pdata, &failure);
    g_ptr_array_unref(command);
    g_object_unref(launcher);
    if (process)
    {
        GBytes *output = NULL, *diagnostics = NULL;
        if (g_subprocess_communicate(process, NULL, NULL, &output, &diagnostics, &failure))
        {
            gsize length = 0;
            const gchar *bytes;
            if (g_subprocess_wait_check(process, NULL, &failure))
            {
                bytes = g_bytes_get_data(output, &length);
                expanded = g_utf8_make_valid(length ? bytes : "", length);
            }
            else
            {
                bytes = g_bytes_get_data(diagnostics, &length);
                if (length)
                    *error = g_utf8_make_valid(bytes, MIN(length, 4096));
                bytes = g_bytes_get_data(output, &length);
                if (length)
                {
                    gchar *tail =
                        g_utf8_make_valid(bytes + length - MIN(length, 1024), MIN(length, 1024));
                    const gchar *summary = g_strrstr(tail, "// Icarus preprocessor had (");
                    if (summary)
                    {
                        gchar *combined =
                            g_strdup_printf("%s%s%s", *error ? *error : "", *error ? "\n" : "", summary);
                        g_free(*error);
                        *error = combined;
                    }
                    g_free(tail);
                }
            }
        }
        g_clear_pointer(&output, g_bytes_unref);
        g_clear_pointer(&diagnostics, g_bytes_unref);
        g_object_unref(process);
    }
#ifdef G_OS_WIN32
    if (flags_file)
        g_unlink(flags_file);
    if (directory)
        g_rmdir(directory);
    g_free(flags_file);
    g_free(directory);
    g_free(helper);
#endif
    if (list_path)
        g_unlink(list_path);
    if (list_directory)
        g_rmdir(list_directory);
    g_free(list_path);
    g_free(list_directory);
    if (!expanded && !*error)
        *error = g_strdup(failure ? failure->message : "No preprocessor output.");
    g_clear_error(&failure);
    return expanded;
}

/* EN: Return the packed atom width for a port declaration prefix. Return zero
 * for an unknown type; any explicit packed range is handled by the caller.
 * RU: Возвращает ширину типа порта из prefix. Для неизвестного типа возвращает
 * ноль. Явный упакованный диапазон обрабатывается вызывающей стороной. */
static guint port_browser_type_width(const gchar *prefix)
{
    gchar **words = g_strsplit_set(prefix, " \t\r\n", -1);
    guint width = 1;
    for (guint i = 0; words[i]; i++)
    {
        const gchar *word = words[i];
        if (!*word || !strcmp(word, "input") || !strcmp(word, "output") || !strcmp(word, "inout") ||
            !strcmp(word, "ref") || !strcmp(word, "wire") || !strcmp(word, "reg") ||
            !strcmp(word, "logic") || !strcmp(word, "bit") || !strcmp(word, "tri") ||
            !strcmp(word, "var") || !strcmp(word, "signed") || !strcmp(word, "unsigned"))
            continue;
        if (!strcmp(word, "byte"))
            width = 8;
        else if (!strcmp(word, "shortint"))
            width = 16;
        else if (!strcmp(word, "integer") || !strcmp(word, "int"))
            width = 32;
        else if (!strcmp(word, "longint") || !strcmp(word, "time"))
            width = 64;
        else
        {
            width = 0;
            break;
        }
    }
    g_strfreev(words);
    return width;
}

/* EN: Extract port names and inherited directions from list into ports. With existing_only,
 * update only names declared in the module header.
 * RU: Извлекает имена и направления портов из list в ports. При existing_only обновляет
 * только имена, объявленные в заголовке модуля. */
static void port_browser_declarations(const gchar *list, GHashTable *ports, gboolean existing_only)
{
    GRegex *direction = g_regex_new("^\\s*(input|output|inout|ref)\\b", 0, 0, NULL);
    GRegex *name = g_regex_new("([A-Za-z_][A-Za-z0-9_$]*)\\s*(?:\\[[^]]*\\]\\s*)*$", 0, 0, NULL);
    PortInfo current = {.direction = "—"};
    GRegex *range = g_regex_new("\\[\\s*(-?[0-9]+)\\s*:\\s*(-?[0-9]+)\\s*\\]", 0, 0, NULL);
    const gchar *start = list;
    gint depth = 0;
    for (const gchar *p = list;; p++)
    {
        if (!*p || (*p == ',' && depth == 0))
        {
            gchar *part = g_strndup(start, p - start);
            GMatchInfo *match = NULL;
            gboolean explicit_direction = g_regex_match(direction, part, 0, &match);
            if (explicit_direction)
            {
                gchar *value = g_match_info_fetch(match, 1);
                g_strlcpy(current.direction, value, sizeof current.direction);
                g_free(value);
                current.width = 1;
                current.left = current.right = 0;
            }
            g_match_info_free(match);
            gchar *assignment = strchr(part, '=');
            if (assignment)
                *assignment = 0;
            if (g_regex_match(name, part, 0, &match))
            {
                gint start_name;
                g_match_info_fetch_pos(match, 1, &start_name, NULL);
                gchar *port = g_match_info_fetch(match, 1);
                gchar *prefix = g_strndup(part, start_name);
                if (explicit_direction && !strchr(prefix, '['))
                {
                    current.width = port_browser_type_width(prefix);
                    current.left = current.width ? (gint)current.width - 1 : 0;
                }
                if (strchr(prefix, '['))
                {
                    GMatchInfo *packed;
                    current.width = 0;
                    if (g_regex_match(range, prefix, 0, &packed) && !strchr(strchr(prefix, '[') + 1, '['))
                    {
                        gchar *left = g_match_info_fetch(packed, 1);
                        gchar *right = g_match_info_fetch(packed, 2);
                        gint64 first, last;
                        if (g_ascii_string_to_signed(left, 10, -1000000, 1000000, &first, NULL) &&
                            g_ascii_string_to_signed(right, 10, -1000000, 1000000, &last, NULL) &&
                            ABS(first - last) < 4096)
                        {
                            current.left = first;
                            current.right = last;
                            current.width = ABS(first - last) + 1;
                        }
                        g_free(left);
                        g_free(right);
                    }
                    g_match_info_free(packed);
                }
                PortInfo *previous = g_hash_table_lookup(ports, port);
                if (!existing_only || (previous && !strcmp(previous->direction, "—")))
                {
                    PortInfo *info = g_new(PortInfo, 1);
                    *info = current;
                    info->order = previous ? previous->order : g_hash_table_size(ports);
                    if (strchr(part + start_name, '['))
                        info->width = 0;
                    g_hash_table_replace(ports, port, info);
                }
                else
                    g_free(port);
                g_free(prefix);
            }
            g_match_info_free(match);
            g_free(part);
            if (!*p)
                break;
            start = p + 1;
        }
        else if (strchr("([{", *p))
            depth++;
        else if (strchr(")]}", *p))
            depth--;
    }
    g_regex_unref(range);
    g_regex_unref(direction);
    g_regex_unref(name);
}

/* EN: Find the named module in source and return its port-to-direction map. With no
 * module name, inspect the first module. The caller owns the map.
 * RU: Находит модуль module в source и возвращает таблицу направлений портов.
 * Без имени берёт первый модуль. Таблица принадлежит вызывающей стороне. */
static GHashTable *port_browser_ports(const gchar *source, const gchar *module)
{
    GHashTable *ports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (!source)
        return ports;
    GRegex *comments = g_regex_new("//[^\\n]*|/\\*[\\s\\S]*?\\*/|\"(?:\\\\.|[^\"\\\\])*\"", 0, 0, NULL);
    gchar *clean = g_regex_replace_literal(comments, source, -1, 0, " ", 0, NULL);
    g_regex_unref(comments);
    GRegex *modules =
        g_regex_new("\\bmodule\\s+(?:(?:automatic|static)\\s+)?([A-Za-z_][A-Za-z0-9_$]*)", 0, 0, NULL);
    GMatchInfo *match;
    g_regex_match(modules, clean, 0, &match);
    while (g_match_info_matches(match))
    {
        gchar *name = g_match_info_fetch(match, 1);
        gboolean selected = !module || !strcmp(name, module);
        g_free(name);
        if (selected)
        {
            gint end;
            g_match_info_fetch_pos(match, 0, NULL, &end);
            const gchar *cursor = clean + end;
            while (g_ascii_isspace(*cursor))
                cursor++;
            if (*cursor == '#')
            {
                cursor++;
                while (g_ascii_isspace(*cursor))
                    cursor++;
                if (*cursor == '(')
                {
                    guint depth = 1;
                    for (cursor++; *cursor && depth; cursor++)
                    {
                        if (*cursor == '(')
                            depth++;
                        else if (*cursor == ')')
                            depth--;
                    }
                }
            }
            while (g_ascii_isspace(*cursor))
                cursor++;
            if (*cursor == '(')
            {
                const gchar *start = ++cursor;
                guint depth = 1;
                for (; *cursor && depth; cursor++)
                {
                    if (*cursor == '(')
                        depth++;
                    else if (*cursor == ')')
                        depth--;
                }
                if (!depth)
                {
                    gchar *header = g_strndup(start, cursor - start - 1);
                    port_browser_declarations(header, ports, FALSE);
                    g_free(header);
                    GRegex *body = g_regex_new("\\b(input|output|inout|ref)\\b[^;]*;", 0, 0, NULL);
                    GMatchInfo *declaration;
                    const gchar *limit = strstr(cursor, "endmodule");
                    gchar *contents = limit ? g_strndup(cursor, limit - cursor) : g_strdup(cursor);
                    g_regex_match(body, contents, 0, &declaration);
                    while (g_match_info_matches(declaration))
                    {
                        gchar *line = g_match_info_fetch(declaration, 0);
                        line[strlen(line) - 1] = 0;
                        port_browser_declarations(line, ports, TRUE);
                        g_free(line);
                        g_match_info_next(declaration, NULL);
                    }
                    g_match_info_free(declaration);
                    g_regex_unref(body);
                    g_free(contents);
                }
            }
            break;
        }
        g_match_info_next(match, NULL);
    }
    g_match_info_free(match);
    g_regex_unref(modules);
    g_free(clean);
    return ports;
}
