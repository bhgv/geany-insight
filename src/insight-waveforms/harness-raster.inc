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
    gboolean lvds = !strcmp(args[0], "lvds-display");
    guint inputs = rgb ? 6 : lvds ? 5 : 4;
    guint geometry = inputs + 1;
    if (count != (rgb || !lvds ? 11 : 12))
        return FALSE;

    guint64 width, height, depth, vs, phase = 0;
    if (!g_ascii_string_to_unsigned(args[geometry], 10, 1, 2048, &width, NULL) ||
        !g_ascii_string_to_unsigned(args[geometry + 1], 10, 1, 1200, &height, NULL) ||
        !g_ascii_string_to_unsigned(args[geometry + 2], 10, 6, 8, &depth, NULL) ||
        depth == 7 || !g_ascii_string_to_unsigned(args[count - 1], 10, 0, 1, &vs, NULL))
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
        if (lvds && depth == 6)
        {
            if (strcmp(args[5], "-"))
                return FALSE;
            inputs--;
        }
    }

    if (g_hash_table_size(ports) + inputs > 128)
        return FALSE;
    for (guint i = 1; i <= inputs; i++)
    {
        if (!simulation_port_name(args[i]) || g_hash_table_contains(ports, args[i]))
            return FALSE;
        for (guint j = 1; j < i; j++)
            if (!strcmp(args[i], args[j]))
                return FALSE;
    }

    guint id = g_hash_table_size(ports);
    for (guint i = 1; i <= inputs; i++)
        simulation_port_connect(body, connections, ports, args[i], rgb && i >= 4 ? depth : 1, TRUE);
    g_string_append_printf(body, "// GEANY-RASTER %u %u %u %u\n",
                           id, mode, (guint)width, (guint)height);
    g_string_append_printf(body, "always @(posedge pin%u) $geany_raster(", id);
    for (guint i = 0; i < 6; i++)
    {
        if (i < inputs)
            g_string_append_printf(body, "pin%u,", id + i);
        else
            g_string_append(body, "1'b0,");
    }
    g_string_append_printf(body, "%u,%u,%u,%u,%u,%u,%u);\n",
                           (guint)width, (guint)height, (guint)depth, (guint)vs, mode, (guint)phase, id);
    return TRUE;
}
