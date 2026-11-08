/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Input controls, indicators, capture triggers and port slices.
 * RU: Органы ввода, индикаторы, триггеры записи и срезы портов.
 */

/* EN: Validate a DUT signal name or constant bit/range in name for generated HDL.
 * RU: Проверяет имя сигнала или постоянный бит/диапазон в name для генерации HDL. */
static gboolean simulation_capture_signal(const gchar *name)
{
    return g_regex_match_simple("^[A-Za-z_][A-Za-z0-9_$]*(\\.[A-Za-z_][A-Za-z0-9_$]*)*"
                                "(\\[-?[0-9]+(:-?[0-9]+)?\\])?$",
                                name, 0, 0);
}

/* EN: Validate count scope args and emit scope id into body and its sampling into live.
 * RU: Проверяет count аргументов args осциллографа, добавляет прибор id в body и выборку в live. */
static gboolean simulation_port_scope(gchar **args, gint count, guint id, GString *body, GString *live)
{
    guint axes = !strcmp(args[0], "scope") ? 1 : !strcmp(args[0], "scope-xy") ? 2 : 3;
    if (count != (gint)axes + 1 || id >= 8)
        return 0;
    for (guint i = 1; i <= axes; i++)
        if (!simulation_capture_signal(args[i]) || strlen(args[i]) > 256)
            return 0;
    g_string_append_printf(body, "// GEANY-SCOPE %u", axes);
    for (guint i = 1; i <= axes; i++)
        g_string_append_printf(body, " %s", args[i]);
    g_string_append_c(body, '\n');
    for (guint i = 1; i <= axes; i++)
        g_string_append_printf(body,
                               "initial if ($bits(dut.%s) > 64) "
                               "$fatal(1, \"Scope signal %s exceeds 64 bits. Select a slice.\");\n",
                               args[i], args[i]);
    g_string_append_printf(live, "$display(\"GEANY-SCOPE%%c%u%%c%%0d", id);
    for (guint i = 0; i < axes; i++)
        g_string_append(live, "%c%b");
    g_string_append(live, "\",9,9,$time");
    for (guint i = 1; i <= axes; i++)
        g_string_append_printf(live, ",9,dut.%s", args[i]);
    g_string_append(live, ");\n");
    return 1;
}

/* EN: Validate count trigger args and append the capture condition to trigger.
 * RU: Проверяет count аргументов args и добавляет условие захвата в trigger. */
static gboolean simulation_capture_trigger(gchar **args, gint count, GString *trigger)
{
    if (trigger->len || (count != 3 && count != 4) || !simulation_capture_signal(args[2]))
        return FALSE;
    if (count == 3 && (!strcmp(args[1], "rise") || !strcmp(args[1], "fall")))
    {
        gchar active = !strcmp(args[1], "rise") ? '1' : '0';
        gchar idle = active == '1' ? '0' : '1';
        g_string_append_printf(
            trigger,
            "begin : geany_capture_edge reg previous; previous=(dut.%s === 1'b%c); "
            "forever begin @(dut.%s); if (previous && dut.%s === 1'b%c) disable geany_capture_edge; "
            "previous=(dut.%s === 1'b%c); end end ",
            args[2], idle, args[2], args[2], active, args[2], idle);
        return TRUE;
    }
    if (count != 4 || strcmp(args[1], "match") ||
        !g_regex_match_simple("^[1-9][0-9]{0,3}'([bB][01?][01?_]*|[hH][0-9a-fA-F?][0-9a-fA-F?_]*)$",
                              args[3], 0, 0))
        return FALSE;
    gchar *base;
    guint64 width = g_ascii_strtoull(args[3], &base, 10);
    guint digits = 0;
    for (const gchar *it = base + 2; *it; it++)
        digits += *it != '_';
    guint bits = g_ascii_tolower(base[1]) == 'b' ? 1 : 4;
    if (width > 4096 || digits > (width + bits - 1) / bits)
        return FALSE;
    g_string_append_printf(trigger, "wait ((dut.%s %s %s) === 1'b1); ", args[2],
                           strchr(args[3], '?') ? "==?" : "===", args[3]);
    return TRUE;
}

/* EN: Validate count button or switch args and generate body, connections and live records using ports.
 * RU: Проверяет count аргументов args кнопки или переключателя и формирует body, connections и live с учётом ports. */
