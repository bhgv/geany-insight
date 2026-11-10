/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Generate HDL adapters for parallel RGB, OpenLDI and TMDS displays.
 * RU: Генерирует HDL-адаптеры дисплеев RGB, OpenLDI и TMDS.
 */

/* EN: Validate a raster rule in args and wire its DUT outputs into body and
 * connections. ports tracks assignments and count is the number of tokens.
 * RU: Проверяет правило дисплея args из count токенов и подключает выходы
 * DUT в body и connections. ports учитывает назначенные выводы. */
static gboolean simulation_port_raster(gchar **args, gint count, GString *body,
                                       GString *connections, GHashTable *ports)
{
    gboolean rgb = !strcmp(args[0], "rgb-display");
    gboolean dual = !strcmp(args[0], "lvds-dual-display");
    gboolean lvds = dual || !strcmp(args[0], "lvds-display");
    guint inputs = rgb ? 6 : dual ? 9 : lvds ? 5 : 4;
    guint geometry = inputs + 1;
    if (count != (dual ? 17 : lvds ? 12 : 11))
        return FALSE;

    guint64 width, height, depth, vs, phase = 0;
    gboolean swap = FALSE;
    if (!g_ascii_string_to_unsigned(args[geometry], 10, 1, 3840, &width, NULL) ||
        !g_ascii_string_to_unsigned(args[geometry + 1], 10, 1, 2160, &height, NULL) ||
        !g_ascii_string_to_unsigned(args[geometry + 2], 10, 6, 8, &depth, NULL) ||
        depth == 7 || (dual && (width & 1)) ||
        !g_ascii_string_to_unsigned(args[geometry + (rgb ? 3 : 5)], 10, 0, 1, &vs, NULL))
        return FALSE;

    guint mode = 0;
    if (!rgb)
    {
        const gchar *format = args[geometry + 3];
        if (!g_ascii_string_to_unsigned(args[geometry + 4], 10, 0, lvds ? 6 : 9, &phase, NULL))
            return FALSE;
        if (lvds && !strcmp(format, "jeida"))
            mode = 1;
        else if (lvds && !strcmp(format, "vesa"))
            mode = 2;
        else if (!lvds && !strcmp(format, "hdmi"))
            mode = 3;
        else if (!lvds && !strcmp(format, "dvi"))
            mode = 4;
        else
            return FALSE;
        if (lvds && depth == 6 &&
            (strcmp(args[5], "-") || (dual && strcmp(args[9], "-"))))
            return FALSE;
        if (dual)
        {
            const gchar *order = args[geometry + 6];
            swap = !strcmp(order, "b-first");
            if (!swap && strcmp(order, "a-first"))
                return FALSE;
        }
    }

    guint connected = inputs - (lvds && depth == 6 ? (dual ? 2 : 1) : 0);
    if (g_hash_table_size(ports) + connected > 128)
        return FALSE;
    for (guint i = 1; i <= inputs; i++)
    {
        if (lvds && depth == 6 && (i == 5 || i == 9))
            continue;
        if (!simulation_port_name(args[i]) || g_hash_table_contains(ports, args[i]))
            return FALSE;
        for (guint j = 1; j < i; j++)
            if (!strcmp(args[i], args[j]))
                return FALSE;
    }

    guint id = g_hash_table_size(ports);
    gint pins[9];
    for (guint i = 0; i < G_N_ELEMENTS(pins); ++i)
    {
        pins[i] = -1;
        if (i < inputs && !(lvds && depth == 6 && (i == 4 || i == 8)))
            pins[i] = (gint)simulation_port_connect(body, connections, ports, args[i + 1],
                                                   rgb && i >= 3 ? depth : 1, TRUE);
    }
    g_string_append_printf(body, "// GEANY-RASTER %u %u %u %u\n",
                           id, dual ? mode + 4 : mode, (guint)width, (guint)height);
    g_string_append_printf(body, "always @(posedge pin%u) $geany_raster%s(",
                           id, dual ? "_dual" : "");
    for (guint i = 0; i < 6; i++)
    {
        if (i < (dual ? 5u : inputs) && pins[i] >= 0)
            g_string_append_printf(body, "pin%d,", pins[i]);
        else
            g_string_append(body, "1'b0,");
    }
    g_string_append_printf(body, "%u,%u,%u,%u,%u,%u,%u",
                           (guint)width, (guint)height, (guint)depth, (guint)vs, mode, (guint)phase, id);
    if (dual)
    {
        for (guint i = 5; i < 9; i++)
            if (pins[i] >= 0)
                g_string_append_printf(body, ",pin%d", pins[i]);
            else
                g_string_append(body, ",1'b0");
        g_string_append_printf(body, ",%u", (guint)swap);
    }
    g_string_append(body, ");\n");
    return TRUE;
}

/* EN: Select HDMI geometry from lines for clock. Auto accepts one display,
 * or the default when none is configured. Set width and height on success.
 * RU: Выбирает геометрию HDMI из lines по clock. Auto принимает один дисплей
 * или значения по умолчанию. При успехе заполняет width и height. */
static gboolean simulation_hdmi_geometry(gchar **lines, const gchar *clock,
                                         guint *width, guint *height)
{
    gboolean automatic = !strcmp(clock, "auto");
    guint found = 0;
    *width = 640;
    *height = 480;
    for (guint i = 0; lines[i]; ++i)
    {
        const gchar *line = lines[i];
        while (g_ascii_isspace(*line))
            ++line;
        if (!*line || *line == '#')
            continue;
        gint count = 0;
        gchar **args = NULL;
        if (!g_shell_parse_argv(line, &count, &args, NULL))
            continue;
        guint64 w, h;
        if (count == 11 && !strcmp(args[0], "hdmi-display") &&
            (automatic || !strcmp(clock, args[1])) &&
            g_ascii_string_to_unsigned(args[5], 10, 1, 3840, &w, NULL) &&
            g_ascii_string_to_unsigned(args[6], 10, 1, 2160, &h, NULL))
        {
            ++found;
            *width = (guint)w;
            *height = (guint)h;
        }
        g_strfreev(args);
    }
    return found == 1 || (automatic && !found);
}
