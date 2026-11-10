/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Breakpoint identity, anchors and conditional rule storage.
 * RU: Идентификаторы, привязки и хранение условных точек останова. */

/* Free the condition and source identity stored for one point. */
/* Освобождает условие и имя исходника одной точки. */
static void debug_conditional_point_free(gpointer data)
{
    DebugConditionalPoint *point = data;
    g_free(point->filename);
    g_free(point->expression);
    g_free(point->saved_group);
    g_free(point);
}

/* Build an unambiguous key from a normalized filename and a line number. */
/* Создаёт однозначный ключ из нормализованного имени файла и номера строки. */
static gchar *debug_conditional_point_key(const gchar *filename, unsigned long line)
{
    return g_strdup_printf("%lu:%s", line, filename);
}

/* Suppress a plain breakpoint when a conditional point overrides that line. */
/* Пропускает обычную точку, если на этой строке есть условная точка. */
static gboolean debug_has_conditional_point(const ScriptDebugWindow *view, const gchar *filename,
                                            unsigned long line)
{
    InsightLuaDebug *observer = view->python ? view->python_observer : view->engine;
    gchar *key = debug_conditional_point_key(filename, line);
    const DebugConditionalPoint *point = g_hash_table_lookup(view->conditional_points, key);
    gboolean found = g_hash_table_contains(view->disabled_plain_points, key) ||
        (point && (point->disabled || insight_lua_debug_observer_active(observer)));
    g_free(key);
    return found;
}

typedef struct {
    gchar *filename;
    gchar *source;
    gsize length;
} DebugLuaModule;

static gchar *debug_read_source_file(const gchar *filename, gsize *length);

/* Use the same captured source as the debugger when anchoring a file. */
/* Использует снимок отладчика для привязки точки к исходнику. */
static gchar **debug_breakpoint_source_lines(const ScriptDebugWindow *view, const gchar *filename)
{
    const gchar *source = NULL;
    gchar *owned = NULL;
    gchar *display_key = debug_file_key(view->display_filename);
    if (!strcmp(filename, display_key) && view->display_source)
        source = view->display_source;
    else if (view->python_overlays)
        source = g_hash_table_lookup(view->python_overlays, filename);
    g_free(display_key);
    if (!source && !view->python)
    {
        const DebugLuaModule *module = g_hash_table_lookup(view->lua_modules, filename);
        GeanyDocument *document = module ? NULL : document_find_by_filename(filename);
        if (module)
            source = module->source;
        else if (DOC_VALID(document))
        {
            gint size = sci_get_length(document->editor->sci);
            if (size >= 0 && size <= 2 * 1024 * 1024)
            {
                owned = sci_get_contents(document->editor->sci, size + 1);
                if (memchr(owned, 0, size) || !g_utf8_validate(owned, size, NULL))
                    g_clear_pointer(&owned, g_free);
                source = owned;
            }
        }
    }
    if (!source)
    {
        gchar *local = utils_get_locale_from_utf8(filename);
        gsize length = 0;
        if (local)
            owned = debug_read_source_file(local, &length);
        g_free(local);
        source = owned;
    }
    if (!source)
        return NULL;

    GString *normalized = g_string_new(NULL);
    for (const gchar *byte = source; *byte; byte++)
    {
        if (*byte == '\r')
        {
            g_string_append_c(normalized, '\n');
            if (byte[1] == '\n')
                byte++;
        }
        else
            g_string_append_c(normalized, *byte);
    }
    gchar **lines = g_strsplit(normalized->str, "\n", -1);
    for (guint i = 0; lines[i]; i++)
        g_strstrip(lines[i]);
    g_string_free(normalized, TRUE);
    g_free(owned);
    return lines;
}

/* Fingerprint the line and its immediate neighbors. */
/* Вычисляет отпечаток строки и двух соседних строк. */
static gchar *debug_breakpoint_anchor(gchar **lines, guint count, unsigned long line)
{
    if (!line || line > count || !*lines[line - 1])
        return g_strdup("!");
    gchar *center = g_compute_checksum_for_string(G_CHECKSUM_SHA256, lines[line - 1], -1);
    gchar *before = line > 1 ? g_compute_checksum_for_string(G_CHECKSUM_SHA256, lines[line - 2], -1) : g_strdup("-");
    gchar *after = line < count ? g_compute_checksum_for_string(G_CHECKSUM_SHA256, lines[line], -1) : g_strdup("-");
    gchar *anchor = g_strjoin(":", center, before, after, NULL);
    g_free(center);
    g_free(before);
    g_free(after);
    return anchor;
}

