/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Port declarations and wiring for built-in memory devices.
 * RU: Объявления портов и подключение встроенных моделей памяти.
 */

/* EN: Return whether name is a simple HDL identifier accepted by recipe rules.
 * RU: Проверяет, является ли name простым идентификатором HDL для правил. */
static gboolean simulation_port_name(const gchar *name)
{
    return g_regex_match_simple("^[A-Za-z_][A-Za-z0-9_$]*$", name, 0, 0);
}

/* EN: Declare name in body and attach it in connections. ports owns its index, width sets bits, wire selects the HDL storage kind.
 * RU: Объявляет name в body и подключает в connections. ports хранит индекс, width задаёт ширину, wire выбирает тип сигнала. */
static guint simulation_port_connect(GString *body, GString *connections, GHashTable *ports,
                                     const gchar *name, guint64 width, gboolean wire)
{
    guint id = g_hash_table_size(ports);
    g_hash_table_insert(ports, g_strdup(name), GUINT_TO_POINTER(id + 1));
    g_string_append_printf(body, "%s [%" G_GUINT64_FORMAT ":0] pin%u;\n", wire ? "wire" : "reg",
                           width - 1, id);
    if (connections->len)
        g_string_append_c(connections, ',');
    g_string_append_printf(connections, ".%s(pin%u)", name, id);
    return id;
}

/* EN: Return the owned staging filename for image or script anchor.
 * RU: Возвращает новое имя временного файла образа или скрипта по anchor. */
static gchar *simulation_memory_filename(const gchar *anchor)
{
    return *anchor == ':' ? g_strdup_printf("geany-device-%s.lua", anchor + 1)
                          : g_strdup_printf("geany-memory-%s.hex", anchor);
}

/* EN: Append the staged file for anchor in images and optional reset to body, closing the device call.
 * RU: Добавляет в body файл anchor из images и необязательный reset, затем закрывает вызов устройства. */
static void simulation_device_argument(GString *body, GHashTable *images, const gchar *anchor,
                                       const gchar *reset)
{
    if (g_hash_table_contains(images, anchor))
    {
        gchar *name = simulation_memory_filename(anchor);
        g_string_append_printf(body, ",\"%s\"", name);
        g_free(name);
    }
    if (reset)
        g_string_append_printf(body, ",dut.%s", reset);
    g_string_append(body, ");\n");
}

/* EN: Finish the memory call in body with the optional image for anchor from images.
 * RU: Завершает вызов памяти в body, добавляя необязательный образ anchor из images. */
static void simulation_memory_argument(GString *body, GHashTable *images, const gchar *anchor)
{
    simulation_device_argument(body, images, anchor, NULL);
}

/* EN: Validate SRAM args and write HDL to body and connections. ports tracks used pins, images supplies preload files.
 * RU: Проверяет args SRAM и формирует HDL в body и connections. ports учитывает занятые выводы, images задаёт образы. */
static gboolean simulation_port_sram(gchar **args, GString *body, GString *connections, GHashTable *ports,
                                     GHashTable *images)
{
    guint64 address, width;
    if (g_hash_table_size(ports) > 123 ||
        !g_ascii_string_to_unsigned(args[6], 10, 1, 16, &address, NULL) ||
        !g_ascii_string_to_unsigned(args[7], 10, 1, 64, &width, NULL))
        return FALSE;
    for (guint i = 1; i <= 5; i++)
    {
        if (!simulation_port_name(args[i]) || g_hash_table_contains(ports, args[i]))
            return FALSE;
        for (guint j = 1; j < i; j++)
            if (!strcmp(args[i], args[j]))
                return FALSE;
    }
    guint addr = simulation_port_connect(body, connections, ports, args[1], address, TRUE);
    guint dq = simulation_port_connect(body, connections, ports, args[2], width, TRUE);
    guint ce = simulation_port_connect(body, connections, ports, args[3], 1, TRUE);
    guint we = simulation_port_connect(body, connections, ports, args[4], 1, TRUE);
    guint oe = simulation_port_connect(body, connections, ports, args[5], 1, TRUE);
    g_string_append_printf(body,
                           "reg [%" G_GUINT64_FORMAT ":0] sram_drive%u = 'z;\n"
                           "assign pin%u = sram_drive%u;\n"
                           "always @(pin%u or pin%u or pin%u or pin%u or pin%u) "
                           "$geany_sram(pin%u,pin%u,pin%u,pin%u,pin%u,sram_drive%u",
                           width - 1, addr, dq, addr, we, ce, oe, addr, dq, addr, dq, ce, we, oe, addr);
    simulation_memory_argument(body, images, args[1]);
    return TRUE;
}

