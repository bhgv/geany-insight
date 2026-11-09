/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Capture data structures and bounded VCD parsing.
 * RU: Структуры записи и чтение VCD с ограничениями.
 */

#define VCD_BYTES (32u * 1024u * 1024u)
#define VCD_SIGNALS 4096u
#define VCD_EVENTS 250000u
#define WAVE_TRACKS 16u
#define TRACK_HEIGHT 64
#define TRACK_LABEL_WIDTH 220

typedef struct
{
    guint64 time;
    gchar *value;
} Edge;
typedef struct
{
    GArray *edges;
    guint width;
} Trace;
typedef struct
{
    gchar *name, *scope, *leaf;
    Trace *trace;
} Signal;
typedef struct
{
    GPtrArray *signals;
    GHashTable *traces;
    guint64 end;
    gchar *scale;
} Capture;
typedef struct
{
    Capture *capture;
    GtkWidget *window, *selector, *canvas, *value, *source, *pin, *filter, *radix, *markers;
    GtkListStore *watch_values;
    GtkWidget *instruments, *instrument_plot, *instrument_range;
    GtkWidget *instrument_signal[3], *instrument_lamp[3], *instrument_value[3];
    gint instrument_index[3];
    gchar *session_path;
    gint active;
    gboolean rebuilding;
    GArray *tracks;
    guint64 cursor, start, span, marker[2];
    gboolean marker_set[2], hexadecimal;
    InsightWaveSource navigate;
} Viewer;

/* EN: Release the value in the Edge stored at data.
 * RU: Освобождает значение Edge, переданного в data. */
static void edge_clear(gpointer data)
{
    g_free(((Edge *)data)->value);
}
/* EN: Free the Trace and its owned edges from data.
 * RU: Освобождает Trace из data и его переходы. */
static void trace_free(gpointer data)
{
    Trace *trace = data;
    g_array_unref(trace->edges);
    g_free(trace);
}
/* EN: Free the Signal and its names from data. Its trace is shared.
 * RU: Освобождает Signal из data и его имена. Трасса общая. */
static void signal_free(gpointer data)
{
    Signal *signal = data;
    g_free(signal->name);
    g_free(signal->scope);
    g_free(signal->leaf);
    g_free(signal);
}
/* EN: Free the Capture from data with its signals and traces.
 * RU: Освобождает запись Capture из data вместе с сигналами и трассами. */
static void capture_free(gpointer data)
{
    Capture *capture = data;
    g_ptr_array_unref(capture->signals);
    g_hash_table_unref(capture->traces);
    g_free(capture->scale);
    g_free(capture);
}

/* EN: Read the next token before end and advance cursor. Return owned text or NULL.
 * RU: Читает токен до end и продвигает cursor. Возвращает новую строку или NULL. */
static gchar *word(const gchar **cursor, const gchar *end)
{
    const gchar *start = *cursor;
    while (start < end && g_ascii_isspace(*start))
        start++;
    const gchar *next = start;
    while (next < end && !g_ascii_isspace(*next))
        next++;
    *cursor = next;
    return next == start ? NULL : g_strndup(start, next - start);
}

/* EN: Parse unsigned decimal text into value and return whether it fits.
 * RU: Разбирает десятичный text в value и проверяет допустимость числа. */
static gboolean number(const gchar *text, guint64 *value)
{
    if (!text || !*text || strspn(text, "0123456789") != strlen(text))
        return FALSE;
    return g_ascii_string_to_unsigned(text, 10, 0, G_MAXUINT64, value, NULL);
}

/* EN: Parse length bytes of VCD into a new capture. Report invalid input in error.
 * RU: Разбирает length байт VCD из bytes. Ошибку формата записывает в error. */
