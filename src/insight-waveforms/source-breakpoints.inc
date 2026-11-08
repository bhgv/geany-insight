/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Source anchors and persistent breakpoint rules.
 * RU: Привязки к исходнику и сохранение точек останова.
 */

typedef struct
{
    gchar **lines;
    gchar *digest;
    guint count;
} WavePointSource;

/* EN: Release the temporary source snapshot in data.
 * RU: Освобождает временный снимок исходника из data. */
static void simulation_point_source_free(gpointer data)
{
    WavePointSource *source = data;
    if (!source)
        return;
    g_strfreev(source->lines);
    g_free(source->digest);
    g_free(source);
}

/* EN: Read filename and build a snapshot for line anchoring. Return NULL on failure.
 * RU: Читает filename и создаёт снимок для привязки строк. При ошибке возвращает NULL. */
static WavePointSource *simulation_point_source(const gchar *filename)
{
    const gsize limit = 2 * 1024 * 1024;
    GFile *file = g_file_new_for_path(filename);
    GFileInputStream *stream = g_file_read(file, NULL, NULL);
    g_object_unref(file);
    if (!stream)
        return NULL;

    gchar *bytes = g_malloc(limit + 1);
    gsize length = 0;
    gboolean valid =
        g_input_stream_read_all(G_INPUT_STREAM(stream), bytes, limit + 1, &length, NULL, NULL) &&
        length <= limit && !memchr(bytes, 0, length) && g_utf8_validate(bytes, (gssize)length, NULL);
    g_object_unref(stream);
    if (!valid)
    {
        g_free(bytes);
        return NULL;
    }

    WavePointSource *source = g_new0(WavePointSource, 1);
    source->digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guchar *)bytes, length);
    gsize out = 0;
    for (gsize i = 0; i < length; i++)
    {
        if (bytes[i] == '\r')
        {
            bytes[out++] = '\n';
            if (i + 1 < length && bytes[i + 1] == '\n')
                i++;
        }
        else
            bytes[out++] = bytes[i];
    }
    bytes[out] = 0;
    source->lines = g_strsplit(bytes, "\n", -1);
    source->count = g_strv_length(source->lines);
    for (guint i = 0; i < source->count; i++)
        g_strstrip(source->lines[i]);
    g_free(bytes);
    return source;
}

/* EN: Build the persistent anchor for line in source.
 * RU: Создаёт сохраняемую привязку строки line в source. */
static gchar *simulation_point_anchor(const WavePointSource *source, guint line)
{
    if (!source || !line || line > source->count || !*source->lines[line - 1])
        return g_strdup("!");
    const gchar *context[] = {source->lines[line - 1], line > 1 ? source->lines[line - 2] : NULL,
                              line < source->count ? source->lines[line] : NULL};
    gchar *encoded[3];
    for (guint i = 0; i < G_N_ELEMENTS(context); i++)
    {
        if (!context[i])
            encoded[i] = g_strdup("-");
        else if (strlen(context[i]) > 1024)
            encoded[i] = g_strdup("!");
        else
            encoded[i] = g_base64_encode((const guchar *)context[i], strlen(context[i]));
    }
    gchar *anchor = g_strjoin(":", source->digest, encoded[0], encoded[1], encoded[2], NULL);
    for (guint i = 0; i < G_N_ELEMENTS(encoded); i++)
        g_free(encoded[i]);
    return anchor;
}

/* EN: Compare the encoded anchor component with source line.
 * RU: Сравнивает закодированный компонент encoded со строкой line. */
static gboolean simulation_neighbor_matches(const gchar *encoded, const gchar *line)
{
    if (!line)
        return !strcmp(encoded, "-");
    if (!strcmp(encoded, "-") || !strcmp(encoded, "!"))
        return FALSE;
    gsize length;
    guchar *text = g_base64_decode(encoded, &length);
    gboolean equal = length == strlen(line) && (!length || !memcmp(text, line, length));
    g_free(text);
    return equal;
}

/* EN: Resolve original line in source using anchor. Return zero when it cannot be resolved.
 * RU: Находит исходную строку original в source по anchor. Возвращает ноль, если найти её не удалось. */
