/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Source stop locations and live instrument records.
 * RU: Позиции остановок в исходнике и записи живых приборов.
 */

/* EN: Decode a simulator source record text into filename and line. Return whether it matched.
 * RU: Разбирает запись позиции text в filename и line. Возвращает признак совпадения формата. */
static gboolean simulation_trace_location(const gchar *text, gchar **filename, guint *line)
{
    const gchar *bridge = strstr(text, "GEANY-VVP-STOP\t");
    const gchar *breakpoint = strstr(text, "GEANY-VVP-BREAKPOINT\t");
    if (bridge || breakpoint)
    {
        const gchar *number = strchr(breakpoint ? breakpoint : bridge, '\t') + 1;
        const gchar *tab = strchr(number, '\t');
        if (!tab || !tab[1])
            return FALSE;
        gchar *field = g_strndup(number, tab - number);
        guint64 value;
        gboolean valid = g_ascii_string_to_unsigned(field, 10, 1, G_MAXINT, &value, NULL);
        g_free(field);
        if (!valid)
            return FALSE;
        *filename = g_strdup(tab + 1);
        *line = (guint)value;
        return TRUE;
    }

    for (const gchar *colon = strchr(text, ':'); colon; colon = strchr(colon + 1, ':'))
    {
        const gchar *end = colon + 1 + strspn(colon + 1, "0123456789");
        if (end == colon + 1 || *end != ':' || end[1] != ' ')
            continue;
        gchar *path = g_strndup(text, colon - text);
        if (!g_path_is_absolute(path))
        {
            g_free(path);
            return FALSE;
        }
        gchar *field = g_strndup(colon + 1, end - colon - 1);
        guint64 value;
        gboolean valid = g_ascii_string_to_unsigned(field, 10, 1, G_MAXINT, &value, NULL);
        g_free(field);
        if (!valid)
        {
            g_free(path);
            return FALSE;
        }
        *filename = path;
        *line = (guint)value;
        return TRUE;
    }
    return FALSE;
}

/* EN: Format sampled bits as owned hexadecimal byte rows with X/Z preserved.
 * RU: Формирует новую шестнадцатеричную таблицу байтов из bits, сохраняя X/Z. */
static gchar *simulation_hex_table(const gchar *bits)
{
    gsize width = strlen(bits);
    guint bytes = (width + 7) / 8;
    GString *text = g_string_new(NULL);
    for (guint byte = 0; byte < bytes; byte++)
    {
        if (byte)
            g_string_append_c(text, '\n');
        g_string_append_printf(text, "%02X  ", bytes - byte - 1);
        for (guint nibble = 0; nibble < 2; nibble++)
        {
            guint number = 0;
            gchar unknown = 0;
            gboolean known = FALSE;
            for (guint bit = 0; bit < 4; bit++)
            {
                gint index = (gint)(byte * 8 + nibble * 4 + bit) - (gint)(bytes * 8 - width);
                gchar value = index < 0 ? '0' : bits[index];
                number = (number << 1) | (value == '1');
                if (value == 'x' || value == 'z')
                    unknown = unknown && unknown != value ? '?' : value;
                else
                    known = TRUE;
            }
            g_string_append_c(text, unknown ? (known ? '?' : g_ascii_toupper(unknown))
                                            : "0123456789ABCDEF"[number]);
        }
    }
    return g_string_free(text, FALSE);
}

/* EN: Refresh the live indicator for port in run from its sampled bits.
 * RU: Обновляет индикатор порта port в run по полученным битам bits. */
static void simulation_update_indicator(WaveSimulation *run, const gchar *port, const gchar *bits)
{
    GtkLabel *label = run->live_indicators ? g_hash_table_lookup(run->live_indicators, port) : NULL;
    if (!label)
        return;
    const gchar *format = g_object_get_data(G_OBJECT(label), "geany-format");
    if (*format == '0' || *format == '1')
    {
        const gchar *state = *bits == *format               ? "●  ON"
                             : *bits == '0' || *bits == '1' ? "○  OFF"
                             : *bits == 'z'                 ? "◉  Z"
                                                            : "◉  X";
        gtk_label_set_text(label, state);
        return;
    }
    guint64 value;
    gchar *reading;
    if (!strcmp(format, "hex-table"))
        reading = simulation_hex_table(bits);
    else if (!strcmp(format, "bin") || !instrument_number(bits, &value))
        reading = g_strdup_printf("0b%s", bits);
    else if (!strcmp(format, "hex"))
        reading = g_strdup_printf("0x%0*" G_GINT64_MODIFIER "X", (gint)((strlen(bits) + 3) / 4), value);
    else
        reading = g_strdup_printf("%" G_GUINT64_FORMAT, value);
    gtk_label_set_text(label, reading);
    g_free(reading);
}

/* EN: Decode one record from the simulator and add a sample to the live scope in run.
 * RU: Разбирает запись record симулятора и добавляет отсчёт осциллографа в run. */
static void simulation_scope_record(WaveSimulation *run, const gchar *record)
{
    gchar **fields = g_strsplit(record, "\t", 6);
    guint length = g_strv_length(fields);
    guint64 id, tick;
    if (length >= 3 &&
        g_ascii_string_to_unsigned(fields[0], 10, 0, run->live_scopes->len - 1, &id, NULL) &&
        g_ascii_string_to_unsigned(fields[1], 10, 0, G_MAXUINT64, &tick, NULL))
    {
        LiveScope *scope = run->live_scopes->pdata[id];
        if (length == scope->axes + 2)
        {
            guint previous = (scope->next + LIVE_SCOPE_POINTS - 1) % LIVE_SCOPE_POINTS;
            if (scope->count && tick < scope->samples[previous].time)
            {
                g_strfreev(fields);
                return;
            }
            LiveScopeSample sample = {.time = tick, .point.valid = TRUE};
            for (guint i = 0; i < scope->axes; i++)
                if (!instrument_number(fields[i + 2], &sample.point.values[i]))
                    sample.point.valid = FALSE;
            scope->samples[scope->next] = sample;
            scope->next = (scope->next + 1) % LIVE_SCOPE_POINTS;
            scope->count = MIN(scope->count + 1, LIVE_SCOPE_POINTS);
            scope->dirty = TRUE;
        }
    }
    g_strfreev(fields);
}
