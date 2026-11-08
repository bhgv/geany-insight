/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Visible tracks, cursor values and edge navigation.
 * RU: Видимые дорожки, значения курсора и переходы по фронтам.
 */

/* EN: Return the current signal in viewer, or NULL if none is selected.
 * RU: Возвращает текущий сигнал viewer или NULL при отсутствии выбора. */
static Signal *selected(Viewer *viewer)
{
    gint index = viewer->active;
    return index < 0 ? NULL : viewer->capture->signals->pdata[index];
}

/* EN: Find the position of signal in viewer's pinned tracks, or return -1.
 * RU: Находит позицию signal в закреплённых дорожках viewer или возвращает -1. */
static gint pinned_index(Viewer *viewer, gint signal)
{
    for (guint i = 0; i < viewer->tracks->len; i++)
        if (g_array_index(viewer->tracks, gint, i) == signal)
            return (gint)i;
    return -1;
}

/* EN: Count visible tracks in viewer, including active if it is not pinned.
 * RU: Считает видимые дорожки viewer, включая незакреплённый active. */
static guint track_count(Viewer *viewer, gint active)
{
    return viewer->tracks->len + (active >= 0 && pinned_index(viewer, active) < 0);
}

/* EN: Resolve visible track in viewer to a signal index, using active for the extra track.
 * RU: Находит индекс сигнала дорожки track в viewer, используя active для дополнительной дорожки. */
static gint track_signal(Viewer *viewer, guint track, gint active)
{
    return track < viewer->tracks->len ? g_array_index(viewer->tracks, gint, track) : active;
}

/* EN: Return the index of the first edge in trace strictly after time.
 * RU: Возвращает индекс первого перехода trace строго после time. */
static guint upper_bound(const Trace *trace, guint64 time)
{
    guint lo = 0, hi = trace->edges->len;
    while (lo < hi)
    {
        guint mid = lo + (hi - lo) / 2;
        if (g_array_index(trace->edges, Edge, mid).time <= time)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* EN: Return owned display text for bits, choosing hex when hexadecimal is true.
 * RU: Возвращает новую строку для bits. При hexadecimal использует шестнадцатеричный вид. */
static gchar *format_bits(const gchar *bits, gboolean hexadecimal)
{
    if (!hexadecimal || !*bits || strspn(bits, "01xz") != strlen(bits))
        return g_strdup(bits);
    GString *out = g_string_new("0x");
    gsize size = strlen(bits);
    for (gsize at = 0; at < size;)
    {
        gsize group = at == 0 && size % 4 ? size % 4 : 4;
        guint value = 0;
        gboolean binary = TRUE, all_x = TRUE, all_z = TRUE;
        for (gsize i = 0; i < group; i++)
        {
            gchar bit = bits[at + i];
            value = value * 2 + (bit == '1');
            binary = binary && (bit == '0' || bit == '1');
            all_x = all_x && bit == 'x';
            all_z = all_z && bit == 'z';
        }
        if (binary)
            g_string_append_c(out, "0123456789abcdef"[value]);
        else if (all_x || all_z)
            g_string_append_c(out, all_x ? 'x' : 'z');
        else
            g_string_append_printf(out, "[%.*s]", (gint)group, bits + at);
        at += group;
    }
    return g_string_free(out, FALSE);
}

/* EN: Update the A/B markers and their tick difference in viewer.
 * RU: Обновляет отметки A/B и разность тактов в viewer. */
static void update_markers(Viewer *viewer)
{
    GString *line = g_string_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(viewer->marker); i++)
    {
        if (viewer->marker_set[i])
            g_string_append_printf(line, "%c: %" G_GUINT64_FORMAT "    ", 'A' + i, viewer->marker[i]);
    }
    if (viewer->marker_set[0] && viewer->marker_set[1])
    {
        guint64 low = MIN(viewer->marker[0], viewer->marker[1]);
        guint64 high = MAX(viewer->marker[0], viewer->marker[1]);
        g_string_append_printf(line, "Δ: %" G_GUINT64_FORMAT " ticks", high - low);
    }
    gtk_label_set_text(GTK_LABEL(viewer->markers), line->str);
    g_string_free(line, TRUE);
}

/* EN: Show the values of viewer's visible tracks at the current cursor tick.
 * RU: Показывает значения видимых дорожек viewer на текущем такте курсора. */
static void update_watch_values(Viewer *viewer)
{
    gtk_list_store_clear(viewer->watch_values);
    for (guint lane = 0; lane < track_count(viewer, viewer->active); lane++)
    {
        gint index = track_signal(viewer, lane, viewer->active);
        const Signal *signal = viewer->capture->signals->pdata[index];
        guint at = upper_bound(signal->trace, viewer->cursor);
        const Edge *edge = at ? &g_array_index(signal->trace->edges, Edge, at - 1) : NULL;
        gchar *value = format_bits(edge ? edge->value : "unknown", viewer->hexadecimal);
        gchar *tick = edge ? g_strdup_printf("%" G_GUINT64_FORMAT, edge->time) : g_strdup("—");
        gtk_list_store_insert_with_values(viewer->watch_values, NULL, -1, 0, signal->name, 1, value, 2,
                                          tick, 3, edge && edge->time == viewer->cursor ? "Yes" : "", -1);
        g_free(tick);
        g_free(value);
    }
}

/* EN: Find the next or previous edge of trace from cursor. forward selects direction and time receives the tick.
 * RU: Находит соседний переход trace от cursor. forward задаёт направление, time получает такт. */
static gboolean wave_adjacent_event(const Trace *trace, guint64 cursor, gboolean forward, guint64 *time)
{
    guint at = upper_bound(trace, cursor);
    if (forward)
    {
        if (at == trace->edges->len)
            return FALSE;
        *time = g_array_index(trace->edges, Edge, at).time;
    }
    else
    {
        if (at && g_array_index(trace->edges, Edge, at - 1).time == cursor)
            at--;
        if (!at)
            return FALSE;
        *time = g_array_index(trace->edges, Edge, at - 1).time;
    }
    return TRUE;
}

/* EN: Move viewer's cursor to the nearest visible edge in the direction given by forward.
 * RU: Перемещает курсор viewer к ближайшему видимому фронту в направлении forward. */
static void wave_step_visible(Viewer *viewer, gboolean forward)
{
    gboolean found = FALSE;
    guint64 target = 0;
    for (guint lane = 0; lane < track_count(viewer, viewer->active); lane++)
    {
        gint index = track_signal(viewer, lane, viewer->active);
        const Signal *signal = viewer->capture->signals->pdata[index];
        guint64 time;
        if (wave_adjacent_event(signal->trace, viewer->cursor, forward, &time) &&
            (!found || (forward ? time < target : time > target)))
        {
            target = time;
            found = TRUE;
        }
    }
    if (found)
        viewer->cursor = target;
}
