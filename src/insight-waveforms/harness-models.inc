/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: External models, image staging and memory presets.
 * RU: Внешние модели, подготовка образов и настройки памяти.
 */

typedef struct
{
    gchar *module;
    GString *pins, *parameters;
    GHashTable *pin_names, *parameter_names;
} SimulationModel;

/* EN: Free the SimulationModel and its owned declarations from data.
 * RU: Освобождает SimulationModel из data и его объявления. */
static void simulation_model_free(gpointer data)
{
    SimulationModel *model = data;
    g_free(model->module);
    g_string_free(model->pins, TRUE);
    g_string_free(model->parameters, TRUE);
    g_hash_table_unref(model->pin_names);
    g_hash_table_unref(model->parameter_names);
    g_free(model);
}

/* EN: Return whether expression is accepted for an external-model binding.
 * RU: Проверяет, подходит ли expression для подключения внешней модели. */
static gboolean simulation_port_expression(const gchar *expression)
{
    return strlen(expression) <= 256 &&
           g_regex_match_simple("^[A-Za-z0-9_$'+*/%&|^~!=<>?:(). -]+$", expression, 0, 0) &&
           !strstr(expression, "//") && !strstr(expression, "/*");
}

/* EN: Resolve filename against directory and add the source to sources. Return false if it is invalid.
 * RU: Разрешает filename относительно directory и добавляет в sources. При ошибке возвращает false. */
static gboolean simulation_model_source(GPtrArray *sources, const gchar *directory, const gchar *filename)
{
    gchar *native = g_filename_from_utf8(filename, -1, NULL, NULL, NULL);
    if (!native)
        return FALSE;
    gchar *path = insight_canonicalize_filename(native, directory);
    g_free(native);
    const gchar *extension = path ? strrchr(path, '.') : NULL;
    if (!extension || (g_ascii_strcasecmp(extension, ".v") && g_ascii_strcasecmp(extension, ".sv")) ||
        !g_file_test(path, G_FILE_TEST_IS_REGULAR))
    {
        g_free(path);
        return FALSE;
    }
    for (guint i = 0; i < sources->len; i++)
        if (!strcmp(sources->pdata[i], path))
        {
            g_free(path);
            return TRUE;
        }
    if (sources->len >= 32)
    {
        g_free(path);
        return FALSE;
    }
    g_ptr_array_add(sources, path);
    return TRUE;
}

/* EN: Add a unique name and expression to HDL entries and register the name in names.
 * RU: Добавляет уникальное имя name и expression в HDL entries и регистрирует имя в names. */
static void simulation_model_binding(GString *entries, GHashTable *names, const gchar *name,
                                     const gchar *expression)
{
    if (entries->len)
        g_string_append_c(entries, ',');
    g_string_append_printf(entries, ".%s(%s)", name, expression);
    g_hash_table_add(names, g_strdup(name));
}

/* EN: Apply count recipe args to models and sources. Resolve files in directory and emit shared ports in body and connections using ports.
 * RU: Применяет count аргументов args к models и sources. Разрешает файлы в directory и создаёт общие выводы в body и connections через ports. */
static gboolean simulation_model_rule(gchar **args, gint count, GHashTable *models, GPtrArray *sources,
                                      const gchar *directory, GString *body, GString *connections,
                                      GHashTable *ports)
{
    const gchar *rule = args[0];
    if (!strcmp(rule, "model-source"))
        return count == 2 && simulation_model_source(sources, directory, args[1]);
    if (count < 2)
        return FALSE;
    SimulationModel *model = g_hash_table_lookup(models, args[1]);
    if (!strcmp(rule, "model"))
    {
        if (count != 4 || model || g_hash_table_size(models) >= 8 || !simulation_port_name(args[1]) ||
            !simulation_port_name(args[2]) || !simulation_model_source(sources, directory, args[3]))
            return FALSE;
        model = g_new0(SimulationModel, 1);
        model->module = g_strdup(args[2]);
        model->pins = g_string_new(NULL);
        model->parameters = g_string_new(NULL);
        model->pin_names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        model->parameter_names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        g_hash_table_insert(models, g_strdup(args[1]), model);
        return TRUE;
    }
    if (!model || count < 4 || !simulation_port_name(args[2]))
        return FALSE;
    if (!strcmp(rule, "model-param") || !strcmp(rule, "model-value"))
    {
        gboolean parameter = !strcmp(rule, "model-param");
        GHashTable *names = parameter ? model->parameter_names : model->pin_names;
        if (count != 4 || g_hash_table_contains(names, args[2]) || !simulation_port_expression(args[3]))
            return FALSE;
        simulation_model_binding(parameter ? model->parameters : model->pins, names, args[2], args[3]);
        return TRUE;
    }
    if (!simulation_port_name(args[3]) || g_hash_table_contains(model->pin_names, args[3]))
        return FALSE;
    gpointer existing = g_hash_table_lookup(ports, args[2]);
    guint id;
    if (!strcmp(rule, "model-share") && count == 4 && existing)
        id = GPOINTER_TO_UINT(existing) - 1;
    else if (!strcmp(rule, "model-link") && count == 5 && !existing && g_hash_table_size(ports) < 128)
    {
        guint64 width;
        if (!g_ascii_string_to_unsigned(args[4], 10, 1, 4096, &width, NULL))
            return FALSE;
        id = simulation_port_connect(body, connections, ports, args[2], width, TRUE);
    }
    else
        return FALSE;
    gchar *signal = g_strdup_printf("pin%u", id);
    simulation_model_binding(model->pins, model->pin_names, args[3], signal);
    g_free(signal);
    return TRUE;
}

