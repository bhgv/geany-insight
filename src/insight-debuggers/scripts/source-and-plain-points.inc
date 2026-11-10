/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Plain breakpoints, Lua module lookup and source display.
 * RU: Обычные точки останова, поиск модулей Lua и показ исходников. */

/* Read the language's breakpoints for the active project or standalone files. */
/* Читает точки останова языка для текущего проекта или отдельных файлов. */
static void debug_load_breakpoints(ScriptDebugWindow *view)
{
    const gchar *filename = view->project_filename ? view->project_filename : view->source_filename;
    if (!filename)
        return;

    gchar *identity = debug_file_key(filename);
    gchar *scope = g_compute_checksum_for_string(G_CHECKSUM_SHA256, identity, -1);
    gchar *name = g_strdup_printf("%s-%s-%s.conf", view->python ? "python" : "lua",
                                  view->project_filename ? "project" : "file", scope);
    view->breakpoint_settings_path = g_build_filename(app->configdir, "insight", "breakpoints", name, NULL);
    g_free(name);
    g_free(scope);
    g_free(identity);

    GKeyFile *settings = g_key_file_new();
    if (g_key_file_load_from_file(settings, view->breakpoint_settings_path, G_KEY_FILE_NONE, NULL))
    {
        gchar **keys = g_key_file_get_keys(settings, "Breakpoints", NULL, NULL);
        for (guint i = 0; keys && keys[i]; i++)
        {
            gchar *filename = g_uri_unescape_string(keys[i], NULL);
            gchar *text = g_key_file_get_string(settings, "Breakpoints", keys[i], NULL);
            unsigned long lines[128];
            gsize count = 0;
            if (filename && g_path_is_absolute(filename) && text && strlen(text) <= 2048 &&
                debug_parse_breakpoints(text, lines, &count) && count)
            {
                gsize anchor_count = 0;
                gchar **anchors = g_key_file_get_string_list(settings, "Anchors", keys[i], &anchor_count, NULL);
                if (anchor_count == count)
                {
                    gchar **source_lines = debug_breakpoint_source_lines(view, filename);
                    guint source_count = source_lines ? g_strv_length(source_lines) : 0;
                    GString *resolved = g_string_new(NULL);
                    for (gsize j = 0; j < count; j++)
                    {
                        unsigned long line = debug_resolve_breakpoint(source_lines, source_count, anchors[j], lines[j]);
                        if (line)
                        {
                            if (resolved->len)
                                g_string_append(resolved, ", ");
                            g_string_append_printf(resolved, "%lu", line);
                        }
                        else
                            view->unresolved_breakpoints++;
                    }
                    g_free(text);
                    text = g_string_free(resolved, FALSE);
                    g_strfreev(source_lines);
                }
                g_strfreev(anchors);
                if (*text)
                {
                    g_hash_table_replace(view->file_breakpoints, filename, text);
                    continue;
                }
            }
            g_free(filename);
            g_free(text);
        }
        g_strfreev(keys);
    }
    g_key_file_unref(settings);
}

