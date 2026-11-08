/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Recipe validation and assembly of the generated testbench.
 * RU: Проверка рецепта и сборка генерируемого тестбенча.
 */

/* EN: Generate a harness from recipe, resolving files in directory. Append HDL
 * dependencies to sources and staged bytes to images. Return owned HDL or set error.
 * RU: Генерирует тестовый модуль из recipe, разрешая файлы в directory. Добавляет
 * зависимости в sources и данные в images. Возвращает новый HDL или задаёт error. */
static gchar *simulation_port_harness(const gchar *recipe, const gchar *directory, GPtrArray *sources,
                                      GHashTable *images, GError **error)
{
    gchar **lines = g_strsplit(recipe, "\n", -1);
    GString *body = g_string_new("`timescale 1ns/1ns\nmodule geany_port_testbench;\n");
    GString *connections = g_string_new(NULL);
    GString *controller_completion = g_string_new(NULL);
    GString *trigger = g_string_new(NULL);
    GString *live = g_string_new(NULL);
    guint64 live_interval = 0;
    GPtrArray *slices = g_ptr_array_new_with_free_func(simulation_slice_free);
    GHashTable *ports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *i2c_buses = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    GHashTable *spi_pins = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *models = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, simulation_model_free);
    gchar *module = NULL;
    guint64 duration = 1000;
    gboolean explicit_duration = FALSE;
    guint attached_images = 0, scopes = 0;
    gboolean valid = TRUE;
    for (guint i = 0; lines[i] && valid; i++)
    {
        gchar *line = g_strstrip(lines[i]);
        if (!*line || *line == '#')
            continue;
        gint count = 0;
        gchar **args = NULL;
        valid = g_shell_parse_argv(line, &count, &args, NULL);
        if (valid)
            simulation_memory_defaults(&args, &count);
        if (valid && count == 2 && !strcmp(args[0], "module") && !module &&
            g_regex_match_simple("^[A-Za-z_][A-Za-z0-9_$]*$", args[1], 0, 0))
            module = g_strdup(args[1]);
        else if (valid && count == 8 && !strcmp(args[0], "port-slice"))
            valid = simulation_slice_parse(args, slices);
        else if (valid && count == 2 && !strcmp(args[0], "duration"))
        {
            valid = g_ascii_string_to_unsigned(args[1], 10, 1, 1000000000, &duration, NULL);
            explicit_duration = TRUE;
        }
        else if (valid && count == 2 && !strcmp(args[0], "live-interval"))
            valid = g_ascii_string_to_unsigned(args[1], 10, 1, 1000000000, &live_interval, NULL);
        else if (valid && (!strcmp(args[0], "scope") || !strcmp(args[0], "scope-xy") ||
                           !strcmp(args[0], "scope-xyz")))
        {
            valid = simulation_port_scope(args, count, scopes, body, live);
            scopes += valid;
        }
        else if (valid && !strcmp(args[0], "slider"))
            valid = simulation_port_slider(args, count, body, connections, ports, live);
        else if (valid && (!strcmp(args[0], "led") || !strcmp(args[0], "display") ||
                           !strcmp(args[0], "hex-table")))
            valid = simulation_port_indicator(args, count, body, connections, ports, live);
        else if (valid && !strcmp(args[0], "capture-trigger"))
            valid = simulation_capture_trigger(args, count, trigger);
        else if (valid && (!strcmp(args[0], "button") || !strcmp(args[0], "switch")))
            valid = simulation_port_control(args, count, body, connections, ports, live);
        else if (valid && count == 3 && !strcmp(args[0], "memory-image"))
            valid = simulation_port_name(args[1]) && !g_hash_table_contains(ports, args[1]) &&
                    simulation_memory_image(images, args[1], args[2], directory, error);
        else if (valid && count == 11 && !strcmp(args[0], "sdram"))
        {
            valid = simulation_port_sdram(args, body, connections, ports, images);
            attached_images += valid && g_hash_table_contains(images, args[7]);
        }
        else if (valid && count == 8 && !strcmp(args[0], "sram"))
        {
            valid = simulation_port_sram(args, body, connections, ports, images);
            attached_images += valid && g_hash_table_contains(images, args[1]);
        }
        else if (valid &&
                 ((count == 14 && (!strcmp(args[0], "ddr1") || !strcmp(args[0], "ddr2") ||
                                   !strcmp(args[0], "ddr4"))) ||
                  (count == 15 && !strcmp(args[0], "ddr3"))))
        {
            guint generation = args[0][3] - '0';
            guint image_port = generation <= 2 ? 7 : generation == 3 ? 8 : 6;
            valid = simulation_port_ddr(args, body, connections, ports, images, generation);
            attached_images += valid && g_hash_table_contains(images, args[image_port]);
        }
        else if (valid && ((count == 9 && !strcmp(args[0], "spi-controller")) ||
                           (count == 8 && !strcmp(args[0], "i2c-controller"))))
            valid = simulation_port_controller(args, body, connections, ports, count == 8,
                                               controller_completion);
        else if (valid && (count == 2 || count == 6 || count == 7) && !strcmp(args[0], "spi-lua"))
        {
            gchar *defaults[] = {args[0], "sck", "cs_n", "mosi", "miso", args[1]};
            gchar **pins = count == 2 ? defaults : args;
            const gchar *reset = count == 7 ? args[6] : NULL;
            gchar *key = g_strconcat(":spi_", pins[2], NULL);
            valid = (!reset || simulation_capture_signal(reset)) &&
                    simulation_memory_image(images, key, pins[5], directory, error) &&
                    simulation_port_spi(pins, body, connections, ports, images, spi_pins, key, reset);
            attached_images += valid;
            g_free(key);
        }
        else if (valid && (count == 2 || count == 5 || count == 6) && !strcmp(args[0], "i2c-lua"))
        {
            gchar *defaults[] = {args[0], "scl", "sda", "0x50", args[1]};
            gchar **pins = count == 2 ? defaults : args;
            const gchar *reset = count == 6 ? args[5] : NULL;
            guint64 address;
            valid =
                (!reset || simulation_capture_signal(reset)) && simulation_i2c_address(pins[3], &address);
            if (valid)
            {
                gchar *key = g_strdup_printf(":i2c_%s_%" G_GUINT64_FORMAT, pins[2], address);
                valid =
                    simulation_memory_image(images, key, pins[4], directory, error) &&
                    simulation_port_i2c(pins, body, connections, ports, images, i2c_buses, key, reset);
                attached_images += valid;
                g_free(key);
            }
        }
        else if (valid && count == 5 &&
                 (!strcmp(args[0], "spi-nor") || !strcmp(args[0], "spi-nand") ||
                  !strcmp(args[0], "spi-eeprom") || !strcmp(args[0], "spi-psram")))
        {
            valid = simulation_port_spi(args, body, connections, ports, images, spi_pins, NULL, NULL);
            attached_images += valid && g_hash_table_contains(images, args[2]);
        }
        else if (valid && count == 4 && !strcmp(args[0], "i2c-eeprom"))
        {
            valid = simulation_port_i2c(args, body, connections, ports, images, i2c_buses, NULL, NULL);
            attached_images += valid && g_hash_table_contains(images, args[2]);
        }
        else if (valid && (count == 4 || count == 5) && !strcmp(args[0], "uart"))
            valid = simulation_port_uart(args, count, body, connections, ports, live);
        else if (valid && (!strcmp(args[0], "model") || g_str_has_prefix(args[0], "model-")))
            valid =
                simulation_model_rule(args, count, models, sources, directory, body, connections, ports);
        else if (valid && count >= 3 && g_hash_table_size(ports) < 128 &&
                 g_regex_match_simple("^[A-Za-z_][A-Za-z0-9_$]*$", args[1], 0, 0) &&
                 !g_hash_table_contains(ports, args[1]))
        {
            const gchar *mode = args[0];
            guint64 width = 1;
            gboolean monitor = !strcmp(mode, "monitor");
            gboolean input = !strcmp(mode, "live-input");
            gboolean output = monitor || !strcmp(mode, "output");
            gboolean scalar =
                !strcmp(mode, "clock") || !strcmp(mode, "clock-hz") || !strcmp(mode, "pulse");
            gboolean bus = !strcmp(mode, "bus");
            gboolean constant = !strcmp(mode, "value");
            valid = (output && count == 3) || (!strcmp(mode, "clock") && count == 5) ||
                    (!strcmp(mode, "clock-hz") && count == 4) || (!strcmp(mode, "pulse") && count == 5) ||
                    (bus && count == 5) || ((constant || input) && count == 4);
            if (valid && !scalar)
                valid = g_ascii_string_to_unsigned(args[2], 10, 1, 4096, &width, NULL);
            if (valid && (monitor || input))
                valid = width <= 64;
            for (gint j = scalar ? 2 : 3; j < count && valid; j++)
                valid = simulation_port_expression(args[j]);
            if (valid && !strcmp(mode, "pulse"))
                valid = !strcmp(args[4], "0") || !strcmp(args[4], "1");
            if (valid)
            {
                guint id = simulation_port_connect(body, connections, ports, args[1], width, output);
                if (constant || input)
                    g_string_append_printf(body, "initial pin%u = (%s);\n", id, args[3]);
                if (input)
                    g_string_append_printf(
                        live,
                        "fd=$fopen(\"geany-input-%s\",\"r\"); "
                        "if(fd) begin if($fscanf(fd,\"%%b\",bits)==1) pin%u=bits; $fclose(fd); end\n",
                        args[1], id);
                else if (monitor)
                    g_string_append_printf(live, "$display(\"GEANY-PORT%%c%s%%c%%b\",9,9,pin%u);\n",
                                           args[1], id);
                else if (bus)
                    g_string_append_printf(
                        body,
                        "initial begin integer step; time delay_ns; step=0; forever begin "
                        "pin%u=(%s); if (!( (%s)>0 )) $fatal(1, \"Invalid bus interval\"); "
                        "delay_ns=(%s); if (delay_ns==0) $fatal(1, \"Bus interval rounds to zero\"); "
                        "#(delay_ns); step=step+1; end end\n",
                        id, args[4], args[3], args[3]);
                else if (!strcmp(mode, "pulse"))
                    g_string_append_printf(
                        body,
                        "initial begin time length_ns; pin%u=1'b%c; "
                        "if (!( (%s)>=0 && (%s)>0 )) $fatal(1, \"Invalid pulse timing\"); "
                        "length_ns=(%s); if (!length_ns) $fatal(1, \"Pulse length rounds to zero\"); "
                        "#(%s) pin%u=1'b%s; #(length_ns) pin%u=1'b%c; end\n",
                        id, *args[4] == '0' ? '1' : '0', args[2], args[3], args[3], args[2], id, args[4],
                        id, *args[4] == '0' ? '1' : '0');
                else if (scalar)
                {
                    gchar *half_period = count == 4 ? g_strdup_printf("500000000.0/(%s)", args[3]) : NULL;
                    const gchar *high = half_period ? half_period : args[3];
                    const gchar *low = half_period ? half_period : args[4];
                    g_string_append_printf(
                        body,
                        "initial begin time high_ns, low_ns; pin%u=0; "
                        "if (!( (%s)>=0 )) $fatal(1, \"Invalid clock start\"); #(%s); forever begin "
                        "if (!( (%s)>0 && (%s)>0 )) $fatal(1, \"Invalid clock phase\"); "
                        "high_ns=(%s); low_ns=(%s); if (!high_ns || !low_ns) $fatal(1, \"Clock phase rounds to zero\"); "
                        "pin%u=1; #(high_ns); pin%u=0; #(low_ns); end end\n",
                        id, args[2], args[2], high, low, high, low, id, id);
                    g_free(half_period);
                }
            }
        }
        else
            valid = FALSE;
        if (!valid && (!error || !*error))
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid port recipe on line %u.",
                        i + 1);
        g_strfreev(args);
    }
    if (valid && !simulation_slice_connect(slices, ports, connections, body))
    {
        valid = FALSE;
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Conflicting port slices or a slice without a device.");
    }
    g_ptr_array_unref(slices);
    if (valid && attached_images != g_hash_table_size(images))
    {
        valid = FALSE;
        g_set_error_literal(
            error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            "Place memory-image before the memory rule and use its address port, SPI chip-select port or I2C SDA.");
    }
    if (valid && !module &&
        (trigger->len || scopes || connections->len || sources->len || g_hash_table_size(models)))
    {
        valid = FALSE;
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Add a module line for the configured ports.");
    }
    gchar *result = NULL;
    if (valid && module)
    {
        GList *instances = g_list_sort(g_hash_table_get_keys(models), (GCompareFunc)g_strcmp0);
        for (GList *it = instances; it; it = it->next)
        {
            SimulationModel *model = g_hash_table_lookup(models, it->data);
            g_string_append_printf(body, "%s ", model->module);
            if (model->parameters->len)
                g_string_append_printf(body, "#(%s) ", model->parameters->str);
            g_string_append_printf(body, "geany_model_%s(%s);\n", (const gchar *)it->data,
                                   model->pins->str);
        }
        g_list_free(instances);
        g_string_append_printf(body, "%s dut(%s);\n", module, connections->str);
        if (trigger->len)
            g_string_append(body, "// GEANY-CAPTURE-TRIGGER\n");
        g_string_append_printf(
            body,
            "initial begin %s$dumpfile(\"ports.vcd\"); $dumpvars(0,geany_port_testbench); %s end\n"
            "initial begin #%" G_GUINT64_FORMAT "; ",
            trigger->str,
            trigger->len ? "$display(\"Waveform capture started at %0t\",$time); $fflush();" : "",
            duration);
        if (!explicit_duration && controller_completion->len)
            g_string_append_printf(body, "wait (%s); #1; ", controller_completion->str);
        g_string_append(body, "$finish; end\nendmodule\n");
        if (live->len)
        {
            /* Put the pacing checkpoint at line 3 of the private source. */
            gchar *preamble =
                g_strdup_printf("task geany_live_yield; $fflush(); endtask\n"
                                "localparam time geany_live_interval = %" G_GUINT64_FORMAT ";\n",
                                live_interval ? live_interval : 10);
            g_string_insert(body, strlen("`timescale 1ns/1ns\nmodule geany_port_testbench;\n"), preamble);
            g_free(preamble);
            g_string_truncate(body, body->len - strlen("endmodule\n"));
            g_string_append_printf(
                body,
                "initial begin integer fd; reg [63:0] bits; forever begin #%" G_GUINT64_FORMAT ";\n",
                live_interval ? live_interval : 10);
            g_string_append(body, live->str);
            g_string_append(body, "geany_live_yield(); end end\nendmodule\n");
        }
        result = g_string_free(body, FALSE);
    }
    else
        g_string_free(body, TRUE);
    g_free(module);
    g_hash_table_unref(ports);
    g_hash_table_unref(spi_pins);
    g_hash_table_unref(i2c_buses);
    g_hash_table_unref(models);
    g_string_free(controller_completion, TRUE);
    g_string_free(connections, TRUE);
    g_string_free(live, TRUE);
    g_string_free(trigger, TRUE);
    g_strfreev(lines);
    return result;
}
