/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Drain stdout/stderr independently, retaining exact bounded stdout and a decoded combined display log.
 * RU: Раздельно читает stdout/stderr, сохраняя точный ограниченный stdout и общий декодированный журнал. */
typedef struct {
    CommandView *view;
    gboolean stdout_stream;
    GString *pending;
} CommandRead;

/* EN: Decode stream r bytes through chunk boundaries for display only; eof flushes incomplete UTF-8 as replacement glyphs.
 * RU: Декодирует bytes потока r с сохранением границ чанков только для показа; eof заменяет незавершённый UTF-8 символом замены. */
static void decode(CommandRead *r, const guint8 *bytes, gsize size, gboolean eof)
{
    g_string_append_len(r->pending, (const gchar *)bytes, size);
    GString *text = g_string_new(NULL);
    gsize i = 0;
    while (i < r->pending->len)
    {
        const gchar *p = r->pending->str + i;
        if (!*p)
        {
            g_string_append(text, "␀");
            i++;
            continue;
        }
        gunichar c = g_utf8_get_char_validated(p, r->pending->len - i);
        if (c == (gunichar)-2 && !eof)
            break;
        if (c == (gunichar)-1 || c == (gunichar)-2)
        {
            g_string_append(text, "�");
            i++;
        }
        else
        {
            gsize n = g_utf8_next_char(p) - p;
            g_string_append_len(text, p, n);
            i += n;
        }
    }
    g_string_erase(r->pending, 0, i);
    append(r->view, text->str, text->len);
    g_string_free(text, TRUE);
}

/* EN: Read the next chunk from stream for CommandRead data, enforcing combined output bounds and exact stdout retention.
 * result owns async completion; final EOF/cancellation releases the stream's view reference and contributes to finished().
 * RU: Читает следующий чанк stream для CommandRead data, ограничивая общий вывод и сохраняя точный stdout.
 * result содержит завершение; EOF/отмена освобождает ссылку окна потока и участвует в finished(). */
static void read_ready(GObject *stream, GAsyncResult *result, gpointer data)
{
    CommandRead *r = data;
    CommandView *v = r->view;
    GError *error = NULL;
    GBytes *bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(stream), result, &error);
    gsize size = 0;
    const guint8 *p = bytes ? g_bytes_get_data(bytes, &size) : NULL;
    if (!v->closed && !v->stopped && size)
    {
        gsize keep = MIN(size, OUTPUT_LIMIT - v->received);
        for (gsize i = 0; i < keep; i++)
        {
            if (p[i] == '\n')
            {
                v->line_bytes = 0;
                v->lines++;
            }
            else
                v->line_bytes++;
            if (v->lines >= 50000 || v->line_bytes >= 65536)
            {
                keep = i + 1;
                v->limited = TRUE;
                break;
            }
        }
        if (r->stdout_stream && !v->stdout_lost)
        {
            if (v->stdout_bytes->len + keep <= 1024u * 1024u)
                g_byte_array_append(v->stdout_bytes, p, keep);
            else
            {
                v->stdout_lost = TRUE;
                g_byte_array_set_size(v->stdout_bytes, 0);
            }
        }
        decode(r, p, keep, FALSE);
        v->received += keep;
        if (keep < size || v->limited)
        {
            v->limited = TRUE;
            v->stopped = TRUE;
            g_subprocess_force_exit(v->process);
            g_cancellable_cancel(v->cancel);
        }
    }
    if (bytes)
        g_bytes_unref(bytes);
    if (size && !v->closed && !v->stopped && !error)
    {
        g_input_stream_read_bytes_async(G_INPUT_STREAM(stream), 8192, G_PRIORITY_DEFAULT, v->cancel, read_ready, r);
        return;
    }
    if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) && v->process)
    {
        v->stopped = TRUE;
        v->stdout_lost = TRUE;
        g_subprocess_force_exit(v->process);
        g_cancellable_cancel(v->cancel);
    }
    if (!v->closed)
        decode(r, (const guint8 *)"", 0, TRUE);
    g_clear_error(&error);
    v->reading = --v->readers != 0;
    finished(v);
    g_string_free(r->pending, TRUE);
    g_free(r);
    view_unref(v);
}

/* EN: Start one retained asynchronous read for stream; stdout_stream selects exact stdout capture.
 * RU: Запускает удерживаемое отложенное чтение stream; stdout_stream включает точный захват stdout. */
static void output_read_start(CommandView *v, GInputStream *stream, gboolean stdout_stream)
{
    CommandRead *r = g_new0(CommandRead, 1);
    r->view = view_ref(v);
    r->stdout_stream = stdout_stream;
    r->pending = g_string_new(NULL);
    g_input_stream_read_bytes_async(stream, 8192, G_PRIORITY_DEFAULT, v->cancel, read_ready, r);
}
