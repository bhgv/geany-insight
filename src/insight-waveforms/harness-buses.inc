/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: SPI, I²C and UART wiring in generated testbenches.
 * RU: Подключение SPI, I²C и UART в генерируемых тестбенчах.
 */

/* EN: Validate and wire args into body and connections. ports and spi_pins track shared signals, images holds staged files, script_key and reset are optional.
 * RU: Проверяет args и добавляет HDL в body и connections. ports и spi_pins учитывают общие линии, images хранит файлы, script_key и reset необязательны. */
static gboolean simulation_port_spi(gchar **args, GString *body, GString *connections, GHashTable *ports,
                                    GHashTable *images, GHashTable *spi_pins, const gchar *script_key,
                                    const gchar *reset)
{
    guint ids[4], new_pins = 0;
    for (guint i = 1; i <= G_N_ELEMENTS(ids); i++)
    {
        if (!simulation_port_name(args[i]))
            return FALSE;
        if (g_hash_table_contains(ports, args[i]))
        {
            if (i == 2 || GPOINTER_TO_UINT(g_hash_table_lookup(spi_pins, args[i])) != i)
                return FALSE;
        }
        else
            new_pins++;
        for (guint j = 1; j < i; j++)
            if (!strcmp(args[i], args[j]))
                return FALSE;
    }
    if (g_hash_table_size(ports) + new_pins > 128)
        return FALSE;
    for (guint i = 1; i <= G_N_ELEMENTS(ids); i++)
    {
        gpointer pin = g_hash_table_lookup(ports, args[i]);
        if (pin)
            ids[i - 1] = GPOINTER_TO_UINT(pin) - 1;
        else
        {
            ids[i - 1] = simulation_port_connect(body, connections, ports, args[i], 1, TRUE);
            if (i != 2)
                g_hash_table_insert(spi_pins, g_strdup(args[i]), GUINT_TO_POINTER(i));
        }
    }
    guint ck = ids[0], cs = ids[1], mosi = ids[2], miso = ids[3];
    if (script_key)
        g_string_append_printf(body, "reg spi_start%u; initial #0 spi_start%u=1;\n", cs, cs);
    g_string_append_printf(body,
                           "reg spi_drive%u=1'bz; assign pin%u=spi_drive%u;\n"
                           "always @(pin%u or pin%u",
                           cs, miso, cs, ck, cs);
    if (script_key)
        g_string_append_printf(body, " or spi_start%u", cs);
    if (reset)
        g_string_append_printf(body, " or dut.%s", reset);
    g_string_append_printf(body, ") $geany_spi_%s(pin%u,pin%u,pin%u,spi_drive%u", args[0] + 4, ck, cs,
                           mosi, cs);
    simulation_device_argument(body, images, script_key ? script_key : args[2], reset);
    return TRUE;
}

/* EN: Parse decimal or 0x-prefixed value into a nonreserved 7-bit address.
 * RU: Разбирает десятичный value или value с 0x в допустимый семибитный address. */
static gboolean simulation_i2c_address(const gchar *value, guint64 *address)
{
    gboolean hex = g_str_has_prefix(value, "0x") || g_str_has_prefix(value, "0X");
    return g_ascii_string_to_unsigned(value + (hex ? 2 : 0), hex ? 16 : 10, 8, 119, address, NULL);
}

/* EN: Validate and wire args into body and connections. ports and buses track shared lines, images holds staged files, script_key and reset are optional.
 * RU: Проверяет args и добавляет HDL в body и connections. ports и buses учитывают общие линии, images хранит файлы, script_key и reset необязательны. */