static guint simulation_resolve_point(const WavePointSource *source, guint original, const gchar *anchor)
{
    if (!anchor || !*anchor)
        return original;
    if (!source || !strcmp(anchor, "!"))
        return 0;
    gchar **parts = g_strsplit(anchor, ":", 4);
    guint resolved = 0;
    if (g_strv_length(parts) == 4)
    {
        if (!strcmp(parts[0], source->digest) && original <= source->count)
            resolved = original;
        else if (*parts[1] && strcmp(parts[1], "!"))
        {
            gsize length;
            guchar *center = g_base64_decode(parts[1], &length);
            guint matches = 0, contexts = 0, candidate = 0, exact = 0;
            for (guint i = 0; length && i < source->count; i++)
            {
                if (strlen(source->lines[i]) != length || memcmp(center, source->lines[i], length))
                    continue;
                candidate = i + 1;
                matches++;
                if (simulation_neighbor_matches(parts[2], i ? source->lines[i - 1] : NULL) &&
                    simulation_neighbor_matches(parts[3],
                                                i + 1 < source->count ? source->lines[i + 1] : NULL))
                {
                    exact = i + 1;
                    contexts++;
                }
            }
            resolved = matches == 1 ? candidate : contexts == 1 ? exact : 0;
            g_free(center);
        }
    }
    g_strfreev(parts);
    return resolved;
}

/* EN: Restore source breakpoint rules into run from its settings file.
 * RU: Загружает точки останова в run из файла настроек. */
static void simulation_load_settings(WaveSimulation *run)
{
    if (!run->settings_path)
        return;
    GKeyFile *settings = g_key_file_new();
    if (g_key_file_load_from_file(settings, run->settings_path, G_KEY_FILE_NONE, NULL))
    {
        gsize count = 0;
        gchar **records = g_key_file_get_string_list(settings, "Simulation", "Points", &count, NULL);
        gsize anchor_count = 0;
        gchar **anchors =
            g_key_file_get_string_list(settings, "Simulation", "Anchors", &anchor_count, NULL);
        GHashTable *sources =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free, simulation_point_source_free);
        for (gsize i = 0; i < MIN(count, 128); i++)
        {
            gchar **parts = g_strsplit(records[i], "\t", 7);
            guint64 line, hits;
            if (g_strv_length(parts) == 7 &&
                g_ascii_string_to_unsigned(parts[0], 10, 1, G_MAXINT, &line, NULL) &&
                g_strv_contains((const gchar *const *)run->sources, parts[1]) &&
                !strpbrk(parts[1], "\r\n") && !strpbrk(parts[2], "\r\n ") && strlen(parts[2]) <= 2048 &&
                strlen(parts[3]) <= 1024 && ((*parts[2] == 0) == (*parts[3] == 0)) &&
                strspn(parts[3], "01xz") == strlen(parts[3]) &&
                g_ascii_string_to_unsigned(parts[4], 10, 1, 1000000, &hits, NULL) &&
                (!strcmp(parts[5], "0") || !strcmp(parts[5], "1")) &&
                (!strcmp(parts[6], "0") || !strcmp(parts[6], "1")) &&
                (strcmp(parts[5], "1") || strcmp(parts[6], "1")))
            {
                const gchar *anchor = i < anchor_count ? anchors[i] : "";
                WavePointSource *source = NULL;
                if (*anchor)
                {
                    if (!g_hash_table_contains(sources, parts[1]))
                    {
                        if (g_hash_table_size(sources) >= 4)
                            g_hash_table_remove_all(sources);
                        source = simulation_point_source(parts[1]);
                        g_hash_table_insert(sources, g_strdup(parts[1]), source);
                    }
                    else
                        source = g_hash_table_lookup(sources, parts[1]);
                }
                guint resolved = simulation_resolve_point(source, (guint)line, anchor);
                gchar *key = resolved ? g_strdup_printf("%u\t%s", resolved, parts[1]) : NULL;
                if (!key || g_hash_table_contains(run->breakpoints, key))
                {
                    g_ptr_array_add(run->unresolved_records, g_strdup(records[i]));
                    g_ptr_array_add(run->unresolved_anchors, g_strdup(anchor));
                    g_free(key);
                }
                else
                {
                    gchar *rule = g_strjoin("\t", parts[2], parts[3], parts[4], parts[5], parts[6], NULL);
                    gchar *current_anchor =
                        resolved != line ? simulation_point_anchor(source, resolved) : g_strdup(anchor);
                    if (resolved != line)
                        run->moved_points++;
                    g_hash_table_insert(run->point_anchors, g_strdup(key), current_anchor);
                    g_hash_table_insert(run->breakpoints, key, rule);
                }
            }
            g_strfreev(parts);
        }
        g_hash_table_unref(sources);
        g_strfreev(anchors);
        g_strfreev(records);
        gchar *script = g_key_file_get_string(settings, "Simulation", "Observer", NULL);
        if (script)
        {
            gchar *local = g_filename_from_utf8(script, -1, NULL, NULL, NULL);
            if (local && g_path_is_absolute(local))
                run->observer_file = g_steal_pointer(&local);
            g_free(local);
            g_free(script);
        }
    }
    g_key_file_unref(settings);
}