/* EN: Stage filename from directory in images under anchor. Report read or size errors in error.
 * RU: Добавляет файл filename из directory в images под ключом anchor. Ошибки чтения и размера записывает в error. */
static gboolean simulation_memory_image(GHashTable *images, const gchar *anchor, const gchar *filename,
                                        const gchar *directory, GError **error)
{
    if (!simulation_port_name(*anchor == ':' ? anchor + 1 : anchor) ||
        g_hash_table_contains(images, anchor))
        return FALSE;
    gchar *native = g_filename_from_utf8(filename, -1, NULL, NULL, error);
    if (!native)
        return FALSE;
    gchar *path = g_canonicalize_filename(native, directory);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Device file must be a regular file: %s",
                    filename);
        g_free(path);
        g_free(native);
        return FALSE;
    }
    GFile *file = g_file_new_for_path(path);
    GFileInputStream *stream = g_file_read(file, NULL, error);
    gboolean valid = FALSE;
    if (stream)
    {
        const gsize limit = 1024 * 1024;
        gchar *data = g_malloc(limit + 1);
        gsize length = 0;
        if (g_input_stream_read_all(G_INPUT_STREAM(stream), data, limit + 1, &length, NULL, error))
        {
            if (length <= limit && !memchr(data, 0, length))
            {
                g_hash_table_insert(images, g_strdup(anchor), g_bytes_new_take(data, length));
                data = NULL;
                valid = TRUE;
            }
            else
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                    "Use a text device file of at most 1 MiB.");
        }
        g_free(data);
        g_object_unref(stream);
    }
    g_object_unref(file);
    g_free(path);
    g_free(native);
    return valid;
}

/* EN: Expand recognized memory presets in arguments and update count.
 * RU: Подставляет значения по умолчанию для памяти в arguments и обновляет count. */
static void simulation_memory_defaults(gchar ***arguments, gint *count)
{
    static const struct
    {
        const gchar *name, *defaults;
        gint pins;
    } presets[] = {
        {"parallel-psram", "addr dq ce_n we_n oe_n lb_n ub_n 20", 7},
        {"sram", "addr dq ce_n we_n oe_n 16 8", 5},
        {"sdram", "ck cke cs_n ras_n cas_n we_n a ba dq dqm", 10},
        {"ddr1", "ck cke cs_n ras_n cas_n we_n a ba dq dqs dm 2 1", 11},
        {"ddr2", "ck cke cs_n ras_n cas_n we_n a ba dq dqs dm 3 2", 11},
        {"ddr3", "ck reset_n cke cs_n ras_n cas_n we_n a ba dq dqs dm 3 2", 12},
        {"ddr4", "ck reset_n cke cs_n act_n a ba bg dq dqs dm_n 3 2", 11},
        {"ddr5", "ck reset_n cs_n ca dq dqs dm_n 3 2", 7},
        {"spi-nor", "sck cs_n mosi miso", 4},
        {"spi-nand", "sck cs_n mosi miso", 4},
        {"spi-eeprom", "sck cs_n mosi miso", 4},
        {"spi-psram", "sck cs_n mosi miso", 4},
        {"i2c-eeprom", "scl sda 0x50", 2},
    };
    gchar **args = *arguments;
    for (guint i = 0; i < G_N_ELEMENTS(presets); i++)
    {
        if (strcmp(args[0], presets[i].name))
            continue;
        gchar **defaults = g_strsplit(presets[i].defaults, " ", -1);
        gint expected = 1 + (gint)g_strv_length(defaults);
        if (*count == 1 || (*count > presets[i].pins && *count <= expected))
        {
            gchar **expanded = g_new0(gchar *, expected + 1);
            expanded[0] = g_strdup(args[0]);
            for (gint j = 1; j < expected; j++)
                expanded[j] = g_strdup(j < *count && strcmp(args[j], "-") ? args[j] : defaults[j - 1]);
            g_strfreev(args);
            *arguments = expanded;
            *count = expected;
        }
        g_strfreev(defaults);
        return;
    }
}