/* Update only the edited file so separate debugger windows can save independently. */
/* Обновляет только изменённый файл, чтобы окна отладки сохраняли настройки независимо. */
static void debug_save_breakpoints(ScriptDebugWindow *view, const gchar *filename, const gchar *text)
{
    if (!view->breakpoint_settings_path || !g_path_is_absolute(filename))
        return;

    GError *error = NULL;
    GKeyFile *settings = g_key_file_new();
    if (!g_key_file_load_from_file(settings, view->breakpoint_settings_path, G_KEY_FILE_NONE, &error) &&
        g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_clear_error(&error);

    if (!error)
    {
        gchar *key = g_uri_escape_string(filename, NULL, FALSE);
        if (text)
        {
            g_key_file_set_string(settings, "Breakpoints", key, text);
            unsigned long lines[128];
            gsize count = 0;
            if (debug_parse_breakpoints(text, lines, &count))
            {
                gchar **source_lines = debug_breakpoint_source_lines(view, filename);
                guint source_count = source_lines ? g_strv_length(source_lines) : 0;
                gchar *anchors[128];
                for (gsize i = 0; i < count; i++)
                    anchors[i] = debug_breakpoint_anchor(source_lines, source_count, lines[i]);
                g_key_file_set_string_list(settings, "Anchors", key, (const gchar *const *)anchors, count);
                for (gsize i = 0; i < count; i++)
                    g_free(anchors[i]);
                g_strfreev(source_lines);
            }
        }
        else
        {
            g_key_file_remove_key(settings, "Breakpoints", key, NULL);
            g_key_file_remove_key(settings, "Anchors", key, NULL);
        }
        g_free(key);

        gchar *directory = g_path_get_dirname(view->breakpoint_settings_path);
        if (g_mkdir_with_parents(directory, 0700) == 0)
            g_key_file_save_to_file(settings, view->breakpoint_settings_path, &error);
        else
            g_set_error_literal(&error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                "Cannot create debugger settings directory");
        g_free(directory);
    }
    if (error)
        ui_set_statusbar(TRUE, "Cannot save debugger breakpoints: %s", error->message);
    g_clear_error(&error);
    g_key_file_unref(settings);
}

/* Store valid breakpoint text and persist changes for files with real paths. */
/* Хранит корректные точки останова и сохраняет изменения для файлов с путём. */
static void debug_store_file_breakpoints(ScriptDebugWindow *view, const gchar *filename, const gchar *text)
{
    unsigned long lines[128];
    gsize count = 0;
    if (!debug_parse_breakpoints(text, lines, &count))
        return;

    gchar *key = debug_file_key(filename);
    const gchar *value = count ? text : NULL;
    if (g_strcmp0(g_hash_table_lookup(view->file_breakpoints, key), value))
    {
        debug_save_breakpoints(view, key, value);
        if (value)
        {
            g_hash_table_replace(view->file_breakpoints, key, g_strdup(value));
            return;
        }
        g_hash_table_remove(view->file_breakpoints, key);
    }
    g_free(key);
}

/* Compare file identity using the same normalization as breakpoint storage. */
/* Сравнивает файлы по тем же правилам, что и список точек останова. */
static gboolean debug_same_file(const gchar *first, const gchar *second)
{
    gchar *first_key = debug_file_key(first);
    gchar *second_key = debug_file_key(second);
    gboolean same = !strcmp(first_key, second_key);
    g_free(first_key);
    g_free(second_key);
    return same;
}



/* Release a loaded module snapshot. */
/* Освобождает снимок загруженного модуля. */
static void debug_lua_module_free(gpointer data)
{
    DebugLuaModule *module = data;
    g_free(module->filename);
    g_free(module->source);
    g_free(module);
}

/* Read a bounded UTF-8 source file for display or module loading. */
/* Читает ограниченный по размеру исходник UTF-8 для показа или загрузки модуля. */
static gchar *debug_read_source_file(const gchar *filename, gsize *length)
{
    GFile *file = g_file_new_for_path(filename);
    GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                      G_FILE_QUERY_INFO_NONE, NULL, NULL);
    gchar *source = NULL;
    if (info && g_file_info_get_file_type(info) == G_FILE_TYPE_REGULAR &&
        g_file_info_get_size(info) <= 2 * 1024 * 1024)
    {
        if (!g_file_load_contents(file, NULL, &source, length, NULL, NULL) ||
            *length > 2 * 1024 * 1024 || memchr(source, 0, *length) ||
            !g_utf8_validate(source, *length, NULL))
            g_clear_pointer(&source, g_free);
    }
    g_clear_object(&info);
    g_object_unref(file);
    return source;
}