static Capture *parse_capture(const gchar *bytes, gsize length, GError **error)
{
    Capture *capture = g_new0(Capture, 1);
    capture->signals = g_ptr_array_new_with_free_func(signal_free);
    capture->traces = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, trace_free);
    GPtrArray *scopes = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *fields = g_ptr_array_new_with_free_func(g_free);
    const gchar *cursor = bytes, *end = bytes + length;
    guint64 time = 0;
    guint events = 0;
    gsize value_bytes = 0;
    gboolean body = FALSE, valid = TRUE;
    const gchar *failure = NULL;
    gchar *token;
    while (valid && (token = word(&cursor, end)))
    {
        if (token[0] == '$')
        {
            gboolean dump = body && (!strcmp(token, "$dumpvars") || !strcmp(token, "$dumpall") ||
                                     !strcmp(token, "$dumpon") || !strcmp(token, "$dumpoff") ||
                                     !strcmp(token, "$end"));
            if (!dump)
            {
                gchar *field;
                while ((field = word(&cursor, end)) && strcmp(field, "$end"))
                {
                    g_ptr_array_add(fields, field);
                    if (fields->len > 65536)
                        break;
                }
                valid = field != NULL && fields->len <= 65536;
                if (valid)
                    g_free(field);
                if (valid && !body && !strcmp(token, "$scope"))
                {
                    valid = fields->len == 2 && scopes->len < 128;
                    if (valid)
                        g_ptr_array_add(scopes, g_strdup(fields->pdata[1]));
                }
                else if (valid && !body && !strcmp(token, "$upscope"))
                {
                    valid = scopes->len > 0;
                    if (valid)
                        g_ptr_array_remove_index(scopes, scopes->len - 1);
                }
                else if (valid && !body && !strcmp(token, "$timescale"))
                {
                    GString *scale = g_string_new(NULL);
                    for (guint i = 0; i < fields->len; i++)
                        g_string_append(scale, fields->pdata[i]);
                    g_free(capture->scale);
                    gchar *raw = g_string_free(scale, FALSE);
                    capture->scale = g_utf8_make_valid(raw, -1);
                    g_free(raw);
                }
                else if (valid && !body && !strcmp(token, "$var") && fields->len &&
                         strcmp(fields->pdata[0], "real") && strcmp(fields->pdata[0], "realtime") &&
                         strcmp(fields->pdata[0], "string"))
                {
                    guint64 width;
                    valid = fields->len >= 4 && capture->signals->len < VCD_SIGNALS &&
                            number(fields->pdata[1], &width) && width && width <= 4096;
                    if (capture->signals->len >= VCD_SIGNALS)
                        failure = "VCD exceeds the viewer limit of 4096 signal declarations.";
                    else if (fields->len >= 4 && number(fields->pdata[1], &width) && width > 4096)
                        failure = "VCD contains a signal wider than 4096 bits.";
                    if (valid)
                    {
                        Trace *trace = g_hash_table_lookup(capture->traces, fields->pdata[2]);
                        if (!trace)
                        {
                            trace = g_new0(Trace, 1);
                            trace->width = (guint)width;
                            trace->edges = g_array_new(FALSE, FALSE, sizeof(Edge));
                            g_array_set_clear_func(trace->edges, edge_clear);
                            g_hash_table_insert(capture->traces, g_strdup(fields->pdata[2]), trace);
                        }
                        if (trace->width != width)
                            valid = FALSE;
                        GString *name = g_string_new(NULL);
                        for (guint i = 0; i < scopes->len; i++)
                            g_string_append_printf(name, "%s.", (gchar *)scopes->pdata[i]);
                        Signal *signal = g_new(Signal, 1);
                        if (name->len)
                            signal->scope = g_utf8_make_valid(name->str, (gssize)name->len - 1);
                        else
                            signal->scope = g_strdup("");
                        signal->leaf = g_utf8_make_valid(fields->pdata[3], -1);
                        g_string_append(name, fields->pdata[3]);
                        gchar *full = g_string_free(name, FALSE);
                        signal->name = g_utf8_make_valid(full, -1);
                        g_free(full);
                        signal->trace = trace;
                        g_ptr_array_add(capture->signals, signal);
                    }
                }
                else if (valid && !body && !strcmp(token, "$enddefinitions"))
                {
                    body = scopes->len == 0;
                    valid = body;
                }
                g_ptr_array_set_size(fields, 0);
            }
        }
        else if (!body)
        {
            valid = FALSE;
        }
        else if (token[0] == '#')
        {
            guint64 next;
            valid =
                number(token + 1, &next) && next >= time && next <= G_GUINT64_CONSTANT(9007199254740991);
            if (valid)
                time = next;
        }
        else
        {
            gboolean vector = strchr("bBrRsS", token[0]) != NULL;
            gchar *code = vector ? word(&cursor, end) : NULL;
            Trace *trace = g_hash_table_lookup(capture->traces, code ? code : token + 1);
            if (trace && !strchr("rRsS", token[0]))
            {
                if (!vector)
                    token[1] = 0;
                gchar *value = g_ascii_strdown(vector ? token + 1 : token, -1);
                gsize n = strlen(value);
                valid = n && n <= trace->width && strspn(value, "01xz") == n && ++events <= VCD_EVENTS &&
                        (value_bytes += trace->width) <= VCD_BYTES;
                if (events > VCD_EVENTS)
                    failure = "VCD exceeds the viewer limit of 250000 value changes.";
                else if (value_bytes > VCD_BYTES)
                    failure = "Decoded VCD values exceed the viewer limit of 32 MiB.";
                if (valid)
                {
                    gchar *bits = g_malloc(trace->width + 1);
                    memset(bits, value[0] == 'x' || value[0] == 'z' ? value[0] : '0', trace->width - n);
                    memcpy(bits + trace->width - n, value, n + 1);
                    Edge edge = {time, bits};
                    if (trace->edges->len &&
                        g_array_index(trace->edges, Edge, trace->edges->len - 1).time == time)
                        g_array_remove_index(trace->edges, trace->edges->len - 1);
                    if (trace->edges->len &&
                        !strcmp(g_array_index(trace->edges, Edge, trace->edges->len - 1).value, bits))
                        g_free(bits);
                    else
                        g_array_append_val(trace->edges, edge);
                }
                g_free(value);
            }
            g_free(code);
        }
        g_free(token);
    }
    g_ptr_array_unref(scopes);
    g_ptr_array_unref(fields);
    if (!valid || !body || !capture->signals->len)
    {
        capture_free(capture);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            failure ? failure : !valid ? "Invalid or unsupported VCD data."
                                                       : "VCD contains no supported signal definitions.");
        return NULL;
    }
    capture->end = time;
    return capture;
}
