/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Define and validate bounded per-project/per-executable debugger settings.
 * RU: Определяет и проверяет ограниченные настройки отладчика для проекта и программы. */

static const gchar *native_profile_names[] = {
    "host",          "user",         "identity", "ssh-config", "server-host",      "remote-executable",
    "remote-source", "local-source", "sysroot",  "arguments",  "working-directory"};

/* EN: Validate required fields and optional SSH setup values in profile before changing widgets; return FALSE
 * and error on invalid data. RU: Проверяет обязательные поля и необязательные параметры SSH в profile до
 * изменения формы; ошибка даёт FALSE и error. */
static gboolean native_profile_validate(GKeyFile *profile, GError **error)
{
    const gchar *integer_names[] = {"version", "mode", "ssh-port", "server-port"};
    const gint minimum[] = {1, 0, 0, 1};
    const gint maximum[] = {4, 2, 65535, 65535};
    for (guint i = 0; i < G_N_ELEMENTS(integer_names); i++)
    {
        GError *failure = NULL;
        gint value = g_key_file_get_integer(profile, "debugger", integer_names[i], &failure);
        if (failure || value < minimum[i] || value > maximum[i])
        {
            g_clear_error(&failure);
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                insight_tr("Invalid debugger profile version, mode or port."));
            return FALSE;
        }
    }
    for (guint i = 0; i < G_N_ELEMENTS(native_profile_names); i++)
    {
        gchar *value = g_key_file_get_string(profile, "debugger", native_profile_names[i], NULL);
        glong limit = !strcmp(native_profile_names[i], "arguments") ? 8192 : 4096;
        gboolean valid = value && strlen(value) <= (gsize)limit * 4 && g_utf8_validate(value, -1, NULL) &&
                         g_utf8_strlen(value, -1) <= limit && !strpbrk(value, "\r\n");
        g_free(value);
        if (!valid)
        {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                insight_tr("Invalid or missing debugger profile field."));
            return FALSE;
        }
    }
    const gchar *options[] = {"ssh-remember", "ssh-start-server", "stop-at-main"};
    for (guint i = 0; i < G_N_ELEMENTS(options); i++)
        if (g_key_file_has_key(profile, "debugger", options[i], NULL))
        {
            GError *failure = NULL;
            g_key_file_get_boolean(profile, "debugger", options[i], &failure);
            if (failure)
            {
                g_clear_error(&failure);
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                    insight_tr("Invalid launch or SSH options in debugger profile."));
                return FALSE;
            }
        }
    if (g_key_file_has_key(profile, "debugger", "ssh-directory", NULL))
    {
        gchar *value = g_key_file_get_string(profile, "debugger", "ssh-directory", NULL);
        gboolean valid = value && strlen(value) <= 4096 && g_utf8_validate(value, -1, NULL) &&
                         !strpbrk(value, "\r\n") && (!*value || *value == '/');
        g_free(value);
        if (!valid)
        {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                insight_tr("Invalid remote working directory in debugger profile."));
            return FALSE;
        }
    }
    return g_key_file_get_integer(profile, "debugger", "version", NULL) < 2 ||
           native_profile_debug_validate(profile, error);
}

/* EN: Build a private settings filename from directory, project and target; all returned bytes are owned.
 * RU: Создаёт имя личных настроек по directory, project и target; результат принадлежит вызывающему. */
static gchar *native_profile_path(const gchar *directory, const gchar *project, const gchar *target)
{
    const gchar *scope = project ? project : "";
    gchar *identity = g_strdup_printf("%" G_GSIZE_FORMAT ":%s%s", strlen(scope), scope, target);
    gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, identity, -1);
    gchar *name = g_strconcat(hash, ".ini", NULL);
    gchar *path = g_build_filename(directory, "insight", "gdb-profiles", name, NULL);
    g_free(name);
    g_free(hash);
    g_free(identity);
    return path;
}