/* EN: Return the owned simulator breakpoint records for run.
 * RU: Возвращает новый список записей точек останова run для симулятора. */
static GPtrArray *simulation_rule_records(const WaveSimulation *run)
{
    GPtrArray *records = g_ptr_array_new_with_free_func(g_free);
    GList *keys = g_list_sort(g_hash_table_get_keys(run->breakpoints), (GCompareFunc)g_strcmp0);
    for (GList *item = keys; item; item = item->next)
    {
        const gchar *rule = g_hash_table_lookup(run->breakpoints, item->data);
        g_ptr_array_add(records, g_strconcat(item->data, "\t", rule, NULL));
    }
    g_list_free(keys);
    return records;
}

/* EN: Save the breakpoint rules of run and return whether writing succeeded.
 * RU: Сохраняет точки останова run и возвращает признак успешной записи. */
static gboolean simulation_save_settings(const WaveSimulation *run)
{
    if (!run->settings_path)
        return TRUE;
    GKeyFile *settings = g_key_file_new();
    GPtrArray *records = simulation_rule_records(run);
    GPtrArray *anchors = g_ptr_array_new();
    for (guint i = 0; i < records->len; i++)
    {
        const gchar *record = records->pdata[i];
        const gchar *tab = strchr(record, '\t');
        const gchar *end = strchr(tab + 1, '\t');
        gchar *key = g_strndup(record, end - record);
        const gchar *anchor = g_hash_table_lookup(run->point_anchors, key);
        g_ptr_array_add(anchors, (gpointer)(anchor ? anchor : ""));
        g_free(key);
    }
    for (guint i = 0; i < run->unresolved_records->len; i++)
    {
        g_ptr_array_add(records, g_strdup(run->unresolved_records->pdata[i]));
        g_ptr_array_add(anchors, run->unresolved_anchors->pdata[i]);
    }
    if (records->len)
    {
        g_key_file_set_string_list(settings, "Simulation", "Points", (const gchar *const *)records->pdata,
                                   records->len);
        g_key_file_set_string_list(settings, "Simulation", "Anchors",
                                   (const gchar *const *)anchors->pdata, anchors->len);
    }
    g_ptr_array_unref(anchors);
    if (run->observer_file)
    {
        gchar *utf8 = g_filename_to_utf8(run->observer_file, -1, NULL, NULL, NULL);
        if (utf8)
            g_key_file_set_string(settings, "Simulation", "Observer", utf8);
        g_free(utf8);
    }
    gchar *directory = g_path_get_dirname(run->settings_path);
    gboolean saved = g_mkdir_with_parents(directory, 0700) == 0 &&
                     g_key_file_save_to_file(settings, run->settings_path, NULL);
    g_free(directory);
    g_ptr_array_unref(records);
    g_key_file_unref(settings);
    return saved;
}