/* EN: Validate DDR args for generation. Write HDL to body and connections using ports and preload images.
 * RU: Проверяет args DDR поколения generation. Формирует HDL в body и connections с выводами ports и образами images. */
static gboolean simulation_port_ddr(gchar **args, GString *body, GString *connections, GHashTable *ports,
                                    GHashTable *images, guint generation)
{
    const guint ddr2_widths[] = {1, 1, 1, 1, 1, 1, 14, 3, 8, 1, 1};
    const guint ddr3_widths[] = {1, 1, 1, 1, 1, 1, 1, 16, 3, 8, 1, 1};
    const guint ddr4_widths[] = {1, 1, 1, 1, 1, 17, 2, 2, 8, 1, 1};
    const guint *widths = generation == 2 ? ddr2_widths : generation == 3 ? ddr3_widths : ddr4_widths;
    guint count = generation == 3 ? G_N_ELEMENTS(ddr3_widths) : G_N_ELEMENTS(ddr4_widths);
    guint ids[G_N_ELEMENTS(ddr3_widths)];
    guint64 read_latency, write_latency;
    if (g_hash_table_size(ports) > 128 - count ||
        !g_ascii_string_to_unsigned(args[count + 1], 10, 1, 64, &read_latency, NULL) ||
        !g_ascii_string_to_unsigned(args[count + 2], 10, 1, 64, &write_latency, NULL))
        return FALSE;
    for (guint i = 0; i < count; i++)
    {
        if (!simulation_port_name(args[i + 1]) || g_hash_table_contains(ports, args[i + 1]))
            return FALSE;
        for (guint j = 0; j < i; j++)
            if (!strcmp(args[i + 1], args[j + 1]))
                return FALSE;
    }
    for (guint i = 0; i < count; i++)
        ids[i] = simulation_port_connect(body, connections, ports, args[i + 1], widths[i], TRUE);
    guint id = ids[0], dq = ids[count - 3], dqs = ids[count - 2];
    g_string_append_printf(body,
                           "reg [7:0] ddr_dq%u='z; reg ddr_dqs%u=1'bz;\n"
                           "assign pin%u=ddr_dq%u; assign pin%u=ddr_dqs%u;\n",
                           id, id, dq, id, dqs, id);
    g_string_append_printf(body, "always @(pin%u or pin%u", id, dqs);
    if (generation != 2)
        g_string_append_printf(body, " or pin%u", ids[1]);
    g_string_append_printf(body, ") $geany_ddr%u(", generation);
    for (guint i = 0; i < count; i++)
        g_string_append_printf(body, "pin%u,", ids[i]);
    g_string_append_printf(body, "%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT ",ddr_dq%u,ddr_dqs%u",
                           read_latency, write_latency, id, id);
    simulation_memory_argument(body, images, args[generation == 2 ? 7 : generation == 3 ? 8 : 6]);
    return TRUE;
}

/* EN: Validate SDR SDRAM args and add HDL to body and connections using ports and preload images.
 * RU: Проверяет args SDR SDRAM и добавляет HDL в body и connections, используя ports и образы images. */
static gboolean simulation_port_sdram(gchar **args, GString *body, GString *connections,
                                      GHashTable *ports, GHashTable *images)
{
    const guint widths[] = {1, 1, 1, 1, 1, 1, 13, 2, 16, 2};
    guint ids[G_N_ELEMENTS(widths)];
    if (g_hash_table_size(ports) > 128 - G_N_ELEMENTS(widths))
        return FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(widths); i++)
    {
        if (!simulation_port_name(args[i + 1]) || g_hash_table_contains(ports, args[i + 1]))
            return FALSE;
        for (guint j = 0; j < i; j++)
            if (!strcmp(args[i + 1], args[j + 1]))
                return FALSE;
    }
    for (guint i = 0; i < G_N_ELEMENTS(widths); i++)
        ids[i] = simulation_port_connect(body, connections, ports, args[i + 1], widths[i], TRUE);
    guint id = ids[0];
    g_string_append_printf(body,
                           "reg [15:0] sdram_drive%u='z; assign pin%u=sdram_drive%u;\n"
                           "always @(pin%u) $geany_sdram(",
                           id, ids[8], id, id);
    for (guint i = 0; i < G_N_ELEMENTS(ids); i++)
        g_string_append_printf(body, "pin%u,", ids[i]);
    g_string_append_printf(body, "sdram_drive%u", id);
    simulation_memory_argument(body, images, args[7]);
    return TRUE;
}