/* Locate a Lua module under the project base directory and snapshot its text. */
/* Ищет модуль Lua в корне проекта и сохраняет снимок его текста. */
static int debug_lua_resolve_module(void *context, const char *name, const char **filename,
                                    const char **source, size_t *length)
{
    ScriptDebugWindow *view = context;
    if (!view->project_root || !*name)
        return 0;

    GString *relative = g_string_new(NULL);
    gboolean component = FALSE;
    for (const gchar *character = name; *character; character++)
    {
        if (*character == '.' && component)
        {
            g_string_append_c(relative, G_DIR_SEPARATOR);
            component = FALSE;
        }
        else if (g_ascii_isalnum(*character) || *character == '_' || *character == '-')
        {
            g_string_append_c(relative, *character);
            component = TRUE;
        }
        else
        {
            g_string_free(relative, TRUE);
            return 0;
        }
    }
    if (!component)
    {
        g_string_free(relative, TRUE);
        return 0;
    }

    gchar *flat_name = g_strconcat(relative->str, ".lua", NULL);
    gchar *directory_name = g_build_filename(relative->str, "init.lua", NULL);
    const gchar *candidates[] = {flat_name, directory_name};
    DebugLuaModule *module = NULL;
    for (guint i = 0; i < G_N_ELEMENTS(candidates) && !module; i++)
    {
        gchar *path = g_build_filename(view->project_root, candidates[i], NULL);
        gchar *key = debug_file_key(path);
        module = g_hash_table_lookup(view->lua_modules, key);
        if (!module && g_hash_table_size(view->lua_modules) < 64)
        {
            gsize module_length = 0;
            gchar *text = NULL;
            GeanyDocument *document = document_find_by_filename(path);
            if (DOC_VALID(document))
            {
                gint size = sci_get_length(document->editor->sci);
                if (size >= 0 && size <= 2 * 1024 * 1024)
                {
                    module_length = (gsize)size;
                    text = sci_get_contents(document->editor->sci, size + 1);
                    if (memchr(text, 0, module_length) || !g_utf8_validate(text, module_length, NULL))
                        g_clear_pointer(&text, g_free);
                }
            }
            else
                text = debug_read_source_file(path, &module_length);

            if (text && view->lua_module_bytes + module_length <= 16 * 1024 * 1024)
            {
                module = g_new0(DebugLuaModule, 1);
                module->filename = g_strdup(key);
                module->source = text;
                module->length = module_length;
                view->lua_module_bytes += module_length;
                g_hash_table_insert(view->lua_modules, g_strdup(key), module);
            }
            else
                g_free(text);
        }
        g_free(key);
        g_free(path);
    }
    g_free(flat_name);
    g_free(directory_name);
    g_string_free(relative, TRUE);
    if (!module)
        return 0;

    *filename = module->filename;
    *source = module->source;
    *length = module->length;
    return 1;
}

/* Display the source file containing the current frame. */
/* Показывает файл, в котором находится текущий кадр. */
static gboolean debug_show_script_file(ScriptDebugWindow *view, const gchar *filename)
{
    if (!filename)
        return FALSE;
    if (view->display_filename && debug_same_file(view->display_filename, filename))
        return view->display_source != NULL;

    gchar *source = NULL;
    gsize length = 0;
    if (debug_same_file(view->source_filename, filename))
    {
        source = g_strdup(view->source_snapshot);
        length = view->source_length;
    }
    else if (!view->python)
    {
        gchar *key = debug_file_key(filename);
        DebugLuaModule *module = g_hash_table_lookup(view->lua_modules, key);
        if (module)
        {
            source = g_strdup(module->source);
            length = module->length;
        }
        g_free(key);
    }
    else if (g_path_is_absolute(filename))
    {
        gchar *key = debug_file_key(filename);
        const gchar *overlay = view->python_overlays ? g_hash_table_lookup(view->python_overlays, key) : NULL;
        if (overlay)
        {
            source = g_strdup(overlay);
            length = strlen(overlay);
        }
        else
            source = debug_read_source_file(filename, &length);
        g_free(key);
    }

    g_free(view->display_filename);
    g_free(view->display_source);
    g_clear_pointer(&view->source_code_mask, g_free);
    g_array_set_size(view->source_line_offsets, 0);
    view->display_filename = g_strdup(filename);
    view->display_source = source;
    view->display_length = source ? length : 0;
    if (source)
    {
        view->source_code_mask = insight_source_code_mask(source, length,
            view->python ? INSIGHT_CODE_PYTHON : INSIGHT_CODE_LUA, NULL);
        gchar *numbered = debug_number_source(source, length, view->source_line_offsets, view->python);
        debug_set_text(view->source_view, numbered);
        g_free(numbered);
    }
    else
        debug_set_text(view->source_view, "Source unavailable or exceeds 2 MiB.");

    gchar *key = debug_file_key(filename);
    const gchar *breakpoints = g_hash_table_lookup(view->file_breakpoints, key);
    gtk_entry_set_text(GTK_ENTRY(view->breakpoints), breakpoints ? breakpoints : "");
    debug_mark_breakpoints(GTK_EDITABLE(view->breakpoints), view);
    g_free(key);
    return source != NULL;
}
