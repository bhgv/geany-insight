/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Validate and match source-based C++ sub-location settings across GDB sessions.
 * RU: Проверяет и сопоставляет настройки подпозиций C++ по исходникам между сеансами GDB. */

/* EN: Release selector data and its owned source/function strings.
 * RU: Освобождает селектор data и принадлежащие ему строки файла/функции. */
static void native_saved_location_free(gpointer data)
{
    NativeSavedLocation *location = data;
    g_free(location->file);
    g_free(location->function);
    g_free(location);
}

/* EN: Decode stable source coordinates from tuple; return an owned selector or NULL without usable debug information.
 * RU: Читает устойчивые координаты tuple; возвращает селектор либо NULL без пригодной отладочной информации. */
static NativeSavedLocation *native_profile_location_parse(const gchar *tuple)
{
    gchar *file = native_debug_field(tuple, "file");
    if (!file)
        file = native_debug_field(tuple, "fullname");
    gchar *function = native_debug_field(tuple, "func");
    gchar *line = native_debug_field(tuple, "line");
    guint64 number = 0;
    gboolean valid = native_profile_text_valid(file, 4096) && *file &&
        native_profile_text_valid(function, 4096) && *function && line &&
        g_ascii_string_to_unsigned(line, 10, 1, G_MAXINT, &number, NULL);
    NativeSavedLocation *location = NULL;
    if (valid)
    {
        location = g_new0(NativeSavedLocation, 1);
        location->file = g_steal_pointer(&file);
        location->function = g_steal_pointer(&function);
        location->line = (guint)number;
    }
    g_free(line);
    g_free(function);
    g_free(file);
    return location;
}

/* EN: Return an owned collision-free key for parent and location; no runtime addresses are included.
 * RU: Возвращает однозначный ключ parent и location; адреса процесса в него не входят. */
static gchar *native_profile_location_key(const gchar *parent, const NativeSavedLocation *location)
{
    return g_strdup_printf("%" G_GSIZE_FORMAT ":%s%" G_GSIZE_FORMAT ":%s%" G_GSIZE_FORMAT ":%s:%u",
        strlen(parent), parent, strlen(location->file), location->file,
        strlen(location->function), location->function, location->line);
}

/* EN: Validate all disabled sub-locations in profile for points parents; reject invalid data through error.
 * RU: Проверяет выключенные подпозиции profile для points родителей; неверные данные возвращаются через error. */
static gboolean native_profile_locations_validate(GKeyFile *profile, gint points, GError **error)
{
    guint total = 0;
    gboolean valid = TRUE;
    GError *failure = NULL;
    for (gint i = 0; valid && i < points; i++)
    {
        gchar *parent = g_strdup_printf("breakpoint-%d", i);
        gint count = g_key_file_get_integer(profile, parent, "disabled-count", &failure);
        valid = !failure && count >= 0 && count <= 1024 && total + count <= 1024 &&
            (!count || native_profile_point_kind(profile, (guint)i) == 0);
        if (valid)
            total += (guint)count;
        for (gint j = 0; valid && j < count; j++)
        {
            gchar *group = g_strdup_printf("%s-location-%d", parent, j);
            gchar *file = g_key_file_get_string(profile, group, "file", NULL);
            gchar *function = g_key_file_get_string(profile, group, "function", NULL);
            gint line = g_key_file_get_integer(profile, group, "line", &failure);
            valid = !failure && line > 0 && native_profile_text_valid(file, 4096) && *file &&
                native_profile_text_valid(function, 4096) && *function;
            g_free(function);
            g_free(file);
            g_free(group);
        }
        g_free(parent);
    }
    g_clear_error(&failure);
    if (!valid)
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            insight_tr("Invalid saved C++ locations (maximum 1024 entries; source coordinates are required)."));
    return valid;
}