/* Resolve only a unique text match or a unique full context match. */
/* Переносит точку только при единственном совпадении текста или контекста. */
static unsigned long debug_resolve_breakpoint(gchar **lines, guint count, const gchar *anchor,
                                              unsigned long fallback)
{
    if (!anchor || !*anchor)
        return fallback;
    if (!lines || strlen(anchor) < 65 || anchor[64] != ':')
        return 0;

    unsigned matches = 0, contexts = 0;
    unsigned long match = 0, context = 0;
    for (guint i = 0; i < count; i++)
    {
        if (!*lines[i])
            continue;
        gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, lines[i], -1);
        gboolean equal = !strncmp(hash, anchor, 64);
        g_free(hash);
        if (!equal)
            continue;
        matches++;
        match = i + 1;
        gchar *candidate = debug_breakpoint_anchor(lines, count, match);
        if (!strcmp(candidate, anchor))
        {
            contexts++;
            context = match;
        }
        g_free(candidate);
    }
    if (matches == 1)
        return match;
    return contexts == 1 ? context : 0;
}

/* Locate the saved conditional rules for the same scope as ordinary points. */
/* Находит сохранённые правила в той же области, что и обычные точки. */
static gchar *debug_conditional_settings_path(const ScriptDebugWindow *view)
{
    if (!view->breakpoint_settings_path)
        return NULL;
    gchar *name = g_path_get_basename(view->breakpoint_settings_path);
    gchar *path = g_build_filename(app->configdir, "insight", "conditional-breakpoints", name, NULL);
    g_free(name);
    return path;
}

/* Restore bounded rule data. Counters are created by the observer at runtime. */
/* Восстанавливает правила с проверкой границ. Счётчики создаёт обработчик при запуске. */
static void debug_read_conditional_points(const ScriptDebugWindow *view, GKeyFile *settings,
                                          GHashTable *points, guint *unresolved)
{
    gsize count;
    gchar **groups = g_key_file_get_groups(settings, &count);
    for (gsize i = 0; i < count; i++)
    {
        const gchar *group = groups[i];
        if (!strcmp(group, "RuleSet") || !strcmp(group, "Observer"))
            continue;
        if (g_hash_table_size(points) >= 32)
        {
            (*unresolved)++;
            continue;
        }
        gchar *filename = g_key_file_get_string(settings, group, "File", NULL);
        gchar *expression = g_key_file_get_string(settings, group, "Expression", NULL);
        gint line = g_key_file_get_integer(settings, group, "Line", NULL);
        gint minimum_hits = g_key_file_get_integer(settings, group, "Hits", NULL);
        if (filename && g_path_is_absolute(filename) && expression && *expression &&
            g_utf8_strlen(expression, -1) <= 1024 && line > 0 &&
            minimum_hits > 0 && minimum_hits <= 1000000)
        {
            DebugConditionalPoint *point = g_new0(DebugConditionalPoint, 1);
            point->filename = debug_file_key(filename);
            point->expression = g_steal_pointer(&expression);
            point->saved_group = g_strdup(group);
            gchar *anchor = g_key_file_get_string(settings, group, "Anchor", NULL);
            gchar **lines = anchor && *anchor ? debug_breakpoint_source_lines(view, point->filename) : NULL;
            point->line = debug_resolve_breakpoint(lines, lines ? g_strv_length(lines) : 0,
                                                   anchor, (unsigned long)line);
            g_strfreev(lines);
            g_free(anchor);
            point->minimum_hits = (guint)minimum_hits;
            point->log_only = g_key_file_get_boolean(settings, group, "LogOnly", NULL);
            point->disabled = g_key_file_get_boolean(settings, group, "Disabled", NULL);
            gchar *key = point->line ? debug_conditional_point_key(point->filename, point->line) : NULL;
            if (!key || g_hash_table_contains(points, key))
            {
                (*unresolved)++;
                g_free(key);
                debug_conditional_point_free(point);
            }
            else
                g_hash_table_insert(points, key, point);
        }
        else
            (*unresolved)++;
        g_free(expression);
        g_free(filename);
    }
    g_strfreev(groups);

}