static gboolean simulation_port_control(gchar **args, gint count, GString *body, GString *connections,
                                        GHashTable *ports, GString *live)
{
    gboolean button = !strcmp(args[0], "button");
    guint64 length = 100;
    const gchar *idle = count >= 3 ? args[2] : "0";
    if (count < 2 || count > (button ? 4 : 3) || g_hash_table_size(ports) >= 128 ||
        !simulation_port_name(args[1]) || g_hash_table_contains(ports, args[1]) ||
        (strcmp(idle, "0") && strcmp(idle, "1")) ||
        (count == 4 && !g_ascii_string_to_unsigned(args[3], 10, 1, 1000000000, &length, NULL)))
        return FALSE;
    guint pin = simulation_port_connect(body, connections, ports, args[1], 1, FALSE);
    g_string_append_printf(body, "// GEANY-CONTROL %s %s %s\ninitial pin%u=1'b%s;\n", args[0], args[1],
                           idle, pin, idle);
    if (button)
    {
        g_string_append_printf(
            body,
            "initial begin : geany_button%u integer fd, serial, previous; previous=0; forever begin "
            "#(geany_live_interval); fd=$fopen(\"geany-control-%s\",\"r\"); if(fd) begin "
            "if($fscanf(fd,\"%%d\",serial)==1 && serial!=previous) begin "
            "previous=previous+1; $fclose(fd); pin%u=1'b%c; #%" G_GUINT64_FORMAT "; pin%u=1'b%s; "
            "end else $fclose(fd); end end end\n",
            pin, args[1], pin, *idle == '0' ? '1' : '0', length, pin, idle);
        g_string_append(live, "// Button pacing\n");
    }
    else
        g_string_append_printf(
            live,
            "fd=$fopen(\"geany-control-%s\",\"r\"); "
            "if(fd) begin if($fscanf(fd,\"%%b\",bits)==1) pin%u=bits; $fclose(fd); end\n",
            args[1], pin);
    return TRUE;
}

/* EN: Validate count slider args and generate body, connections and live records using ports.
 * RU: Проверяет count аргументов args ползунка и формирует body, connections и live с учётом ports. */
static gboolean simulation_port_slider(gchar **args, gint count, GString *body, GString *connections,
                                       GHashTable *ports, GString *live)
{
    guint64 width = 8, initial = 0;
    if (count < 2 || count > 4 || g_hash_table_size(ports) >= 128 || !simulation_port_name(args[1]) ||
        g_hash_table_contains(ports, args[1]) ||
        (count >= 3 && !g_ascii_string_to_unsigned(args[2], 10, 1, 16, &width, NULL)))
        return FALSE;
    guint64 maximum = (1u << width) - 1;
    if (count == 4 && !g_ascii_string_to_unsigned(args[3], 10, 0, maximum, &initial, NULL))
        return FALSE;
    guint pin = simulation_port_connect(body, connections, ports, args[1], width, FALSE);
    g_string_append_printf(body,
                           "// GEANY-SLIDER %s %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT "\n"
                           "initial pin%u=%" G_GUINT64_FORMAT ";\n",
                           args[1], maximum, initial, pin, initial);
    g_string_append_printf(live,
                           "fd=$fopen(\"geany-control-%s\",\"r\"); "
                           "if(fd) begin if($fscanf(fd,\"%%d\",bits)==1) pin%u=bits; $fclose(fd); end\n",
                           args[1], pin);
    return TRUE;
}

/* EN: Validate count tokens in args, attach a monitored port to connections and
 * ports, and append its indicator declaration to body and sampling code to live.
 * RU: Проверяет count токенов args, добавляет порт в connections и ports,
 * объявление индикатора в body и код опроса в live. */
static gboolean simulation_port_indicator(gchar **args, gint count, GString *body, GString *connections,
                                          GHashTable *ports, GString *live)
{
    gboolean lamp = !strcmp(args[0], "led");
    gboolean table = !strcmp(args[0], "hex-table");
    guint64 width = lamp ? 1 : table ? 32 : 8;
    if (count < 2 || count > (lamp || table ? 3 : 4) || g_hash_table_size(ports) >= 128 ||
        !simulation_port_name(args[1]) || g_hash_table_contains(ports, args[1]))
        return FALSE;
    const gchar *format = lamp         ? (count == 3 ? args[2] : "1")
                          : table      ? "hex-table"
                          : count == 4 ? args[3]
                                       : "hex";
    if (lamp ? (strcmp(format, "0") && strcmp(format, "1"))
             : ((count >= 3 && !g_ascii_string_to_unsigned(args[2], 10, 1, 64, &width, NULL)) ||
                (!table && strcmp(format, "hex") && strcmp(format, "dec") && strcmp(format, "bin"))))
        return FALSE;
    guint pin = simulation_port_connect(body, connections, ports, args[1], width, TRUE);
    g_string_append_printf(body, "// GEANY-INDICATOR %s %s\n", args[1], format);
    g_string_append_printf(live, "$display(\"GEANY-PORT%%c%s%%c%%b\",9,9,pin%u);\n", args[1], pin);
    return TRUE;
}

typedef struct
{
    gchar *alias, *port;
    gint left, right, first, last;
    gchar direction;
    guint net;
} SimulationPortSlice;

/* EN: Free the slice and its owned names from data.
 * RU: Освобождает срез и его имена из data. */
static void simulation_slice_free(gpointer data)
{
    SimulationPortSlice *slice = data;
    g_free(slice->alias);
    g_free(slice->port);
    g_free(slice);
}

/* EN: Parse an eight-token port-slice rule from args, appending it to slices.
 * RU: Разбирает правило port-slice из восьми токенов args и добавляет его в slices. */