static gboolean simulation_port_i2c(gchar **args, GString *body, GString *connections, GHashTable *ports,
                                    GHashTable *images, GHashTable *buses, const gchar *script_key,
                                    const gchar *reset)
{
    guint64 address;
    if (!simulation_port_name(args[1]) || !simulation_port_name(args[2]) || !strcmp(args[1], args[2]) ||
        !simulation_i2c_address(args[3], &address))
        return FALSE;
    guint scl, sda;
    const gchar *clock_port = script_key ? g_hash_table_lookup(buses, args[2]) : NULL;
    if (clock_port)
    {
        if (strcmp(clock_port, args[1]))
            return FALSE;
        scl = GPOINTER_TO_UINT(g_hash_table_lookup(ports, args[1])) - 1;
        sda = GPOINTER_TO_UINT(g_hash_table_lookup(ports, args[2])) - 1;
    }
    else
    {
        if (g_hash_table_size(ports) > 126 || g_hash_table_contains(ports, args[1]) ||
            g_hash_table_contains(ports, args[2]))
            return FALSE;
        scl = simulation_port_connect(body, connections, ports, args[1], 1, TRUE);
        sda = simulation_port_connect(body, connections, ports, args[2], 1, TRUE);
        g_string_append_printf(body, "pullup(pin%u); pullup(pin%u);\n", scl, sda);
        if (script_key)
            g_hash_table_insert(buses, g_strdup(args[2]), g_strdup(args[1]));
    }
    guint id = script_key ? sda * 128 + (guint)address + 128 : sda;
    if (script_key)
        g_string_append_printf(body, "reg i2c_start%u; initial #0 i2c_start%u=1;\n", id, id);
    g_string_append_printf(body,
                           "reg i2c_drive%u=1'bz; assign pin%u=i2c_drive%u;\n"
                           "always @(pin%u or pin%u",
                           id, sda, id, scl, sda);
    if (script_key)
        g_string_append_printf(body, " or i2c_start%u", id);
    if (reset)
        g_string_append_printf(body, " or dut.%s", reset);
    g_string_append_printf(body, ") $geany_i2c_%s(pin%u,pin%u,%" G_GUINT64_FORMAT ",i2c_drive%u",
                           script_key ? "lua" : "eeprom", scl, sda, address, id);
    simulation_device_argument(body, images, script_key ? script_key : args[2], reset);
    return TRUE;
}

/* EN: Validate and wire a controller recipe args into body and connections.
 * ports tracks claimed DUT pins. i2c selects open-drain I²C or SPI.
 * completion collects the controller done registers for the default run length.
 * RU: Проверяет args и добавляет подключение ведущего в body и connections.
 * ports содержит занятые выводы DUT. i2c выбирает I²C с открытым стоком или SPI.
 * completion собирает признаки завершения для длительности по умолчанию. */
static gboolean simulation_port_controller(gchar **args, GString *body, GString *connections,
                                           GHashTable *ports, gboolean i2c, GString *completion)
{
    guint pins = i2c ? 2 : 4;
    guint64 mode_or_address, read_count = 0, phase, start;
    const gchar *payload = args[i2c ? 4 : 5];
    gsize length = strlen(payload);
    gboolean empty = i2c && !strcmp(payload, "-");
    if (g_hash_table_size(ports) + pins > 128 ||
        (!empty && (!length || length > 128 || length % 2 ||
                    strspn(payload, "0123456789abcdefABCDEF") != length)) ||
        !g_ascii_string_to_unsigned(args[i2c ? 6 : 7], 10, 1, 1000000000, &phase, NULL) ||
        !g_ascii_string_to_unsigned(args[i2c ? 7 : 8], 10, 1, 1000000000, &start, NULL))
        return FALSE;
    if (i2c ? (!simulation_i2c_address(args[3], &mode_or_address) ||
               !g_ascii_string_to_unsigned(args[5], 10, 0, 64, &read_count, NULL))
            : !g_ascii_string_to_unsigned(args[6], 10, 0, 3, &mode_or_address, NULL))
        return FALSE;
    for (guint i = 1; i <= pins; i++)
    {
        if (!simulation_port_name(args[i]) || g_hash_table_contains(ports, args[i]))
            return FALSE;
        for (guint j = 1; j < i; j++)
            if (!strcmp(args[i], args[j]))
                return FALSE;
    }

    guint id = g_hash_table_size(ports);
    for (guint i = 1; i <= pins; i++)
        simulation_port_connect(body, connections, ports, args[i], 1, TRUE);
    g_string_append_printf(body, "reg bus_clk%u=%u, bus_data%u=%u, bus_cs%u=1, bus_done%u=0;\n", id,
                           i2c ? 1 : (guint)mode_or_address / 2, id, i2c, id, id);
    if (i2c)
        g_string_append_printf(body,
                               "pullup(pin%u); pullup(pin%u);\n"
                               "assign pin%u=bus_clk%u ? 1'bz : 1'b0;\n"
                               "assign pin%u=bus_data%u ? 1'bz : 1'b0;\n",
                               id, id + 1, id, id, id + 1, id);
    else
        g_string_append_printf(
            body, "assign pin%u=bus_clk%u; assign pin%u=bus_cs%u; assign pin%u=bus_data%u;\n", id, id,
            id + 1, id, id + 2, id);
    g_string_append_printf(
        body,
        "initial begin\n"
        "  #%" G_GUINT64_FORMAT ";\n"
        "  while (!bus_done%u) begin\n"
        "    $geany_%s_controller(pin%u,pin%u,bus_clk%u,bus_data%u,bus_cs%u,bus_done%u,"
        "\"%s\",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT ");\n"
        "    #%" G_GUINT64_FORMAT ";\n"
        "  end\nend\n",
        start, id, i2c ? "i2c" : "spi", id, id + (i2c ? 1 : 3), id, id, id, id, payload, mode_or_address,
        read_count, phase);
    if (completion->len)
        g_string_append(completion, " && ");
    g_string_append_printf(completion, "bus_done%u", id);
    return TRUE;
}