/* Load the project's conditional point settings. */
/* Загружает настройки условных точек проекта. */
static void debug_load_conditional_points(ScriptDebugWindow *view)
{
    gchar *path = debug_conditional_settings_path(view);
    if (!path)
        return;
    GKeyFile *settings = g_key_file_new();
    if (g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, NULL))
        debug_read_conditional_points(view, settings, view->conditional_points, &view->unresolved_breakpoints);
    g_key_file_unref(settings);
    g_free(path);
}

/* Serialize one rule with its current source anchor. */
/* Записывает правило с его текущей привязкой к исходнику. */
static void debug_write_conditional_point(const ScriptDebugWindow *view, GKeyFile *settings,
                                          const gchar *group, const DebugConditionalPoint *point)
{
    g_key_file_set_string(settings, group, "File", point->filename);
    g_key_file_set_string(settings, group, "Expression", point->expression);
    g_key_file_set_integer(settings, group, "Line", (gint)point->line);
    g_key_file_set_integer(settings, group, "Hits", (gint)point->minimum_hits);
    g_key_file_set_boolean(settings, group, "LogOnly", point->log_only);
    g_key_file_set_boolean(settings, group, "Disabled", point->disabled);
    gchar **lines = debug_breakpoint_source_lines(view, point->filename);
    gchar *anchor = debug_breakpoint_anchor(lines, lines ? g_strv_length(lines) : 0, point->line);
    g_key_file_set_string(settings, group, "Anchor", anchor);
    g_free(anchor);
    g_strfreev(lines);

}

/* Serialize a complete rule set for versioning or explicit replacement. */
/* Записывает весь набор правил для истории или явной замены. */
static void debug_write_conditional_set(const ScriptDebugWindow *view, GKeyFile *settings,
                                        GHashTable *points)
{
    g_key_file_set_boolean(settings, "RuleSet", "Present", TRUE);
    GList *keys = g_list_sort(g_hash_table_get_keys(points), (GCompareFunc)g_strcmp0);
    for (GList *item = keys; item; item = item->next)
    {
        const DebugConditionalPoint *point = g_hash_table_lookup(points, item->data);
        if (g_path_is_absolute(point->filename) && point->saved_group)
            debug_write_conditional_point(view, settings, point->saved_group, point);
    }
    g_list_free(keys);
}

/* Replace persisted rules only when applying a complete historical draft. */
/* Заменяет сохранённые правила только при применении полного черновика из истории. */
static void debug_save_conditional_set(const ScriptDebugWindow *view)
{
    gchar *path = debug_conditional_settings_path(view);
    if (!path)
        return;
    GKeyFile *settings = g_key_file_new();
    debug_write_conditional_set(view, settings, view->conditional_points);
    gchar *directory = g_path_get_dirname(path);
    GError *error = NULL;
    if (g_mkdir_with_parents(directory, 0700) == 0)
        g_key_file_save_to_file(settings, path, &error);
    else
        g_set_error_literal(&error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                            "Cannot create conditional breakpoint settings directory");
    if (error)
        ui_set_statusbar(TRUE, "Cannot save conditional rules: %s", error->message);
    g_clear_error(&error);
    g_free(directory);
    g_key_file_unref(settings);
    g_free(path);
}

/* Update the saved rule for one line, retaining other windows' edits. */
/* Обновляет сохранённое правило одной строки, сохраняя правки других окон. */
static void debug_save_conditional_point(const ScriptDebugWindow *view, const gchar *key,
                                         const gchar *group)
{
    const gchar *separator = strchr(key, ':');
    if (!separator || !g_path_is_absolute(separator + 1))
        return;
    gchar *path = debug_conditional_settings_path(view);
    if (!path)
        return;

    GError *error = NULL;
    GKeyFile *settings = g_key_file_new();
    if (!g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, &error) &&
        g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_clear_error(&error);
    if (!error)
    {
        const DebugConditionalPoint *point = g_hash_table_lookup(view->conditional_points, key);
        if (point)
            debug_write_conditional_point(view, settings, group, point);
        else
            g_key_file_remove_group(settings, group, NULL);

        gchar *directory = g_path_get_dirname(path);
        if (g_mkdir_with_parents(directory, 0700) == 0)
            g_key_file_save_to_file(settings, path, &error);
        else
            g_set_error_literal(&error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                                "Cannot create conditional breakpoint settings directory");
        g_free(directory);
    }
    if (error)
        ui_set_statusbar(TRUE, "Cannot save conditional breakpoint: %s", error->message);
    g_clear_error(&error);
    g_key_file_unref(settings);
    g_free(path);
}