static gboolean simulation_slice_parse(gchar **args, GPtrArray *slices)
{
    gint64 limits[4];
    if (slices->len >= 128 || !simulation_port_name(args[1]) || !simulation_port_name(args[2]) ||
        !strcmp(args[1], args[2]) || strlen(args[3]) != 1 || !strchr("iob", *args[3]))
        return FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(limits); i++)
        if (!g_ascii_string_to_signed(args[i + 4], 10, -1000000, 1000000, &limits[i], NULL))
            return FALSE;
    if (ABS(limits[0] - limits[1]) >= 4096 || limits[2] < MIN(limits[0], limits[1]) ||
        limits[2] > MAX(limits[0], limits[1]) || limits[3] < MIN(limits[0], limits[1]) ||
        limits[3] > MAX(limits[0], limits[1]) ||
        (limits[2] != limits[3] && (limits[0] < limits[1]) != (limits[2] < limits[3])))
        return FALSE;
    SimulationPortSlice *slice = g_new0(SimulationPortSlice, 1);
    slice->alias = g_strdup(args[1]);
    slice->port = g_strdup(args[2]);
    slice->direction = *args[3];
    slice->left = limits[0];
    slice->right = limits[1];
    slice->first = limits[2];
    slice->last = limits[3];
    g_ptr_array_add(slices, slice);
    return TRUE;
}

/* EN: Replace alias connections with real DUT port nets, adding slice bridges to body.
 * ports maps rule names to pins. Return FALSE for unused or conflicting slices.
 * RU: Заменяет псевдопорты реальными портами схемы и добавляет соединения в body.
 * ports связывает имена правил с выводами. Возвращает FALSE при конфликте или лишнем срезе. */
static gboolean simulation_slice_connect(GPtrArray *slices, GHashTable *ports, GString *connections,
                                         GString *body)
{
    for (guint i = 0; i < slices->len; i++)
    {
        SimulationPortSlice *slice = slices->pdata[i];
        guint pin = GPOINTER_TO_UINT(g_hash_table_lookup(ports, slice->alias));
        guint whole = GPOINTER_TO_UINT(g_hash_table_lookup(ports, slice->port));
        if (!pin || (whole && slice->direction != 'o'))
            return FALSE;
        pin--;
        slice->net = i;
        for (guint j = 0; j < i; j++)
        {
            SimulationPortSlice *previous = slices->pdata[j];
            if (!strcmp(slice->alias, previous->alias))
                return FALSE;
            if (!strcmp(slice->port, previous->port))
            {
                if (slice->direction != previous->direction || slice->left != previous->left ||
                    slice->right != previous->right)
                    return FALSE;
                if (slice->direction != 'o' &&
                    MIN(slice->first, slice->last) <= MAX(previous->first, previous->last) &&
                    MIN(previous->first, previous->last) <= MAX(slice->first, slice->last))
                    return FALSE;
                slice->net = previous->net;
            }
        }
        gchar *token = g_strdup_printf(".%s(pin%u)", slice->alias, pin);
        gchar *position = strstr(connections->str, token);
        if (!position)
        {
            g_free(token);
            return FALSE;
        }
        gsize offset = position - connections->str;
        g_string_erase(connections, offset, strlen(token));
        g_free(token);
        if (slice->net == i)
        {
            g_string_append_printf(body, "wire [%d:%d] geany_slice_net%u;\n", slice->left, slice->right,
                                   i);
            if (whole)
                g_string_append_printf(body, "assign geany_slice_net%u = pin%u;\n", i, whole - 1);
            else
            {
                gchar *connection = g_strdup_printf(".%s(geany_slice_net%u)", slice->port, i);
                g_string_insert(connections, offset, connection);
                g_free(connection);
            }
        }
        if (slice->net != i || whole)
        {
            if (offset)
                g_string_erase(connections, offset - 1, 1);
            else if (connections->len)
                g_string_erase(connections, 0, 1);
        }
        gchar *part =
            slice->first == slice->last
                ? g_strdup_printf("geany_slice_net%u[%d]", slice->net, slice->first)
                : g_strdup_printf("geany_slice_net%u[%d:%d]", slice->net, slice->first, slice->last);
        if (slice->first == slice->last)
            g_string_append_printf(body, "// GEANY-PORT-SLICE %s %s[%d]\n", slice->alias, slice->port,
                                   slice->first);
        else
            g_string_append_printf(body, "// GEANY-PORT-SLICE %s %s[%d:%d]\n", slice->alias, slice->port,
                                   slice->first, slice->last);
        guint width = ABS(slice->first - slice->last) + 1;
        g_string_append_printf(body,
                               "initial if ($bits(pin%u) != %u) "
                               "$fatal(1, \"Port slice width mismatch: %s\");\n",
                               pin, width, slice->port);
        if (slice->direction == 'i')
            g_string_append_printf(body, "assign %s = pin%u;\n", part, pin);
        else if (slice->direction == 'o')
            g_string_append_printf(body, "assign pin%u = %s;\n", pin, part);
        else
            g_string_append_printf(body, "tran geany_slice_bridge%u[%u:0](pin%u,%s);\n", i, width - 1,
                                   pin, part);
        g_free(part);
    }
    return TRUE;
}