/* EN: Validate count UART args and append HDL to body and connections. ports tracks used pins and live enables pacing.
 * RU: Проверяет count аргументов args UART и дописывает HDL в body и connections. ports учитывает выводы, live включает регулирование скорости. */
static gboolean simulation_port_uart(gchar **args, gint count, GString *body, GString *connections,
                                     GHashTable *ports, GString *live)
{
    guint64 baud;
    if (g_hash_table_size(ports) > 126 || strstr(body->str, "task geany_uart_tx;") ||
        (count == 5 && strcmp(args[4], "console")) || !simulation_port_name(args[1]) ||
        !simulation_port_name(args[2]) || !strcmp(args[1], args[2]) ||
        g_hash_table_contains(ports, args[1]) || g_hash_table_contains(ports, args[2]) ||
        !g_ascii_string_to_unsigned(args[3], 10, 1, 100000000, &baud, NULL))
        return FALSE;
    guint rx = simulation_port_connect(body, connections, ports, args[1], 1, FALSE);
    guint tx = simulation_port_connect(body, connections, ports, args[2], 1, TRUE);
    if (count == 5)
        g_string_append(body, "// GEANY UART CONSOLE\n");
    g_string_append_printf(
        body,
        "localparam time geany_uart_bit = (1000000000.0 / %" G_GUINT64_FORMAT ");\n"
        "initial pin%u=1'b1;\n"
        "task geany_uart_tx; input [7:0] octet; integer n; begin\n"
        "pin%u=0; #(geany_uart_bit); for(n=0;n<8;n=n+1) begin "
        "pin%u=octet[n]; #(geany_uart_bit); end pin%u=1; #(geany_uart_bit); end endtask\n"
        "initial begin : geany_uart_receive integer n; reg [7:0] octet; forever begin\n"
        "@(negedge pin%u); #(geany_uart_bit/2); if(pin%u===1'b0) begin\n"
        "for(n=0;n<8;n=n+1) begin #(geany_uart_bit); octet[n]=pin%u; end\n"
        "#(geany_uart_bit); if(pin%u===1'b1 && !$isunknown(octet)) "
        "$display(\"GEANY-UART%%c%%02h\", 9, octet); $fflush(); end end end\n"
        "initial begin : geany_uart_send integer fd, code, serial, previous; reg [7:0] octet; "
        "previous=0; forever begin #(geany_live_interval); fd=$fopen(\"geany-uart-input\",\"r\");\n"
        "if(fd) begin code=$fscanf(fd,\"%%d\", serial); "
        "if(code==1 && serial!=previous) begin previous=serial; "
        "while($fscanf(fd,\"%%h\",octet)==1) geany_uart_tx(octet); "
        "$display(\"GEANY-UART-SENT%%c%%0d\",9,serial); $fflush(); end $fclose(fd); end "
        "end end\n",
        baud, rx, rx, rx, rx, tx, tx, tx, tx);
    /* UART also needs wall-clock pacing, even without parallel port monitors. */
    g_string_append(live, "// UART pacing\n");
    return TRUE;
}
