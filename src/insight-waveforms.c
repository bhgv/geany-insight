/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-waveforms.h"
#include "../insight/desktop/glib_compat.h"
#include "../insight/core/runtime.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#define VCD_BYTES (32u * 1024u * 1024u)
#define VCD_SIGNALS 4096u
#define VCD_EVENTS 250000u
#define WAVE_TRACKS 16u
#define TRACK_HEIGHT 64
#define TRACK_LABEL_WIDTH 220

typedef struct { guint64 time; gchar *value; } Edge;
typedef struct { GArray *edges; guint width; } Trace;
typedef struct { gchar *name, *scope, *leaf; Trace *trace; } Signal;
typedef struct {
 GPtrArray *signals;
 GHashTable *traces;
 guint64 end;
 gchar *scale;
} Capture;
typedef struct {
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

static void edge_clear(gpointer data) { g_free(((Edge *)data)->value); }
static void trace_free(gpointer data) {
 Trace *trace = data;
 g_array_unref(trace->edges);
 g_free(trace);
}
static void signal_free(gpointer data) {
 Signal *signal = data;
 g_free(signal->name);
 g_free(signal->scope);
 g_free(signal->leaf);
 g_free(signal);
}
static void capture_free(gpointer data) {
 Capture *capture = data;
 g_ptr_array_unref(capture->signals);
 g_hash_table_unref(capture->traces);
 g_free(capture->scale);
 g_free(capture);
}

static gchar *word(const gchar **cursor, const gchar *end) {
 const gchar *start = *cursor;
 while (start < end && g_ascii_isspace(*start)) start++;
 const gchar *next = start;
 while (next < end && !g_ascii_isspace(*next)) next++;
 *cursor = next;
 return next == start ? NULL : g_strndup(start, next - start);
}

static gboolean number(const gchar *text, guint64 *value) {
 if (!text || !*text || strspn(text, "0123456789") != strlen(text))
  return FALSE;
 return g_ascii_string_to_unsigned(text, 10, 0, G_MAXUINT64, value, NULL);
}

static Capture *parse_capture(const gchar *bytes, gsize length, GError **error) {
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
 gchar *token;
 while (valid && (token = word(&cursor, end))) {
  if (token[0] == '$') {
   gboolean dump = body && (!strcmp(token, "$dumpvars") || !strcmp(token, "$dumpall") ||
                            !strcmp(token, "$dumpon") || !strcmp(token, "$dumpoff") ||
                            !strcmp(token, "$end"));
   if (!dump) {
    gchar *field;
    while ((field = word(&cursor, end)) && strcmp(field, "$end")) {
     g_ptr_array_add(fields, field);
     if (fields->len > 65536) break;
    }
    valid = field != NULL && fields->len <= 65536;
    if (valid) g_free(field);
    if (valid && !body && !strcmp(token, "$scope")) {
     valid = fields->len == 2 && scopes->len < 128;
     if (valid) g_ptr_array_add(scopes, g_strdup(fields->pdata[1]));
    } else if (valid && !body && !strcmp(token, "$upscope")) {
     valid = scopes->len > 0;
     if (valid) g_ptr_array_remove_index(scopes, scopes->len - 1);
    } else if (valid && !body && !strcmp(token, "$timescale")) {
     GString *scale = g_string_new(NULL);
     for (guint i = 0; i < fields->len; i++) g_string_append(scale, fields->pdata[i]);
     g_free(capture->scale);
     gchar *raw = g_string_free(scale, FALSE);
     capture->scale = g_utf8_make_valid(raw, -1);
     g_free(raw);
    } else if (valid && !body && !strcmp(token, "$var") && fields->len &&
               strcmp(fields->pdata[0], "real") && strcmp(fields->pdata[0], "realtime") &&
               strcmp(fields->pdata[0], "string")) {
     guint64 width;
     valid = fields->len >= 4 && capture->signals->len < VCD_SIGNALS &&
             number(fields->pdata[1], &width) && width && width <= 4096;
     if (valid) {
      Trace *trace = g_hash_table_lookup(capture->traces, fields->pdata[2]);
      if (!trace) {
       trace = g_new0(Trace, 1);
       trace->width = (guint)width;
       trace->edges = g_array_new(FALSE, FALSE, sizeof(Edge));
       g_array_set_clear_func(trace->edges, edge_clear);
       g_hash_table_insert(capture->traces, g_strdup(fields->pdata[2]), trace);
      }
      if (trace->width != width) valid = FALSE;
      GString *name = g_string_new(NULL);
      for (guint i = 0; i < scopes->len; i++) g_string_append_printf(name, "%s.", (gchar *)scopes->pdata[i]);
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
    } else if (valid && !body && !strcmp(token, "$enddefinitions")) {
     body = scopes->len == 0;
     valid = body;
    }
    g_ptr_array_set_size(fields, 0);
   }
  } else if (!body) {
   valid = FALSE;
  } else if (token[0] == '#') {
   guint64 next;
   valid = number(token + 1, &next) && next >= time && next <= G_GUINT64_CONSTANT(9007199254740991);
   if (valid) time = next;
  } else {
   gboolean vector = strchr("bBrRsS", token[0]) != NULL;
   gchar *code = vector ? word(&cursor, end) : NULL;
   Trace *trace = g_hash_table_lookup(capture->traces, code ? code : token + 1);
   if (trace && !strchr("rRsS", token[0])) {
    if (!vector) token[1] = 0;
    gchar *value = g_ascii_strdown(vector ? token + 1 : token, -1);
    gsize n = strlen(value);
    valid = n && n <= trace->width && strspn(value, "01xz") == n &&
            ++events <= VCD_EVENTS && (value_bytes += trace->width) <= VCD_BYTES;
    if (valid) {
     gchar *bits = g_malloc(trace->width + 1);
     memset(bits, value[0] == 'x' || value[0] == 'z' ? value[0] : '0', trace->width - n);
     memcpy(bits + trace->width - n, value, n + 1);
     Edge edge = {time, bits};
     if (trace->edges->len && g_array_index(trace->edges, Edge, trace->edges->len - 1).time == time)
      g_array_remove_index(trace->edges, trace->edges->len - 1);
     if (trace->edges->len && !strcmp(g_array_index(trace->edges, Edge, trace->edges->len - 1).value, bits))
      g_free(bits);
     else g_array_append_val(trace->edges, edge);
    }
    g_free(value);
   }
   g_free(code);
  }
  g_free(token);
 }
 g_ptr_array_unref(scopes);
 g_ptr_array_unref(fields);
 if (!valid || !body || !capture->signals->len) {
  capture_free(capture);
  g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                      "Invalid VCD or capture exceeds the viewer limits");
  return NULL;
 }
 capture->end = time;
 return capture;
}

static Signal *selected(Viewer *viewer) {
 gint index = viewer->active;
 return index < 0 ? NULL : viewer->capture->signals->pdata[index];
}

static gint pinned_index(Viewer *viewer, gint signal) {
 for (guint i = 0; i < viewer->tracks->len; i++)
  if (g_array_index(viewer->tracks, gint, i) == signal) return (gint)i;
 return -1;
}

static guint track_count(Viewer *viewer, gint active) {
 return viewer->tracks->len + (active >= 0 && pinned_index(viewer, active) < 0);
}

static gint track_signal(Viewer *viewer, guint track, gint active) {
 return track < viewer->tracks->len ? g_array_index(viewer->tracks, gint, track) : active;
}

static guint upper_bound(const Trace *trace, guint64 time) {
 guint lo = 0, hi = trace->edges->len;
 while (lo < hi) {
  guint mid = lo + (hi - lo) / 2;
  if (g_array_index(trace->edges, Edge, mid).time <= time) lo = mid + 1;
  else hi = mid;
 }
 return lo;
}

static gchar *format_bits(const gchar *bits, gboolean hexadecimal) {
 if (!hexadecimal || !*bits || strspn(bits, "01xz") != strlen(bits))
  return g_strdup(bits);
 GString *out = g_string_new("0x");
 gsize size = strlen(bits);
 for (gsize at = 0; at < size;) {
  gsize group = at == 0 && size % 4 ? size % 4 : 4;
  guint value = 0;
  gboolean binary = TRUE, all_x = TRUE, all_z = TRUE;
  for (gsize i = 0; i < group; i++) {
   gchar bit = bits[at + i];
   value = value * 2 + (bit == '1');
   binary = binary && (bit == '0' || bit == '1');
   all_x = all_x && bit == 'x';
   all_z = all_z && bit == 'z';
  }
  if (binary) g_string_append_c(out, "0123456789abcdef"[value]);
  else if (all_x || all_z) g_string_append_c(out, all_x ? 'x' : 'z');
  else g_string_append_printf(out, "[%.*s]", (gint)group, bits + at);
  at += group;
 }
 return g_string_free(out, FALSE);
}

static void update_markers(Viewer *viewer) {
 GString *line = g_string_new(NULL);
 for (guint i = 0; i < G_N_ELEMENTS(viewer->marker); i++) {
  if (viewer->marker_set[i])
   g_string_append_printf(line, "%c: %" G_GUINT64_FORMAT "    ", 'A' + i, viewer->marker[i]);
 }
 if (viewer->marker_set[0] && viewer->marker_set[1]) {
  guint64 low = MIN(viewer->marker[0], viewer->marker[1]);
  guint64 high = MAX(viewer->marker[0], viewer->marker[1]);
  g_string_append_printf(line, "Δ: %" G_GUINT64_FORMAT " ticks", high - low);
 }
 gtk_label_set_text(GTK_LABEL(viewer->markers), line->str);
 g_string_free(line, TRUE);
}

/* List the values of all visible signals at the same waveform tick. */
/* Показывает значения всех видимых сигналов на одном такте диаграммы. */
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
        gtk_list_store_insert_with_values(viewer->watch_values, NULL, -1,
            0, signal->name, 1, value, 2, tick,
            3, edge && edge->time == viewer->cursor ? "Yes" : "", -1);
        g_free(tick);
        g_free(value);
    }
}

/* Find the closest recorded event on one trace in the chosen direction. */
/* Ищет ближайшее записанное событие дорожки в выбранном направлении. */
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

/* Step through the union of the displayed signal event times. */
/* Шагает по объединению времён событий видимых сигналов. */
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

static Trace *instrument_trace(Viewer *viewer, guint axis)
{
    gint index = gtk_combo_box_get_active(GTK_COMBO_BOX(viewer->instrument_signal[axis])) - 1;
    if (index < 0)
        return NULL;
    Signal *signal = viewer->capture->signals->pdata[index];
    return signal->trace;
}

static const gchar *instrument_bits(const Trace *trace, guint64 tick)
{
    guint at = upper_bound(trace, tick);
    return at ? g_array_index(trace->edges, Edge, at - 1).value : NULL;
}

static gboolean instrument_number(const gchar *bits, guint64 *number)
{
    if (!bits || !*bits || strlen(bits) > 64)
        return FALSE;
    *number = 0;
    for (const gchar *bit = bits; *bit; bit++)
    {
        if (*bit != '0' && *bit != '1')
            return FALSE;
        *number = (*number << 1) | (*bit == '1');
    }
    return TRUE;
}

static void instruments_update(Viewer *viewer)
{
    if (!viewer->instruments)
        return;
    for (guint i = 0; i < G_N_ELEMENTS(viewer->instrument_signal); i++)
    {
        Trace *trace = instrument_trace(viewer, i);
        const gchar *bits = trace ? instrument_bits(trace, viewer->cursor) : NULL;
        guint64 number;
        gboolean numeric = instrument_number(bits, &number);
        const gchar *lamp = !trace || trace->width != 1 ? " " :
            !numeric ? "<span foreground='#a56818'>◉</span>" :
            number ? "<span foreground='#228447'>●</span>" : "○";
        gtk_label_set_markup(GTK_LABEL(viewer->instrument_lamp[i]), lamp);
        gchar *value = numeric ? g_strdup_printf("%" G_GUINT64_FORMAT "  (0x%" G_GINT64_MODIFIER "x)",
            number, number) : g_strdup_printf("%.100s", bits ? bits : "—");
        gtk_label_set_text(GTK_LABEL(viewer->instrument_value[i]), value);
        g_free(value);
    }
    gtk_widget_queue_draw(viewer->instrument_plot);
}

#define INSTRUMENT_POINTS 2048u

typedef struct {
    guint64 values[3];
    gboolean valid;
} InstrumentPoint;

static InstrumentPoint instrument_point(Trace *const *traces, guint axes, guint64 tick)
{
    InstrumentPoint point = {{0}, TRUE};
    for (guint i = 0; i < axes; i++)
        if (!instrument_number(instrument_bits(traces[i], tick), &point.values[i]))
            point.valid = FALSE;
    return point;
}

static void instrument_project(const InstrumentPoint *point, const guint64 *low,
                               const guint64 *high, guint axes, double *x, double *y)
{
    double v[3] = {0};
    for (guint i = 0; i < axes; i++)
        v[i] = high[i] == low[i] ? 0.5 :
            (double)(point->values[i] - low[i]) / (double)(high[i] - low[i]);
    *x = axes == 3 ? 0.72 * v[0] + 0.28 * v[2] : v[0];
    *y = axes == 3 ? 0.72 * v[1] + 0.28 * v[2] : v[1];
}

static gboolean instruments_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    Viewer *viewer = data;
    Trace *traces[] = {instrument_trace(viewer, 0), instrument_trace(viewer, 1),
                       instrument_trace(viewer, 2)};
    if (!traces[0] || !traces[1])
    {
        gtk_label_set_text(GTK_LABEL(viewer->instrument_range), "Choose X and Y for an XY plot. Add Z for a projection.");
        return FALSE;
    }
    guint axes = traces[2] ? 3 : 2;
    guint64 end = viewer->start + MIN(viewer->span, viewer->capture->end - viewer->start);
    guint64 low[] = {G_MAXUINT64, G_MAXUINT64, G_MAXUINT64};
    guint64 high[] = {0, 0, 0};
    InstrumentPoint *points = g_new(InstrumentPoint, INSTRUMENT_POINTS);
    guint next[3];
    guint count = 0, known = 0;
    guint64 tick = viewer->start;
    for (guint i = 0; i < axes; i++)
        next[i] = upper_bound(traces[i], tick);
    gboolean truncated = FALSE;
    for (;;)
    {
        InstrumentPoint point = instrument_point(traces, axes, tick);
        points[count++] = point;
        if (point.valid)
        {
            known++;
            for (guint i = 0; i < axes; i++)
            {
                low[i] = MIN(low[i], point.values[i]);
                high[i] = MAX(high[i], point.values[i]);
            }
        }
        guint64 following = G_MAXUINT64;
        gboolean found = FALSE;
        for (guint i = 0; i < axes; i++)
            if (next[i] < traces[i]->edges->len)
            {
                following = MIN(following, g_array_index(traces[i]->edges, Edge, next[i]).time);
                found = TRUE;
            }
        if (!found || following > end)
            break;
        if (count == INSTRUMENT_POINTS)
        {
            truncated = TRUE;
            break;
        }
        tick = following;
        for (guint i = 0; i < axes; i++)
            while (next[i] < traces[i]->edges->len &&
                   g_array_index(traces[i]->edges, Edge, next[i]).time == tick)
                next[i]++;
    }
    double width = MAX(1, gtk_widget_get_allocated_width(widget) - 32);
    double height = MAX(1, gtk_widget_get_allocated_height(widget) - 32);
    GdkRGBA color;
    gtk_style_context_get_color(gtk_widget_get_style_context(widget), gtk_widget_get_state_flags(widget), &color);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.35);
    cairo_rectangle(cr, 16, 16, width, height);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, .18, .52, .82);
    gboolean previous = FALSE;
    for (guint i = 0; i < count; i++)
    {
        if (!points[i].valid)
        {
            previous = FALSE;
            continue;
        }
        double x, y;
        instrument_project(&points[i], low, high, axes, &x, &y);
        x = 16 + x * width;
        y = 16 + (1 - y) * height;
        if (previous)
            cairo_line_to(cr, x, y);
        else
        {
            cairo_move_to(cr, x, y);
            cairo_line_to(cr, x + 0.5, y);
            cairo_move_to(cr, x, y);
        }
        previous = TRUE;
    }
    cairo_stroke(cr);
    if (known && viewer->cursor >= viewer->start && viewer->cursor <= (truncated ? tick : end))
    {
        InstrumentPoint cursor = instrument_point(traces, axes, viewer->cursor);
        if (cursor.valid)
        {
            double x, y;
            instrument_project(&cursor, low, high, axes, &x, &y);
            cairo_set_source_rgb(cr, .91, .48, .13);
            cairo_arc(cr, 16 + x * width, 16 + (1 - y) * height, 4, 0, 2 * G_PI);
            cairo_fill(cr);
        }
    }
    GString *summary = g_string_new(axes == 3 ? "XYZ projection" : "XY plot");
    if (known)
    {
        const gchar *axis[] = {"X", "Y", "Z"};
        for (guint i = 0; i < axes; i++)
            g_string_append_printf(summary, "   %s: %" G_GUINT64_FORMAT "–%" G_GUINT64_FORMAT,
                                   axis[i], low[i], high[i]);
    }
    else
        g_string_append(summary, "   No known numeric samples");
    if (truncated)
        g_string_append(summary, "   First 2048 samples; zoom in for detail");
    gtk_label_set_text(GTK_LABEL(viewer->instrument_range), summary->str);
    g_string_free(summary, TRUE);
    g_free(points);
    return FALSE;
}

static void instruments_changed(GtkComboBox *combo, gpointer data)
{
    Viewer *viewer = data;
    guint axis = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(combo), "instrument-axis"));
    viewer->instrument_index[axis] = gtk_combo_box_get_active(combo);
    instruments_update(viewer);
}

static void instruments_closed(GtkWidget *widget, gpointer data)
{
    Viewer *viewer = data;
    (void)widget;
    viewer->instruments = NULL;
}

static void instruments_show(GtkButton *button, gpointer data)
{
    (void)button;
    Viewer *viewer = data;
    if (viewer->instruments)
    {
        gtk_window_present(GTK_WINDOW(viewer->instruments));
        return;
    }
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Signal Instruments", GTK_WINDOW(viewer->window),
        GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    viewer->instruments = dialog;
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    g_signal_connect(dialog, "destroy", G_CALLBACK(instruments_closed), viewer);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 6);
    const gchar *labels[] = {"X", "Y", "Z"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *combo = gtk_combo_box_text_new();
        viewer->instrument_signal[i] = combo;
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "None");
        for (guint j = 0; j < viewer->capture->signals->len; j++)
        {
            Signal *signal = viewer->capture->signals->pdata[j];
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), signal->name);
        }
        gchar *name = g_strdup_printf("insight-instrument-%s", labels[i]);
        gtk_widget_set_name(combo, name);
        g_free(name);
        g_object_set_data(G_OBJECT(combo), "instrument-axis", GUINT_TO_POINTER(i));
        gint selected_index = viewer->instrument_index[i];
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo),
            selected_index >= 0 ? selected_index : i == 0 ? viewer->active + 1 : 0);
        viewer->instrument_lamp[i] = gtk_label_new(NULL);
        viewer->instrument_value[i] = gtk_label_new(NULL);
        gtk_label_set_selectable(GTK_LABEL(viewer->instrument_value[i]), TRUE);
        gtk_widget_set_hexpand(combo, TRUE);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new(labels[i]), 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), combo, 1, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), viewer->instrument_lamp[i], 2, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), viewer->instrument_value[i], 3, i, 1, 1);
    }
    viewer->instrument_plot = gtk_drawing_area_new();
    gtk_widget_set_size_request(viewer->instrument_plot, 200, 180);
    gtk_box_pack_start(GTK_BOX(box), viewer->instrument_plot, TRUE, TRUE, 0);
    viewer->instrument_range = gtk_label_new(NULL);
    gtk_widget_set_name(viewer->instrument_range, "insight-instrument-range");
    gtk_label_set_line_wrap(GTK_LABEL(viewer->instrument_range), TRUE);
    gtk_box_pack_start(GTK_BOX(box), viewer->instrument_range, FALSE, FALSE, 4);
    g_signal_connect(viewer->instrument_plot, "draw", G_CALLBACK(instruments_draw), viewer);
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
        g_signal_connect(viewer->instrument_signal[i], "changed", G_CALLBACK(instruments_changed), viewer);
    instruments_update(viewer);
    gtk_widget_show_all(dialog);
}

static void update(Viewer *viewer) {
 Signal *signal = selected(viewer);
 if (!signal) return;
 guint at = upper_bound(signal->trace, viewer->cursor);
 gchar *value = format_bits(at ? g_array_index(signal->trace->edges, Edge, at - 1).value : "unknown",
                            viewer->hexadecimal);
 gchar *label = g_strdup_printf("Tick %" G_GUINT64_FORMAT " (%s)   View: %" G_GUINT64_FORMAT
     "–%" G_GUINT64_FORMAT "   Value: %.128s%s", viewer->cursor,
     viewer->capture->scale ? viewer->capture->scale : "no timescale", viewer->start,
     MIN(viewer->capture->end, viewer->start + viewer->span), value, strlen(value) > 128 ? "…" : "");
 gtk_label_set_text(GTK_LABEL(viewer->value), label);
 g_free(label);
 g_free(value);
 update_markers(viewer);
 update_watch_values(viewer);
 instruments_update(viewer);
 gint active = viewer->active;
 gboolean pinned = pinned_index(viewer, active) >= 0;
 gtk_button_set_label(GTK_BUTTON(viewer->pin), pinned ? "Unpin track" : "Pin track");
 gtk_widget_set_sensitive(viewer->pin, pinned || viewer->tracks->len < WAVE_TRACKS);
 gtk_widget_set_size_request(viewer->canvas, -1, TRACK_HEIGHT * track_count(viewer, active));
 gtk_widget_queue_draw(viewer->canvas);
}

static void draw_trace(Viewer *viewer, Trace *trace, cairo_t *cr, double width) {
 const double height = TRACK_HEIGHT;
 guint64 stop = viewer->start + viewer->span;
 guint next = upper_bound(trace, viewer->start);
 guint64 time = viewer->start;
 double previous = -1;
 while (time < stop) {
  const gchar *bits = next ? g_array_index(trace->edges, Edge, next - 1).value : "x";
  guint64 until = next < trace->edges->len ? MIN(stop, g_array_index(trace->edges, Edge, next).time) : stop;
  /* Condense transitions closer than one pixel, keeping the full time span. */
  guint64 pixel = MAX(G_GUINT64_CONSTANT(1), (guint64)(viewer->span / MAX(1., width)));
  gboolean dense = until < stop && until - time < pixel;
  if (dense) until = MIN(stop, time + pixel);
  double x = (double)(time - viewer->start) / viewer->span * width;
  double right = (double)(until - viewer->start) / viewer->span * width;
  gboolean unknown = strpbrk(bits, "xz") != NULL;
  cairo_set_source_rgb(cr, unknown ? 0.9 : 0.12, unknown ? 0.54 : 0.62, 0.75);
  if (dense) {
   cairo_rectangle(cr, x, height * .2, MAX(1, right - x), height * .6);
   cairo_fill(cr);
   previous = -1;
  } else if (trace->width > 1) {
   cairo_rectangle(cr, x, height * .26, MAX(1, right - x), height * .48);
   cairo_stroke(cr);
   if (right - x > 70) {
    gchar *label = format_bits(bits, viewer->hexadecimal);
    if (strlen(label) > 12) label[12] = '\0';
    cairo_move_to(cr, x + 4, height * .55);
    cairo_show_text(cr, label);
    g_free(label);
   }
  } else {
   double y = height * (unknown ? .5 : bits[0] == '1' ? .25 : .75);
   cairo_move_to(cr, x, previous < 0 ? y : previous);
   cairo_line_to(cr, x, y);
   cairo_line_to(cr, right, y);
   cairo_stroke(cr);
   previous = y;
  }
  time = until;
  next = dense ? upper_bound(trace, time) : next + 1;
 }
 for (guint i = 0; i < G_N_ELEMENTS(viewer->marker); i++) {
  guint64 tick = viewer->marker[i];
  if (!viewer->marker_set[i] || tick < viewer->start || tick > stop) continue;
  double x = (double)(tick - viewer->start) / viewer->span * width;
  cairo_set_source_rgb(cr, i ? .58 : .20, i ? .28 : .65, .76);
  const double dash[] = {4, 3};
  cairo_set_dash(cr, dash, G_N_ELEMENTS(dash), 0);
  cairo_move_to(cr, x, 0);
  cairo_line_to(cr, x, height);
  cairo_stroke(cr);
  cairo_set_dash(cr, NULL, 0, 0);
  cairo_move_to(cr, x + 3, 12);
  cairo_show_text(cr, i ? "B" : "A");
 }
 if (viewer->cursor >= viewer->start && viewer->cursor <= stop) {
  double x = (double)(viewer->cursor - viewer->start) / viewer->span * width;
  cairo_set_source_rgb(cr, .85, .28, .25);
  cairo_move_to(cr, x, 0);
  cairo_line_to(cr, x, height);
  cairo_stroke(cr);
 }
}

static gboolean draw_wave(GtkWidget *widget, cairo_t *cr, gpointer data) {
 Viewer *viewer = data;
 gint active = viewer->active;
 double width = MAX(1, gtk_widget_get_allocated_width(widget) - TRACK_LABEL_WIDTH);
 guint count = track_count(viewer, active);
 for (guint lane = 0; lane < count; lane++) {
  gint index = track_signal(viewer, lane, active);
  Signal *signal = viewer->capture->signals->pdata[index];
  cairo_save(cr);
  cairo_translate(cr, 0, lane * TRACK_HEIGHT);
  cairo_set_source_rgba(cr, .35, .48, .6, index == active ? .25 : .10);
  cairo_rectangle(cr, 0, 0, width + TRACK_LABEL_WIDTH, TRACK_HEIGHT - 1);
  cairo_fill(cr);
  cairo_save(cr);
  cairo_rectangle(cr, 0, 0, TRACK_LABEL_WIDTH - 6, TRACK_HEIGHT);
  cairo_clip(cr);
  cairo_set_source_rgb(cr, .18, .48, .65);
  cairo_move_to(cr, 8, 22);
  cairo_show_text(cr, signal->name);
  guint at = upper_bound(signal->trace, viewer->cursor);
  const gchar *bits = at ? g_array_index(signal->trace->edges, Edge, at - 1).value : "unknown";
  gchar *formatted = format_bits(bits, viewer->hexadecimal);
  gchar *value = g_strdup_printf("[%u] %.26s%s", signal->trace->width, formatted,
                                strlen(formatted) > 26 ? "…" : "");
  g_free(formatted);
  cairo_move_to(cr, 8, 44);
  cairo_show_text(cr, value);
  g_free(value);
  cairo_restore(cr);
  cairo_translate(cr, TRACK_LABEL_WIDTH, 0);
  cairo_rectangle(cr, 0, 0, width, TRACK_HEIGHT);
  cairo_clip(cr);
  draw_trace(viewer, signal->trace, cr, width);
  cairo_restore(cr);
 }
 return FALSE;
}

static void refresh_selector(Viewer *viewer);
static void activate_signal(Viewer *viewer, gint index);

static gboolean click_wave(GtkWidget *widget, GdkEventButton *event, gpointer data) {
 Viewer *viewer = data;
 if (event->button != 1 || event->y < 0) return FALSE;
 gint active = viewer->active;
 guint lane = (guint)event->y / TRACK_HEIGHT;
 if (lane >= track_count(viewer, active)) return FALSE;
 gint signal = track_signal(viewer, lane, active);
 if (signal != active) {
  activate_signal(viewer, signal);
  gchar id[16];
  g_snprintf(id, sizeof id, "%d", signal);
  viewer->rebuilding = TRUE;
  if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(viewer->selector), id))
   gtk_combo_box_set_active(GTK_COMBO_BOX(viewer->selector), -1);
  viewer->rebuilding = FALSE;
 }
 if (event->x >= TRACK_LABEL_WIDTH) {
  double width = MAX(1, gtk_widget_get_allocated_width(widget) - TRACK_LABEL_WIDTH);
  double fraction = CLAMP((event->x - TRACK_LABEL_WIDTH) / width, 0., 1.);
  viewer->cursor = MIN(viewer->capture->end, viewer->start + (guint64)(fraction * viewer->span));
 }
 update(viewer);
 return TRUE;
}

static void activate_signal(Viewer *viewer, gint index) {
 viewer->active = index;
 Signal *signal = selected(viewer);
 if (signal) gtk_entry_set_text(GTK_ENTRY(viewer->source), signal->name);
 update(viewer);
}

static void select_wave(GtkComboBox *box, gpointer data) {
 Viewer *viewer = data;
 if (viewer->rebuilding) return;
 const gchar *id = gtk_combo_box_get_active_id(box);
 if (id) activate_signal(viewer, (gint)g_ascii_strtoll(id, NULL, 10));
}

static void refresh_selector(Viewer *viewer) {
 const gchar *query = gtk_entry_get_text(GTK_ENTRY(viewer->filter));
 gchar *folded = g_utf8_casefold(query, -1);
 GtkComboBoxText *selector = GTK_COMBO_BOX_TEXT(viewer->selector);
 viewer->rebuilding = TRUE;
 gtk_combo_box_text_remove_all(selector);
 for (guint i = 0; i < viewer->capture->signals->len; i++) {
  Signal *signal = viewer->capture->signals->pdata[i];
  gchar *name = g_utf8_casefold(signal->name, -1);
  if (strstr(name, folded)) {
   gchar id[16];
   g_snprintf(id, sizeof id, "%u", i);
   gtk_combo_box_text_append(selector, id, signal->name);
  }
  g_free(name);
 }
 gchar active_id[16];
 g_snprintf(active_id, sizeof active_id, "%d", viewer->active);
 gtk_combo_box_set_active_id(GTK_COMBO_BOX(selector), active_id);
 viewer->rebuilding = FALSE;
 g_free(folded);
}

/* Select an exact recorded hierarchy and keep the time cursor in place. */
/* Выбирает точный путь записанного сигнала и сохраняет положение курсора. */
gboolean insight_waveforms_select_track(GtkWindow *window, const gchar *hierarchy)
{
    if (!window || !gtk_widget_get_visible(GTK_WIDGET(window)))
        return FALSE;
    Viewer *viewer = g_object_get_data(G_OBJECT(window), "insight-wave-viewer");
    if (!viewer)
        return FALSE;

    for (guint i = 0; i < viewer->capture->signals->len; i++)
    {
        const Signal *signal = viewer->capture->signals->pdata[i];
        if (g_strcmp0(signal->name, hierarchy))
            continue;

        gtk_entry_set_text(GTK_ENTRY(viewer->filter), "");
        activate_signal(viewer, (gint)i);
        refresh_selector(viewer);
        gtk_window_present(window);
        return TRUE;
    }
    return FALSE;
}

static void filter_changed(GtkSearchEntry *entry, gpointer data) {
 (void)entry;
 refresh_selector(data);
}

static void filter_activate(GtkEntry *entry, gpointer data) {
 (void)entry;
 Viewer *viewer = data;
 refresh_selector(viewer);
 gtk_combo_box_set_active(GTK_COMBO_BOX(viewer->selector), 0);
}

static void wave_action(GtkButton *button, gpointer data) {
 Viewer *viewer = data;
 Signal *signal = selected(viewer);
 if (!signal) return;
 gint action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
 Trace *trace = signal->trace;
 guint64 time;
 if ((action == 1 || action == -1) && wave_adjacent_event(trace, viewer->cursor, action == 1, &time))
  viewer->cursor = time;
 if (action == 6 || action == -6) wave_step_visible(viewer, action == 6);
 guint64 total = MAX(G_GUINT64_CONSTANT(1), viewer->capture->end);
 if (action == 2) viewer->span = MAX(G_GUINT64_CONSTANT(1), viewer->span / 2);
 if (action == 3) viewer->span = MIN(total, viewer->span * 2);
 guint64 step = MAX(G_GUINT64_CONSTANT(1), viewer->span / 2);
 if (action == 4) viewer->start -= MIN(viewer->start, step);
 else if (action == 5) viewer->start = MIN(total - viewer->span, viewer->start + step);
 else viewer->start = MIN(total - viewer->span, viewer->cursor > viewer->span / 2 ?
                          viewer->cursor - viewer->span / 2 : 0);
 update(viewer);
}

typedef enum {
    WAVE_MATCH_VALUE,
    WAVE_MATCH_RISING,
    WAVE_MATCH_FALLING
} WaveMatch;

/* Find the next recorded match strictly after the current cursor. */
/* Ищет следующее совпадение в записи строго после текущего курсора. */
static gboolean wave_find_match(const Trace *trace, guint64 cursor, WaveMatch mode,
                                const gchar *value, guint64 *time)
{
    guint start = upper_bound(trace, cursor);
    for (guint i = start; i < trace->edges->len; i++)
    {
        const Edge *edge = &g_array_index(trace->edges, Edge, i);
        gboolean matches = FALSE;
        if (mode == WAVE_MATCH_VALUE)
            matches = !strcmp(edge->value, value);
        else if (i && trace->width == 1)
        {
            const Edge *previous = &g_array_index(trace->edges, Edge, i - 1);
            gchar from = mode == WAVE_MATCH_RISING ? '0' : '1';
            gchar to = mode == WAVE_MATCH_RISING ? '1' : '0';
            matches = previous->value[0] == from && edge->value[0] == to;
        }
        if (matches)
        {
            *time = edge->time;
            return TRUE;
        }
    }
    return FALSE;
}

/* Move the waveform cursor to the next matching value or logical edge. */
/* Перемещает курсор диаграммы к следующему значению или логическому фронту. */
static void wave_search_condition(GtkButton *button, gpointer data)
{
    (void)button;
    Viewer *viewer = data;
    Signal *signal = selected(viewer);
    if (!signal)
        return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Find Signal Condition", GTK_WINDOW(viewer->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE,
        "_Find Next", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *heading = gtk_label_new(signal->name);
    gtk_label_set_ellipsize(GTK_LABEL(heading), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 4);

    GtkWidget *mode = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode), "Exact binary value (0, 1, x, z)");
    if (signal->trace->width == 1)
    {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode), "Rising edge (0 to 1)");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode), "Falling edge (1 to 0)");
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode), 0);
    gtk_box_pack_start(GTK_BOX(box), mode, FALSE, FALSE, 4);
    GtkWidget *value = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(value), (gint)signal->trace->width);
    gtk_entry_set_activates_default(GTK_ENTRY(value), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_box_pack_start(GTK_BOX(box), value, FALSE, FALSE, 4);
    GtkWidget *status = gtk_label_new("Enter every bit of the selected signal for an exact match.");
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 4);
    gtk_widget_show_all(dialog);

    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        WaveMatch match = (WaveMatch)gtk_combo_box_get_active(GTK_COMBO_BOX(mode));
        gchar *bits = g_ascii_strdown(gtk_entry_get_text(GTK_ENTRY(value)), -1);
        guint64 time;
        if (match == WAVE_MATCH_VALUE &&
            (strlen(bits) != signal->trace->width || strspn(bits, "01xz") != strlen(bits)))
            gtk_label_set_text(GTK_LABEL(status), "Enter exactly the signal width in bits (0, 1, x, z).");
        else if (!wave_find_match(signal->trace, viewer->cursor, match, bits, &time))
            gtk_label_set_text(GTK_LABEL(status), "No later matching event in the capture.");
        else
        {
            viewer->cursor = time;
            guint64 total = MAX(G_GUINT64_CONSTANT(1), viewer->capture->end);
            guint64 half = viewer->span / 2;
            viewer->start = MIN(total - viewer->span, time > half ? time - half : 0);
            update(viewer);
            gchar *message = g_strdup_printf("Match at tick %" G_GUINT64_FORMAT, time);
            gtk_label_set_text(GTK_LABEL(status), message);
            g_free(message);
        }
        g_free(bits);
    }
    gtk_widget_destroy(dialog);
}

static void pin_clicked(GtkButton *button, gpointer data) {
 (void)button;
 Viewer *viewer = data;
 gint active = viewer->active;
 if (active < 0) return;
 gint index = pinned_index(viewer, active);
 if (index >= 0) g_array_remove_index(viewer->tracks, (guint)index);
 else if (viewer->tracks->len < WAVE_TRACKS) g_array_append_val(viewer->tracks, active);
 update(viewer);
}

static void radix_changed(GtkComboBox *box, gpointer data) {
 Viewer *viewer = data;
 viewer->hexadecimal = gtk_combo_box_get_active(box) == 1;
 update(viewer);
}

static void marker_clicked(GtkButton *button, gpointer data) {
 Viewer *viewer = data;
 gint marker = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "marker"));
 if (marker < 0) viewer->marker_set[0] = viewer->marker_set[1] = FALSE;
 else {
  viewer->marker[marker] = viewer->cursor;
  viewer->marker_set[marker] = TRUE;
 }
 update(viewer);
}

static void source_clicked(GtkButton *button, gpointer data) {
 (void)button;
 Viewer *viewer = data;
 if (viewer->navigate) viewer->navigate(gtk_entry_get_text(GTK_ENTRY(viewer->source)), FALSE, NULL);
}

typedef struct {
    Viewer *viewer;
    GtkListStore *aliases;
    GtkWidget *details;
    gint selected;
} WaveSignalMap;

/* Identify VCD aliases by shared trace identity rather than by equal values. */
/* Определяет псевдонимы VCD по общей дорожке, а не по равенству значений. */
static gboolean wave_same_recorded_signal(const Signal *first, const Signal *second)
{
    return first->trace == second->trace;
}

/* Display links from the selected signal to every alias of its recorded trace. */
/* Показывает связи выбранного сигнала со всеми именами общей дорожки. */
static void wave_map_update(WaveSignalMap *map, gint index)
{
    map->selected = index;
    gtk_list_store_clear(map->aliases);
    if (index < 0)
    {
        gtk_label_set_text(GTK_LABEL(map->details), "Select a signal in a VCD scope.");
        return;
    }
    const Signal *signal = map->viewer->capture->signals->pdata[index];
    guint at = upper_bound(signal->trace, map->viewer->cursor);
    gchar *value = format_bits(at ? g_array_index(signal->trace->edges, Edge, at - 1).value : "unknown",
                                map->viewer->hexadecimal);
    gchar *label = g_strdup_printf("%s  ·  %u bits  ·  tick %" G_GUINT64_FORMAT "  ·  %.128s",
        signal->name, signal->trace->width, map->viewer->cursor, value);
    gtk_label_set_text(GTK_LABEL(map->details), label);
    g_free(label);
    g_free(value);
    for (guint i = 0; i < map->viewer->capture->signals->len; i++)
    {
        const Signal *alias = map->viewer->capture->signals->pdata[i];
        if ((gint)i != index && wave_same_recorded_signal(signal, alias))
            gtk_list_store_insert_with_values(map->aliases, NULL, -1,
                0, alias->name, 1, (gint)i, -1);
    }
}

/* Refresh the alias view when a signal is selected in the scope tree. */
/* Обновляет список псевдонимов при выборе сигнала в дереве областей. */
static void wave_map_selected(GtkTreeSelection *selection, gpointer data)
{
    WaveSignalMap *map = data;
    GtkTreeModel *model;
    GtkTreeIter row;
    gint index = -1;
    if (gtk_tree_selection_get_selected(selection, &model, &row))
        gtk_tree_model_get(model, &row, 1, &index, -1);
    wave_map_update(map, index);
}

/* Build a tree of recorded VCD scopes and their direct signals. */
/* Строит дерево записанных областей VCD и их непосредственных сигналов. */
static GtkTreeStore *wave_map_scopes(const Capture *capture)
{
    GtkTreeStore *store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    GHashTable *groups = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
    for (guint i = 0; i < capture->signals->len; i++)
    {
        const Signal *signal = capture->signals->pdata[i];
        GtkTreeIter *scope = g_hash_table_lookup(groups, signal->scope);
        if (!scope)
        {
            scope = g_new(GtkTreeIter, 1);
            gtk_tree_store_insert_with_values(store, scope, NULL, -1,
                0, *signal->scope ? signal->scope : "(root)", 1, -1, -1);
            g_hash_table_insert(groups, signal->scope, scope);
        }
        gtk_tree_store_insert_with_values(store, NULL, scope, -1,
            0, signal->leaf, 1, (gint)i, -1);
    }
    g_hash_table_unref(groups);
    return store;
}

/* Add a scrollable tree pane to the signal map dialog. */
/* Добавляет прокручиваемую панель дерева в диалог карты сигналов. */
static GtkWidget *wave_map_tree(GtkTreeModel *model, const gchar *title, GtkWidget *box)
{
    GtkWidget *tree = gtk_tree_view_new_with_model(model);
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(title,
        gtk_cell_renderer_text_new(), "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    return tree;
}

/* Open the recorded signal map and act on a selected signal or alias. */
/* Открывает карту записи и выполняет действие над сигналом или его псевдонимом. */
static void wave_show_signal_map(GtkButton *button, gpointer data)
{
    (void)button;
    Viewer *viewer = data;
    GtkWidget *owner = g_object_ref(viewer->window);
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Signal Map", GTK_WINDOW(owner),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE,
        "_Select Track", GTK_RESPONSE_ACCEPT, "_Pin Track", GTK_RESPONSE_APPLY,
        "_Go to HDL", GTK_RESPONSE_REJECT, "_HDL Uses…", GTK_RESPONSE_OK, NULL);
    g_object_ref_sink(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 740, 410);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *details = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(details), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_selectable(GTK_LABEL(details), TRUE);
    gtk_box_pack_start(GTK_BOX(box), details, FALSE, FALSE, 6);
    GtkWidget *panes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(box), panes, TRUE, TRUE, 0);

    GtkTreeStore *scopes = wave_map_scopes(viewer->capture);
    GtkListStore *aliases = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    GtkWidget *scope_tree = wave_map_tree(GTK_TREE_MODEL(scopes), "VCD scopes and signals", panes);
    GtkWidget *alias_tree = wave_map_tree(GTK_TREE_MODEL(aliases), "Aliases on the same recorded trace", panes);
    GtkTreeSelection *scope_selection = g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(scope_tree)));
    GtkTreeSelection *alias_selection = g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(alias_tree)));
    WaveSignalMap map = {viewer, aliases, details, -1};
    g_signal_connect(scope_selection, "changed", G_CALLBACK(wave_map_selected), &map);
    wave_map_update(&map, viewer->active);

    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    GtkTreeModel *alias_model;
    GtkTreeIter alias_row;
    gint index = map.selected;
    if (response != GTK_RESPONSE_NONE && gtk_tree_selection_get_selected(alias_selection, &alias_model, &alias_row))
        gtk_tree_model_get(alias_model, &alias_row, 1, &index, -1);
    g_signal_handlers_disconnect_by_data(scope_selection, &map);
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    g_object_unref(alias_selection);
    g_object_unref(scope_selection);
    g_object_unref(aliases);
    g_object_unref(scopes);

    if (index < 0 || (response != GTK_RESPONSE_ACCEPT && response != GTK_RESPONSE_APPLY &&
                      response != GTK_RESPONSE_REJECT && response != GTK_RESPONSE_OK))
    {
        g_object_unref(owner);
        return;
    }
    const Signal *signal = viewer->capture->signals->pdata[index];
    if (response == GTK_RESPONSE_REJECT || response == GTK_RESPONSE_OK)
    {
        if (viewer->navigate)
            viewer->navigate(signal->name, response == GTK_RESPONSE_OK, GTK_WINDOW(owner));
        g_object_unref(owner);
        return;
    }
    if (response == GTK_RESPONSE_APPLY && pinned_index(viewer, index) < 0 &&
        viewer->tracks->len < WAVE_TRACKS)
        g_array_append_val(viewer->tracks, index);
    activate_signal(viewer, index);
    refresh_selector(viewer);
    g_object_unref(owner);
}

static gint find_signal(Capture *capture, const gchar *name) {
 if (!name) return -1;
 for (guint i = 0; i < capture->signals->len; i++)
  if (!strcmp(((Signal *)capture->signals->pdata[i])->name, name)) return (gint)i;
 return -1;
}

static void restore_session(Viewer *viewer) {
 GKeyFile *session = g_key_file_new();
 if (viewer->session_path && g_key_file_load_from_file(session, viewer->session_path, G_KEY_FILE_NONE, NULL)) {
  guint64 total = viewer->span;
  guint64 span = g_key_file_get_uint64(session, "view", "span", NULL);
  if (span) viewer->span = MIN(total, span);
  viewer->start = MIN(total - viewer->span, g_key_file_get_uint64(session, "view", "start", NULL));
  viewer->cursor = MIN(viewer->capture->end, g_key_file_get_uint64(session, "view", "cursor", NULL));
  viewer->hexadecimal = g_key_file_get_boolean(session, "view", "hex", NULL);
  const gchar *keys[] = {"marker-a", "marker-b"};
  for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
   GError *error = NULL;
   guint64 tick = g_key_file_get_uint64(session, "view", keys[i], &error);
   viewer->marker_set[i] = !error && tick <= viewer->capture->end;
   if (viewer->marker_set[i]) viewer->marker[i] = tick;
   g_clear_error(&error);
  }
  gchar *active = g_key_file_get_string(session, "view", "active", NULL);
  gint found = find_signal(viewer->capture, active);
  if (found >= 0) viewer->active = found;
  g_free(active);
  const gchar *axis_keys[] = {"x", "y", "z"};
  for (guint i = 0; i < G_N_ELEMENTS(axis_keys); i++) {
   gchar *name = g_key_file_get_string(session, "instruments", axis_keys[i], NULL);
   if (name) viewer->instrument_index[i] = find_signal(viewer->capture, name) + 1;
   g_free(name);
  }
  gsize length = 0;
  gchar **names = g_key_file_get_string_list(session, "view", "tracks", &length, NULL);
  for (gsize i = 0; i < MIN(length, WAVE_TRACKS); i++) {
   gint index = find_signal(viewer->capture, names[i]);
   if (index >= 0 && pinned_index(viewer, index) < 0)
    g_array_append_val(viewer->tracks, index);
  }
  g_strfreev(names);
 }
 g_key_file_unref(session);
}

static void save_session(Viewer *viewer) {
 if (!viewer->session_path) return;
 gchar *directory = g_path_get_dirname(viewer->session_path);
 if (g_mkdir_with_parents(directory, 0700) == 0) {
  GKeyFile *session = g_key_file_new();
  g_key_file_set_uint64(session, "view", "span", viewer->span);
  g_key_file_set_uint64(session, "view", "start", viewer->start);
  g_key_file_set_uint64(session, "view", "cursor", viewer->cursor);
  g_key_file_set_boolean(session, "view", "hex", viewer->hexadecimal);
  const gchar *keys[] = {"marker-a", "marker-b"};
  for (guint i = 0; i < G_N_ELEMENTS(keys); i++)
   if (viewer->marker_set[i]) g_key_file_set_uint64(session, "view", keys[i], viewer->marker[i]);
  Signal *active = selected(viewer);
  if (active) g_key_file_set_string(session, "view", "active", active->name);
  const gchar *axis_keys[] = {"x", "y", "z"};
  for (guint i = 0; i < G_N_ELEMENTS(axis_keys); i++) {
   gint index = viewer->instrument_index[i];
   if (index >= 0) {
    Signal *signal = index ? viewer->capture->signals->pdata[index - 1] : NULL;
    g_key_file_set_string(session, "instruments", axis_keys[i], signal ? signal->name : "");
   }
  }
  const gchar *names[WAVE_TRACKS];
  for (guint i = 0; i < viewer->tracks->len; i++) {
   Signal *signal = viewer->capture->signals->pdata[g_array_index(viewer->tracks, gint, i)];
   names[i] = signal->name;
  }
  if (viewer->tracks->len) g_key_file_set_string_list(session, "view", "tracks", names, viewer->tracks->len);
  gsize length = 0;
  gchar *contents = g_key_file_to_data(session, &length, NULL);
  g_file_set_contents(viewer->session_path, contents, length, NULL);
  g_free(contents);
  g_key_file_unref(session);
 }
 g_free(directory);
}

static void viewer_free(gpointer data) {
 Viewer *viewer = data;
 save_session(viewer);
 g_free(viewer->session_path);
 capture_free(viewer->capture);
 g_array_unref(viewer->tracks);
 g_clear_object(&viewer->watch_values);
 g_free(viewer);
}

static GtkWidget *show_capture(GtkWindow *parent, Capture *capture, const gchar *session_path, InsightWaveSource navigate) {
 Viewer *viewer = g_new0(Viewer, 1);
 viewer->capture = capture;
 for (guint i = 0; i < G_N_ELEMENTS(viewer->instrument_index); i++)
  viewer->instrument_index[i] = -1;
 viewer->tracks = g_array_new(FALSE, FALSE, sizeof(gint));
 viewer->span = MAX(G_GUINT64_CONSTANT(1), capture->end);
 viewer->navigate = navigate;
 viewer->session_path = g_strdup(session_path);
 restore_session(viewer);
 viewer->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
 gtk_window_set_title(GTK_WINDOW(viewer->window), "VCD Waveforms");
 gtk_window_set_transient_for(GTK_WINDOW(viewer->window), parent);
 gtk_window_set_destroy_with_parent(GTK_WINDOW(viewer->window), TRUE);
 gtk_window_set_default_size(GTK_WINDOW(viewer->window), 760, 340);
 g_object_set_data_full(G_OBJECT(viewer->window), "insight-wave-viewer", viewer, viewer_free);
 GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
 gtk_container_set_border_width(GTK_CONTAINER(box), 10);
 gtk_container_add(GTK_CONTAINER(viewer->window), box);
 viewer->selector = gtk_combo_box_text_new();
 viewer->filter = gtk_search_entry_new();
 gtk_entry_set_placeholder_text(GTK_ENTRY(viewer->filter), "Filter signal names…");
 gtk_box_pack_start(GTK_BOX(box), viewer->filter, FALSE, FALSE, 0);
 g_signal_connect(viewer->filter, "search-changed", G_CALLBACK(filter_changed), viewer);
 g_signal_connect(viewer->filter, "activate", G_CALLBACK(filter_activate), viewer);
 GtkWidget *signal_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
 gtk_box_pack_start(GTK_BOX(box), signal_row, FALSE, FALSE, 0);
 gtk_box_pack_start(GTK_BOX(signal_row), viewer->selector, TRUE, TRUE, 0);
 viewer->pin = gtk_button_new_with_label("Pin track");
 g_signal_connect(viewer->pin, "clicked", G_CALLBACK(pin_clicked), viewer);
 gtk_box_pack_start(GTK_BOX(signal_row), viewer->pin, FALSE, FALSE, 0);
 GtkWidget *condition = gtk_button_new_with_label("Find Condition…");
 g_signal_connect(condition, "clicked", G_CALLBACK(wave_search_condition), viewer);
 gtk_box_pack_start(GTK_BOX(signal_row), condition, FALSE, FALSE, 0);
 GtkWidget *signal_map = gtk_button_new_with_label("Signal Map…");
 g_signal_connect(signal_map, "clicked", G_CALLBACK(wave_show_signal_map), viewer);
 gtk_box_pack_start(GTK_BOX(signal_row), signal_map, FALSE, FALSE, 0);
 GtkWidget *instruments = gtk_button_new_with_label("Signal Instruments…");
 g_signal_connect(instruments, "clicked", G_CALLBACK(instruments_show), viewer);
 gtk_box_pack_start(GTK_BOX(signal_row), instruments, FALSE, FALSE, 0);
 viewer->canvas = gtk_drawing_area_new();
 gtk_widget_add_events(viewer->canvas, GDK_BUTTON_PRESS_MASK);
 GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
 gtk_container_add(GTK_CONTAINER(scroll), viewer->canvas);
 gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
 viewer->value = gtk_label_new(NULL);
 gtk_label_set_ellipsize(GTK_LABEL(viewer->value), PANGO_ELLIPSIZE_END);
 gtk_box_pack_start(GTK_BOX(box), viewer->value, FALSE, FALSE, 0);
 GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
 gtk_box_pack_start(GTK_BOX(box), controls, FALSE, FALSE, 0);
 const gchar *labels[] = {"Previous transition", "Next transition", "Zoom in", "Zoom out", "Earlier", "Later"};
 const gint actions[] = {-1, 1, 2, 3, 4, 5};
 for (guint i = 0; i < G_N_ELEMENTS(labels); i++) {
  GtkWidget *button = gtk_button_new_with_label(labels[i]);
  g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(actions[i]));
  g_signal_connect(button, "clicked", G_CALLBACK(wave_action), viewer);
  gtk_box_pack_start(GTK_BOX(controls), button, FALSE, FALSE, 0);
 }
 GtkWidget *watch_panel = gtk_expander_new("Signal Watch");
 GtkWidget *watch_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
 gtk_container_add(GTK_CONTAINER(watch_panel), watch_box);
 gtk_box_pack_start(GTK_BOX(box), watch_panel, FALSE, FALSE, 0);
 GtkWidget *watch_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
 gtk_box_pack_start(GTK_BOX(watch_box), watch_controls, FALSE, FALSE, 0);
 const gchar *step_labels[] = {"Previous visible event", "Next visible event"};
 for (guint i = 0; i < G_N_ELEMENTS(step_labels); i++) {
  GtkWidget *button = gtk_button_new_with_label(step_labels[i]);
  g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(i ? 6 : -6));
  g_signal_connect(button, "clicked", G_CALLBACK(wave_action), viewer);
  gtk_box_pack_start(GTK_BOX(watch_controls), button, FALSE, FALSE, 0);
 }
 viewer->watch_values = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
 GtkWidget *watch_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(viewer->watch_values));
 const gchar *headings[] = {"Signal", "Value", "Last event tick", "Event at cursor"};
 for (guint i = 0; i < G_N_ELEMENTS(headings); i++) {
  GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
      headings[i], gtk_cell_renderer_text_new(), "text", i, NULL);
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(watch_tree), column);
 }
 GtkWidget *watch_scroll = gtk_scrolled_window_new(NULL, NULL);
 gtk_widget_set_size_request(watch_scroll, -1, 128);
 gtk_container_add(GTK_CONTAINER(watch_scroll), watch_tree);
 gtk_box_pack_start(GTK_BOX(watch_box), watch_scroll, TRUE, TRUE, 0);
 GtkWidget *measure = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
 gtk_box_pack_start(GTK_BOX(box), measure, FALSE, FALSE, 0);
 viewer->radix = gtk_combo_box_text_new();
 gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(viewer->radix), "Binary");
 gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(viewer->radix), "Hexadecimal");
 gtk_combo_box_set_active(GTK_COMBO_BOX(viewer->radix), viewer->hexadecimal ? 1 : 0);
 g_signal_connect(viewer->radix, "changed", G_CALLBACK(radix_changed), viewer);
 gtk_box_pack_start(GTK_BOX(measure), viewer->radix, FALSE, FALSE, 0);
 const gchar *marker_labels[] = {"Set A", "Set B", "Clear markers"};
 for (guint i = 0; i < G_N_ELEMENTS(marker_labels); i++) {
  GtkWidget *button = gtk_button_new_with_label(marker_labels[i]);
  g_object_set_data(G_OBJECT(button), "marker", GINT_TO_POINTER(i < 2 ? (gint)i : -1));
  g_signal_connect(button, "clicked", G_CALLBACK(marker_clicked), viewer);
  gtk_box_pack_start(GTK_BOX(measure), button, FALSE, FALSE, 0);
 }
 viewer->markers = gtk_label_new(NULL);
 gtk_box_pack_start(GTK_BOX(measure), viewer->markers, FALSE, FALSE, 0);
 GtkWidget *source_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
 gtk_box_pack_start(GTK_BOX(box), source_row, FALSE, FALSE, 0);
 viewer->source = gtk_entry_new();
 gtk_entry_set_placeholder_text(GTK_ENTRY(viewer->source), "HDL hierarchy (edit to remove simulator-only scopes)");
 gtk_box_pack_start(GTK_BOX(source_row), viewer->source, TRUE, TRUE, 0);
 GtkWidget *open = gtk_button_new_with_label("Go to HDL declaration");
 g_signal_connect(open, "clicked", G_CALLBACK(source_clicked), viewer);
 gtk_box_pack_start(GTK_BOX(source_row), open, FALSE, FALSE, 0);
 g_signal_connect(viewer->canvas, "draw", G_CALLBACK(draw_wave), viewer);
 g_signal_connect(viewer->canvas, "button-press-event", G_CALLBACK(click_wave), viewer);
 g_signal_connect(viewer->selector, "changed", G_CALLBACK(select_wave), viewer);
 refresh_selector(viewer);
 activate_signal(viewer, viewer->active);
 gtk_widget_show_all(viewer->window);
 return viewer->window;
}

typedef struct { GWeakRef parent; gchar *path, *session_path; InsightWaveSource navigate; } Load;
static void load_free(gpointer data) {
 Load *load = data;
 g_weak_ref_clear(&load->parent);
 g_free(load->path);
 g_free(load->session_path);
 g_free(load);
}
/* Read and parse a bounded VCD file for both the viewer and simulation. */
/* Читает VCD с ограничением размера для просмотра и симуляции. */
static Capture *read_capture_file(const gchar *path, GCancellable *cancel, GError **error)
{
 GFile *file = g_file_new_for_path(path);
 GFileInputStream *stream = g_file_read(file, cancel, error);
 g_object_unref(file);
 Capture *capture = NULL;
 if (stream) {
  GByteArray *bytes = g_byte_array_new();
  guint8 buffer[16384];
  gssize size;
  while ((size = g_input_stream_read(G_INPUT_STREAM(stream), buffer, sizeof buffer, cancel, error)) > 0) {
   if (bytes->len + (gsize)size > VCD_BYTES) break;
   g_byte_array_append(bytes, buffer, (guint)size);
  }
  if (size == 0 && bytes->len && !memchr(bytes->data, 0, bytes->len))
   capture = parse_capture((const gchar *)bytes->data, bytes->len, error);
  else if (!*error)
   g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "VCD must be text, 1 byte to 32 MiB");
  g_byte_array_unref(bytes);
  g_object_unref(stream);
 }
 return capture;
}

static void load_capture(GTask *task, gpointer object, gpointer data, GCancellable *cancel)
{
    (void)object;
    Load *load = data;
    GError *error = NULL;
    Capture *capture = read_capture_file(load->path, cancel, &error);
    if (capture)
        g_task_return_pointer(task, capture, capture_free);
    else
        g_task_return_error(task, error);
}

static void load_done(GObject *object, GAsyncResult *result, gpointer data) {
 (void)object; (void)data;
 GTask *task = G_TASK(result);
 Load *load = g_task_get_task_data(task);
 GtkWindow *parent = g_weak_ref_get(&load->parent);
 GError *error = NULL;
 Capture *capture = g_task_propagate_pointer(task, &error);
 if (parent && capture) show_capture(parent, capture, load->session_path, load->navigate);
 else {
  if (capture) capture_free(capture);
  if (parent && error) {
   GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
       GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error->message);
   g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
   gtk_widget_show(dialog);
  }
 }
 g_clear_error(&error);
 if (parent) g_object_unref(parent);
}

void insight_waveforms_open(GtkWindow *parent, const gchar *session_dir, InsightWaveSource navigate) {
 GtkWidget *dialog = gtk_file_chooser_dialog_new("Open VCD waveform", parent, GTK_FILE_CHOOSER_ACTION_OPEN,
     "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
 GtkFileFilter *filter = gtk_file_filter_new();
 gtk_file_filter_set_name(filter, "VCD waveforms");
 gtk_file_filter_add_pattern(filter, "*.[vV][cC][dD]");
 gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
 gchar *path = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT ?
     gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)) : NULL;
 gtk_widget_destroy(dialog);
 if (!path) return;
 Load *load = g_new0(Load, 1);
 g_weak_ref_init(&load->parent, parent);
 load->path = path;
 if (session_dir) {
  gchar *canonical = insight_canonicalize_filename(path, NULL);
  gchar *key = g_compute_checksum_for_string(G_CHECKSUM_SHA256, canonical, -1);
  load->session_path = g_build_filename(session_dir, key, NULL);
  g_free(key);
  g_free(canonical);
 }
 load->navigate = navigate;
 GTask *task = g_task_new(NULL, NULL, load_done, NULL);
 g_task_set_task_data(task, load, load_free);
 g_task_run_in_thread(task, load_capture);
 g_object_unref(task);
}
typedef struct {
    GWeakRef parent;
    GtkWidget *dialog;
    gchar *compiler, *runtime, *include_dir;
    gchar **sources;
    gchar *port_settings, *port_harness;
    GtkWidget *live_port_input, *live_port_value, *live_port_send, *live_port_display;
    GHashTable *live_port_samples;
    InsightWaveSource navigate;
    gboolean interactive;
    GMutex lock;
    GSubprocess *live_process;
    gchar *live_directory;
    GByteArray *live_output;
    GCond live_output_space;
    GtkWidget *console, *live_controls;
    guint refresh_source;
    gchar prompt_tail[2];
    gboolean preview_pending;
    GtkWidget *signal_name;
    GtkWidget *pause_button, *source_button, *breakpoint_controls, *breakpoint_file, *breakpoint_line;
    GtkWidget *rule_signal, *rule_bits, *rule_hits, *rule_log, *rule_script;
    GtkWidget *lua_watches, *reload_script;
    gchar *settings_path, *observer_file;
    gboolean restored_rules;
    InsightLuaDebug *lua_observer;
    gchar **pending_event;
    gsize observer_log_offset;
    gboolean bridge_ready, pause_requested;
    gboolean source_step_ready, processes_ready;
    GtkWidget *process_controls, *process_number;
    GtkWidget *frame_controls, *frame_depth, *memory_word;
    gboolean frames_ready;
    GtkWidget *source_step_controls;
    GString *protocol_line;
    GHashTable *breakpoints, *point_anchors;
    GPtrArray *unresolved_records, *unresolved_anchors;
    guint moved_points;
    gchar *trace_file;
    guint trace_line;
    InsightWaveLocation open_location;
} WaveSimulation;

/* Release the run configuration after the worker and dialog have finished. */
/* Освобождает параметры запуска после завершения задачи и диалога. */
static void simulation_free(gpointer data)
{
    WaveSimulation *run = data;
    if (run->refresh_source)
        g_source_remove(run->refresh_source);
    g_clear_object(&run->live_process);
    g_mutex_clear(&run->lock);
    g_cond_clear(&run->live_output_space);
    g_clear_pointer(&run->live_output, g_byte_array_unref);
    g_free(run->live_directory);
    g_free(run->trace_file);
    g_free(run->settings_path);
    g_free(run->port_settings);
    g_free(run->port_harness);
    g_clear_pointer(&run->live_port_samples, g_hash_table_unref);
    g_free(run->observer_file);
    insight_lua_debug_free(run->lua_observer);
    g_strfreev(run->pending_event);
    if (run->protocol_line)
        g_string_free(run->protocol_line, TRUE);
    g_clear_pointer(&run->breakpoints, g_hash_table_unref);
    g_clear_pointer(&run->point_anchors, g_hash_table_unref);
    g_clear_pointer(&run->unresolved_records, g_ptr_array_unref);
    g_clear_pointer(&run->unresolved_anchors, g_ptr_array_unref);
    g_weak_ref_clear(&run->parent);
    g_clear_object(&run->dialog);
    g_strfreev(run->sources);
    g_free(run->compiler);
    g_free(run->runtime);
    g_free(run->include_dir);
    g_free(run);
}

/* Key the run configuration by project and the unordered source selection. */
/* Привязывает настройки к проекту и набору выбранных исходников. */
static gchar *simulation_settings_path(const gchar *directory, const gchar *project, gchar **sources)
{
    if (!directory)
        return NULL;
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    if (project)
        g_checksum_update(checksum, (const guchar *)project, strlen(project));
    GSList *ordered = NULL;
    for (guint i = 0; sources[i]; i++)
        ordered = g_slist_prepend(ordered, sources[i]);
    ordered = g_slist_sort(ordered, (GCompareFunc)g_strcmp0);
    for (GSList *item = ordered; item; item = item->next)
    {
        const gchar *source = item->data;
        g_checksum_update(checksum, (const guchar *)"\0", 1);
        g_checksum_update(checksum, (const guchar *)source, strlen(source));
    }
    gchar *name = g_strconcat(g_checksum_get_string(checksum), ".conf", NULL);
    gchar *path = g_build_filename(directory, name, NULL);
    g_slist_free(ordered);
    g_checksum_free(checksum);
    g_free(name);
    return path;
}

static gboolean simulation_port_name(const gchar *name)
{
    return g_regex_match_simple("^[A-Za-z_][A-Za-z0-9_$]*$", name, 0, 0);
}

static guint simulation_port_connect(GString *body, GString *connections, GHashTable *ports,
                                     const gchar *name, guint64 width, gboolean wire)
{
    guint id = g_hash_table_size(ports);
    g_hash_table_add(ports, g_strdup(name));
    g_string_append_printf(body, "%s [%" G_GUINT64_FORMAT ":0] pin%u;\n",
                           wire ? "wire" : "reg", width - 1, id);
    if (connections->len)
        g_string_append_c(connections, ',');
    g_string_append_printf(connections, ".%s(pin%u)", name, id);
    return id;
}

static gboolean simulation_port_sram(gchar **args, GString *body, GString *connections,
                                      GHashTable *ports)
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
        "reg [%" G_GUINT64_FORMAT ":0] sram%u [0:%u];\n"
        "assign pin%u = (!pin%u && pin%u && !pin%u) ? sram%u[pin%u] : %" G_GUINT64_FORMAT "'bz;\n"
        "always @(pin%u or pin%u or pin%u or pin%u) begin\n"
        "  if (!pin%u && !pin%u && !$isunknown(pin%u)) sram%u[pin%u] = pin%u;\n"
        "end\n",
        width - 1, addr, (1u << address) - 1, dq, ce, we, oe, addr, addr, width,
        we, ce, addr, dq, ce, we, addr, addr, addr, dq);
    return TRUE;
}

/* Generate a private testbench from a per-source port recipe. */
static gchar *simulation_port_harness(const gchar *recipe, GError **error)
{
    gchar **lines = g_strsplit(recipe, "\n", -1);
    GString *body = g_string_new("`timescale 1ns/1ns\nmodule geany_port_testbench;\n");
    GString *connections = g_string_new(NULL);
    GString *live = g_string_new(NULL);
    guint64 live_interval = 0;
    GHashTable *ports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gchar *module = NULL;
    guint64 duration = 1000;
    gboolean valid = TRUE;
    for (guint i = 0; lines[i] && valid; i++)
    {
        gchar *line = g_strstrip(lines[i]);
        if (!*line || *line == '#')
            continue;
        gint count = 0;
        gchar **args = NULL;
        valid = g_shell_parse_argv(line, &count, &args, NULL);
        if (valid && count == 2 && !strcmp(args[0], "module") && !module &&
            g_regex_match_simple("^[A-Za-z_][A-Za-z0-9_$]*$", args[1], 0, 0))
            module = g_strdup(args[1]);
        else if (valid && count == 2 && !strcmp(args[0], "duration"))
            valid = g_ascii_string_to_unsigned(args[1], 10, 1, 1000000000, &duration, NULL);
        else if (valid && count == 2 && !strcmp(args[0], "live-interval"))
            valid = g_ascii_string_to_unsigned(args[1], 10, 1, 1000000000, &live_interval, NULL);
        else if (valid && count == 8 && !strcmp(args[0], "sram"))
            valid = simulation_port_sram(args, body, connections, ports);
        else if (valid && count >= 3 && g_hash_table_size(ports) < 128 &&
                 g_regex_match_simple("^[A-Za-z_][A-Za-z0-9_$]*$", args[1], 0, 0) &&
                 !g_hash_table_contains(ports, args[1]))
        {
            const gchar *mode = args[0];
            guint64 width = 1;
            gboolean monitor = !strcmp(mode, "monitor");
            gboolean input = !strcmp(mode, "live-input");
            gboolean output = monitor || !strcmp(mode, "output");
            gboolean scalar = !strcmp(mode, "clock") || !strcmp(mode, "clock-hz") ||
                              !strcmp(mode, "pulse");
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
                valid = strlen(args[j]) <= 256 &&
                    g_regex_match_simple("^[A-Za-z0-9_$'+*/%&|^~!=<>?:(). -]+$", args[j], 0, 0) &&
                    !strstr(args[j], "//") && !strstr(args[j], "/*");
            if (valid && !strcmp(mode, "pulse"))
                valid = !strcmp(args[4], "0") || !strcmp(args[4], "1");
            if (valid)
            {
                guint id = simulation_port_connect(body, connections, ports, args[1], width, output);
                if (constant || input)
                    g_string_append_printf(body, "initial pin%u = (%s);\n", id, args[3]);
                if (input)
                    g_string_append_printf(live,
                        "fd=$fopen(\"geany-input-%s\",\"r\"); "
                        "if(fd) begin if($fscanf(fd,\"%%b\",bits)==1) pin%u=bits; $fclose(fd); end\n",
                        args[1], id);
                else if (monitor)
                    g_string_append_printf(live, "$display(\"GEANY-PORT\\t%s\\t%%b\",pin%u);\n", args[1], id);
                else if (bus)
                    g_string_append_printf(body,
                        "initial begin integer step; time delay_ns; step=0; forever begin "
                        "pin%u=(%s); if (!( (%s)>0 )) $fatal(1, \"Invalid bus interval\"); "
                        "delay_ns=(%s); if (delay_ns==0) $fatal(1, \"Bus interval rounds to zero\"); "
                        "#(delay_ns); step=step+1; end end\n", id, args[4], args[3], args[3]);
                else if (!strcmp(mode, "pulse"))
                    g_string_append_printf(body,
                        "initial begin time length_ns; pin%u=1'b%c; "
                        "if (!( (%s)>=0 && (%s)>0 )) $fatal(1, \"Invalid pulse timing\"); "
                        "length_ns=(%s); if (!length_ns) $fatal(1, \"Pulse length rounds to zero\"); "
                        "#(%s) pin%u=1'b%s; #(length_ns) pin%u=1'b%c; end\n",
                        id, *args[4] == '0' ? '1' : '0', args[2], args[3], args[3], args[2], id,
                        args[4], id, *args[4] == '0' ? '1' : '0');
                else if (scalar)
                {
                    gchar *half_period = count == 4 ? g_strdup_printf("500000000.0/(%s)", args[3]) : NULL;
                    const gchar *high = half_period ? half_period : args[3];
                    const gchar *low = half_period ? half_period : args[4];
                    g_string_append_printf(body,
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
        if (!valid)
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid port recipe on line %u.", i + 1);
        g_strfreev(args);
    }
    if (valid && !module && connections->len)
    {
        valid = FALSE;
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Add a module line for the configured ports.");
    }
    gchar *result = NULL;
    if (valid && module)
    {
        g_string_append_printf(body,
            "%s dut(%s);\ninitial begin $dumpfile(\"ports.vcd\"); $dumpvars(0,geany_port_testbench); "
            "#%" G_GUINT64_FORMAT "; $finish; end\nendmodule\n", module, connections->str, duration);
        if (live->len)
        {
            /* Put the pacing checkpoint at line 3 of the private source. */
            g_string_insert(body, strlen("`timescale 1ns/1ns\nmodule geany_port_testbench;\n"),
                            "task geany_live_yield; $fflush(); endtask\n");
            g_string_truncate(body, body->len - strlen("endmodule\n"));
            g_string_append_printf(body,
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
    g_string_free(connections, TRUE);
    g_string_free(live, TRUE);
    g_strfreev(lines);
    return result;
}

static gboolean simulation_edit_ports(GtkWindow *parent, const gchar *path, gchar **harness)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Port Signals", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        harness ? "_Start Debugging" : "_Save for Next Run", GTK_RESPONSE_ACCEPT, NULL);
    g_object_ref_sink(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 390);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hint = gtk_label_new(
        "Leave empty to run an existing testbench. Otherwise enter one rule per line (times in ns).\n"
        "module NAME   ·   duration NS\n"
        "clock PORT START HIGH LOW   ·   clock-hz PORT START HZ\n"
        "pulse PORT START LENGTH 0|1   ·   value PORT WIDTH EXPR\n"
        "bus PORT WIDTH INTERVAL EXPR   ·   output PORT WIDTH\n"
        "live-interval NS   ·   live-input PORT WIDTH INITIAL   ·   monitor PORT WIDTH\n"
        "sram ADDR DQ CE_N WE_N OE_N ADDRESS_BITS DATA_BITS\n"
        "Use step in bus expressions and $time for simulation time. Quote formulas containing spaces.");
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 8);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GFile *file = g_file_new_for_path(path);
    GFileInputStream *stream = g_file_read(file, NULL, NULL);
    if (stream)
    {
        gchar saved[16385];
        gsize length = 0;
        if (g_input_stream_read_all(G_INPUT_STREAM(stream), saved, sizeof saved, &length, NULL, NULL) &&
            length < sizeof saved && g_utf8_validate(saved, length, NULL))
            gtk_text_buffer_set_text(buffer, saved, length);
        g_object_unref(stream);
    }
    g_object_unref(file);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    GtkWidget *status = gtk_label_new(harness ? "" : "Saved changes apply the next time debugging starts.");
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 6);
    gtk_widget_show_all(dialog);
    gboolean accepted = FALSE;
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        GtkTextIter first, last;
        gtk_text_buffer_get_bounds(buffer, &first, &last);
        gchar *recipe = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
        GError *error = NULL;
        gchar *generated = strlen(recipe) <= 16384 ? simulation_port_harness(recipe, &error) : NULL;
        gchar *directory = g_path_get_dirname(path);
        if (!error && strlen(recipe) <= 16384 && g_mkdir_with_parents(directory, 0700) == 0 &&
            g_file_set_contents(path, recipe, -1, &error))
        {
            accepted = TRUE;
            if (harness)
                *harness = g_steal_pointer(&generated);
        }
        if (!accepted)
            gtk_label_set_text(GTK_LABEL(status), error ? error->message : "Cannot save the port recipe.");
        g_free(directory);
        g_free(generated);
        g_free(recipe);
        g_clear_error(&error);
        if (accepted)
            break;
    }
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    return accepted;
}

static void simulation_port_settings(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    gchar *path = g_strdup(run->port_settings);
    simulation_edit_ports(GTK_WINDOW(run->dialog), path, NULL);
    g_free(path);
}

typedef struct {
    gchar **lines;
    gchar *digest;
    guint count;
} WavePointSource;

/* Release the temporary source snapshot used to resolve point anchors. */
/* Освобождает временный снимок исходника для привязки точек. */
static void simulation_point_source_free(gpointer data)
{
    WavePointSource *source = data;
    if (!source)
        return;
    g_strfreev(source->lines);
    g_free(source->digest);
    g_free(source);
}

/* Read a bounded source snapshot and normalize line endings and indentation. */
/* Читает ограниченный снимок исходника и нормализует переводы строк и отступы. */
static WavePointSource *simulation_point_source(const gchar *filename)
{
    const gsize limit = 2 * 1024 * 1024;
    GFile *file = g_file_new_for_path(filename);
    GFileInputStream *stream = g_file_read(file, NULL, NULL);
    g_object_unref(file);
    if (!stream)
        return NULL;

    gchar *bytes = g_malloc(limit + 1);
    gsize length = 0;
    gboolean valid = g_input_stream_read_all(G_INPUT_STREAM(stream), bytes,
        limit + 1, &length, NULL, NULL) && length <= limit && !memchr(bytes, 0, length) &&
        g_utf8_validate(bytes, (gssize)length, NULL);
    g_object_unref(stream);
    if (!valid)
    {
        g_free(bytes);
        return NULL;
    }

    WavePointSource *source = g_new0(WavePointSource, 1);
    source->digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guchar *)bytes, length);
    gsize out = 0;
    for (gsize i = 0; i < length; i++)
    {
        if (bytes[i] == '\r')
        {
            bytes[out++] = '\n';
            if (i + 1 < length && bytes[i + 1] == '\n')
                i++;
        }
        else
            bytes[out++] = bytes[i];
    }
    bytes[out] = 0;
    source->lines = g_strsplit(bytes, "\n", -1);
    source->count = g_strv_length(source->lines);
    for (guint i = 0; i < source->count; i++)
        g_strstrip(source->lines[i]);
    g_free(bytes);
    return source;
}

/* Encode a target line and its neighbors without using separators from the source. */
/* Кодирует строку и её соседей без разделителей из исходного текста. */
static gchar *simulation_point_anchor(const WavePointSource *source, guint line)
{
    if (!source || !line || line > source->count || !*source->lines[line - 1])
        return g_strdup("!");
    const gchar *context[] = {
        source->lines[line - 1],
        line > 1 ? source->lines[line - 2] : NULL,
        line < source->count ? source->lines[line] : NULL
    };
    gchar *encoded[3];
    for (guint i = 0; i < G_N_ELEMENTS(context); i++)
    {
        if (!context[i])
            encoded[i] = g_strdup("-");
        else if (strlen(context[i]) > 1024)
            encoded[i] = g_strdup("!");
        else
            encoded[i] = g_base64_encode((const guchar *)context[i], strlen(context[i]));
    }
    gchar *anchor = g_strjoin(":", source->digest, encoded[0], encoded[1], encoded[2], NULL);
    for (guint i = 0; i < G_N_ELEMENTS(encoded); i++)
        g_free(encoded[i]);
    return anchor;
}

/* Compare a saved neighbor, distinguishing a missing line from an empty line. */
/* Сравнивает соседнюю строку, различая отсутствие строки и пустую строку. */
static gboolean simulation_neighbor_matches(const gchar *encoded, const gchar *line)
{
    if (!line)
        return !strcmp(encoded, "-");
    if (!strcmp(encoded, "-") || !strcmp(encoded, "!"))
        return FALSE;
    gsize length;
    guchar *text = g_base64_decode(encoded, &length);
    gboolean equal = length == strlen(line) && (!length || !memcmp(text, line, length));
    g_free(text);
    return equal;
}

/* Keep unchanged files at the old line, otherwise resolve only an unambiguous match. */
/* Сохраняет номер в неизменённом файле, иначе ищет однозначное совпадение. */
static guint simulation_resolve_point(const WavePointSource *source, guint original, const gchar *anchor)
{
    if (!anchor || !*anchor)
        return original;
    if (!source || !strcmp(anchor, "!"))
        return 0;
    gchar **parts = g_strsplit(anchor, ":", 4);
    guint resolved = 0;
    if (g_strv_length(parts) == 4)
    {
        if (!strcmp(parts[0], source->digest) && original <= source->count)
            resolved = original;
        else if (*parts[1] && strcmp(parts[1], "!"))
        {
            gsize length;
            guchar *center = g_base64_decode(parts[1], &length);
            guint matches = 0, contexts = 0, candidate = 0, exact = 0;
            for (guint i = 0; length && i < source->count; i++)
            {
                if (strlen(source->lines[i]) != length || memcmp(center, source->lines[i], length))
                    continue;
                candidate = i + 1;
                matches++;
                if (simulation_neighbor_matches(parts[2], i ? source->lines[i - 1] : NULL) &&
                    simulation_neighbor_matches(parts[3], i + 1 < source->count ? source->lines[i + 1] : NULL))
                {
                    exact = i + 1;
                    contexts++;
                }
            }
            resolved = matches == 1 ? candidate : contexts == 1 ? exact : 0;
            g_free(center);
        }
    }
    g_strfreev(parts);
    return resolved;
}

/* Restore only well-formed rules for files selected in this run. */
/* Восстанавливает только корректные правила для выбранных файлов. */
static void simulation_load_settings(WaveSimulation *run)
{
    if (!run->settings_path)
        return;
    GKeyFile *settings = g_key_file_new();
    if (g_key_file_load_from_file(settings, run->settings_path, G_KEY_FILE_NONE, NULL))
    {
        gsize count = 0;
        gchar **records = g_key_file_get_string_list(settings, "Simulation", "Points", &count, NULL);
        gsize anchor_count = 0;
        gchar **anchors = g_key_file_get_string_list(settings, "Simulation", "Anchors", &anchor_count, NULL);
        GHashTable *sources = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, simulation_point_source_free);
        for (gsize i = 0; i < MIN(count, 128); i++)
        {
            gchar **parts = g_strsplit(records[i], "\t", 7);
            guint64 line, hits;
            if (g_strv_length(parts) == 7 &&
                g_ascii_string_to_unsigned(parts[0], 10, 1, G_MAXINT, &line, NULL) &&
                g_strv_contains((const gchar *const *)run->sources, parts[1]) &&
                !strpbrk(parts[1], "\r\n") && !strpbrk(parts[2], "\r\n ") &&
                strlen(parts[2]) <= 2048 && strlen(parts[3]) <= 1024 &&
                ((*parts[2] == 0) == (*parts[3] == 0)) &&
                strspn(parts[3], "01xz") == strlen(parts[3]) &&
                g_ascii_string_to_unsigned(parts[4], 10, 1, 1000000, &hits, NULL) &&
                (!strcmp(parts[5], "0") || !strcmp(parts[5], "1")) &&
                (!strcmp(parts[6], "0") || !strcmp(parts[6], "1")) &&
                (strcmp(parts[5], "1") || strcmp(parts[6], "1")))
            {
                const gchar *anchor = i < anchor_count ? anchors[i] : "";
                WavePointSource *source = NULL;
                if (*anchor)
                {
                    if (!g_hash_table_contains(sources, parts[1]))
                    {
                        if (g_hash_table_size(sources) >= 4)
                            g_hash_table_remove_all(sources);
                        source = simulation_point_source(parts[1]);
                        g_hash_table_insert(sources, g_strdup(parts[1]), source);
                    }
                    else
                        source = g_hash_table_lookup(sources, parts[1]);
                }
                guint resolved = simulation_resolve_point(source, (guint)line, anchor);
                gchar *key = resolved ? g_strdup_printf("%u\t%s", resolved, parts[1]) : NULL;
                if (!key || g_hash_table_contains(run->breakpoints, key))
                {
                    g_ptr_array_add(run->unresolved_records, g_strdup(records[i]));
                    g_ptr_array_add(run->unresolved_anchors, g_strdup(anchor));
                    g_free(key);
                }
                else
                {
                    gchar *rule = g_strjoin("\t", parts[2], parts[3], parts[4], parts[5], parts[6], NULL);
                    gchar *current_anchor = resolved != line ? simulation_point_anchor(source, resolved) : g_strdup(anchor);
                    if (resolved != line)
                        run->moved_points++;
                    g_hash_table_insert(run->point_anchors, g_strdup(key), current_anchor);
                    g_hash_table_insert(run->breakpoints, key, rule);
                }
            }
            g_strfreev(parts);
        }
        g_hash_table_unref(sources);
        g_strfreev(anchors);
        g_strfreev(records);
        gchar *script = g_key_file_get_string(settings, "Simulation", "Observer", NULL);
        if (script)
        {
            gchar *local = g_filename_from_utf8(script, -1, NULL, NULL, NULL);
            if (local && g_path_is_absolute(local))
                run->observer_file = g_steal_pointer(&local);
            g_free(local);
            g_free(script);
        }
    }
    g_key_file_unref(settings);
}

/* Serialize the rules in stable order for storage and runtime reload. */
/* Записывает правила в стабильном порядке для настроек и симулятора. */
static GPtrArray *simulation_rule_records(const WaveSimulation *run)
{
    GPtrArray *records = g_ptr_array_new_with_free_func(g_free);
    GList *keys = g_list_sort(g_hash_table_get_keys(run->breakpoints), (GCompareFunc)g_strcmp0);
    for (GList *item = keys; item; item = item->next)
    {
        const gchar *rule = g_hash_table_lookup(run->breakpoints, item->data);
        g_ptr_array_add(records, g_strconcat(item->data, "\t", rule, NULL));
    }
    g_list_free(keys);
    return records;
}

/* Save the run setup in the Geany profile, outside the project tree. */
/* Сохраняет настройки запуска в профиле Geany за пределами дерева проекта. */
static gboolean simulation_save_settings(const WaveSimulation *run)
{
    if (!run->settings_path)
        return TRUE;
    GKeyFile *settings = g_key_file_new();
    GPtrArray *records = simulation_rule_records(run);
    GPtrArray *anchors = g_ptr_array_new();
    for (guint i = 0; i < records->len; i++)
    {
        const gchar *record = records->pdata[i];
        const gchar *tab = strchr(record, '\t');
        const gchar *end = strchr(tab + 1, '\t');
        gchar *key = g_strndup(record, end - record);
        const gchar *anchor = g_hash_table_lookup(run->point_anchors, key);
        g_ptr_array_add(anchors, (gpointer)(anchor ? anchor : ""));
        g_free(key);
    }
    for (guint i = 0; i < run->unresolved_records->len; i++)
    {
        g_ptr_array_add(records, g_strdup(run->unresolved_records->pdata[i]));
        g_ptr_array_add(anchors, run->unresolved_anchors->pdata[i]);
    }
    if (records->len)
    {
        g_key_file_set_string_list(settings, "Simulation", "Points",
            (const gchar *const *)records->pdata, records->len);
        g_key_file_set_string_list(settings, "Simulation", "Anchors",
            (const gchar *const *)anchors->pdata, anchors->len);
    }
    g_ptr_array_unref(anchors);
    if (run->observer_file)
    {
        gchar *utf8 = g_filename_to_utf8(run->observer_file, -1, NULL, NULL, NULL);
        if (utf8)
            g_key_file_set_string(settings, "Simulation", "Observer", utf8);
        g_free(utf8);
    }
    gchar *directory = g_path_get_dirname(run->settings_path);
    gboolean saved = g_mkdir_with_parents(directory, 0700) == 0 &&
        g_key_file_save_to_file(settings, run->settings_path, NULL);
    g_free(directory);
    g_ptr_array_unref(records);
    g_key_file_unref(settings);
    return saved;
}

/* Prefer the simulator installed beside the editor before searching PATH. */
/* Сначала ищет симулятор рядом с редактором, затем в PATH. */
static gchar *simulation_tool(const gchar *bin, const gchar *name)
{
    gchar *path = g_build_filename(bin, name, NULL);
    if (g_file_test(path, G_FILE_TEST_IS_EXECUTABLE))
        return path;
    g_free(path);
    return g_find_program_in_path(name);
}

/* Terminate a child so a blocked pipe read can return on cancellation. */
/* Завершает дочерний процесс, чтобы чтение канала вернулось при отмене. */
static void simulation_force_exit(GCancellable *cancel, gpointer data)
{
    (void)cancel;
    g_subprocess_force_exit(G_SUBPROCESS(data));
}

/* Compile or run a batch testbench and retain its first diagnostic bytes. */
/* Компилирует или запускает тестбенч и сохраняет начало диагностики. */
static gboolean simulation_process(const gchar *const *argv, const gchar *directory,
                                   const gchar *bin, GCancellable *cancel, GError **error)
{
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return FALSE;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, directory);
    const gchar *existing = g_getenv("PATH");
    gchar *path = g_strconcat(bin, G_SEARCHPATH_SEPARATOR_S, existing ? existing : "", NULL);
    g_subprocess_launcher_setenv(launcher, "PATH", path, TRUE);
    g_free(path);

    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, argv, error);
    g_object_unref(launcher);
    if (!process)
        return FALSE;

    gulong cancellation = g_cancellable_connect(cancel, G_CALLBACK(simulation_force_exit), process, NULL);
    GInputStream *output = g_subprocess_get_stdout_pipe(process);
    gchar bytes[8192], diagnostic[4096];
    gsize saved = 0;
    gssize count;
    while ((count = g_input_stream_read(output, bytes, sizeof bytes, cancel, error)) > 0)
    {
        gsize available = MIN((gsize)count, sizeof diagnostic - saved);
        memcpy(diagnostic + saved, bytes, available);
        saved += available;
    }
    gboolean ok = count == 0 && g_subprocess_wait_check(process, cancel, error);
    if (count < 0 || g_cancellable_is_cancelled(cancel))
    {
        g_subprocess_force_exit(process);
        g_subprocess_wait(process, NULL, NULL);
    }
    if (!ok && saved && !g_cancellable_is_cancelled(cancel))
    {
        gchar *message = g_utf8_make_valid(diagnostic, saved);
        g_prefix_error(error, "%s\n", message);
        g_free(message);
    }
    g_cancellable_disconnect(cancel, cancellation);
    g_object_unref(process);
    return ok;
}

/* Wake a blocked output reader or producer and stop the simulator on cancellation. */
/* Будит ожидающий поток вывода и останавливает симулятор при отмене. */
static void simulation_cancel_live(GCancellable *cancel, gpointer data)
{
    (void)cancel;
    WaveSimulation *run = data;
    g_mutex_lock(&run->lock);
    GSubprocess *process = run->live_process ? g_object_ref(run->live_process) : NULL;
    g_cond_broadcast(&run->live_output_space);
    g_mutex_unlock(&run->lock);
    if (process)
    {
        g_subprocess_force_exit(process);
        g_object_unref(process);
    }
}

/* Pump simulator output off the GTK thread through a bounded shared buffer. */
/* Передаёт вывод симулятора через ограниченный буфер вне потока GTK. */
static gboolean simulation_interactive(WaveSimulation *run, const gchar *directory,
                                       const gchar *bin, GCancellable *cancel, GError **error)
{
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return FALSE;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, directory);
    g_subprocess_launcher_setenv(launcher, "GEANY_VVP_DEBUG", "1", TRUE);
    const gchar *existing = g_getenv("PATH");
    gchar *path = g_strconcat(bin, G_SEARCHPATH_SEPARATOR_S, existing ? existing : "", NULL);
    g_subprocess_launcher_setenv(launcher, "PATH", path, TRUE);
    g_free(path);
    const gchar *argv[] = {run->runtime, "-i", "-s", "simulation.vvp", NULL};
    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, argv, error);
    g_object_unref(launcher);
    if (!process)
        return FALSE;

    g_mutex_lock(&run->lock);
    run->live_process = g_object_ref(process);
    run->live_directory = g_strdup(directory);
    g_mutex_unlock(&run->lock);
    gulong cancellation = g_cancellable_connect(cancel, G_CALLBACK(simulation_cancel_live), run, NULL);

    GInputStream *output = g_subprocess_get_stdout_pipe(process);
    guint8 bytes[8192];
    gssize count;
    while ((count = g_input_stream_read(output, bytes, sizeof bytes, cancel, error)) > 0)
    {
        g_mutex_lock(&run->lock);
        while (run->live_output->len + (guint)count > 65536 && !g_cancellable_is_cancelled(cancel))
            g_cond_wait(&run->live_output_space, &run->lock);
        gboolean cancelled = g_cancellable_is_cancelled(cancel);
        if (!cancelled)
            g_byte_array_append(run->live_output, bytes, (guint)count);
        g_mutex_unlock(&run->lock);
        if (cancelled)
        {
            g_cancellable_set_error_if_cancelled(cancel, error);
            break;
        }
    }

    gboolean ok = count == 0 && g_subprocess_wait_check(process, cancel, error);
    if (count != 0 || g_cancellable_is_cancelled(cancel))
    {
        g_subprocess_force_exit(process);
        g_subprocess_wait(process, NULL, NULL);
    }
    g_cancellable_disconnect(cancel, cancellation);
    g_mutex_lock(&run->lock);
    g_clear_object(&run->live_process);
    g_mutex_unlock(&run->lock);
    g_object_unref(process);
    return ok;
}

/* Select the single VCD produced by the testbench in its scratch directory. */
/* Выбирает единственный VCD, созданный тестбенчем во временной папке. */
static gchar *simulation_find_vcd(const gchar *directory, GError **error)
{
    GDir *dir = g_dir_open(directory, 0, error);
    if (!dir)
        return NULL;
    gchar *path = NULL;
    const gchar *name;
    while ((name = g_dir_read_name(dir)))
    {
        const gchar *extension = strrchr(name, '.');
        if (!extension || g_ascii_strcasecmp(extension, ".vcd"))
            continue;
        if (path)
        {
            g_clear_pointer(&path, g_free);
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "The testbench produced more than one VCD file.");
            break;
        }
        path = g_build_filename(directory, name, NULL);
    }
    g_dir_close(dir);
    if (!path && !*error)
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "The testbench produced no VCD. Add $dumpfile and $dumpvars.");
    return path;
}

/* Stage restored rules in the private runtime directory before starting vvp. */
/* Подготавливает восстановленные правила в рабочей папке перед запуском vvp. */
static gboolean simulation_stage_rules(const WaveSimulation *run, const gchar *directory, GError **error)
{
    if (!g_hash_table_size(run->breakpoints))
        return TRUE;
    GPtrArray *records = simulation_rule_records(run);
    GString *contents = g_string_new(NULL);
    for (guint i = 0; i < records->len; i++)
        g_string_append_printf(contents, "%s\n", (gchar *)records->pdata[i]);
    gchar *path = g_build_filename(directory, ".geany-vvp-breakpoints", NULL);
    gboolean staged = g_file_set_contents(path, contents->str, (gssize)contents->len, error);
    g_free(path);
    g_string_free(contents, TRUE);
    g_ptr_array_unref(records);
    return staged;
}

/* Compile and run the selected sources off the GTK thread. */
/* Компилирует и запускает выбранные исходники вне потока GTK. */
static void simulation_worker(GTask *task, gpointer object, gpointer data, GCancellable *cancel)
{
    (void)object;
    WaveSimulation *run = data;
    GError *error = NULL;
    gchar *directory = g_dir_make_tmp("geany-verilog-XXXXXX", &error);
    Capture *capture = NULL;
    if (directory)
    {
        gchar *bin = g_path_get_dirname(run->compiler);
        GPtrArray *command = g_ptr_array_new();
        const gchar *options[] = {run->compiler, "-g2012", "-I", run->include_dir, "-o", "simulation.vvp"};
        for (guint i = 0; i < G_N_ELEMENTS(options); i++)
            g_ptr_array_add(command, (gpointer)options[i]);
        if (run->interactive)
            g_ptr_array_add(command, "-pfileline=1");
        for (guint i = 0; run->sources[i]; i++)
            g_ptr_array_add(command, run->sources[i]);
        gchar *harness_path = NULL;
        if (run->port_harness)
        {
            harness_path = g_build_filename(directory, "geany-port-testbench.sv", NULL);
            if (g_file_set_contents(harness_path, run->port_harness, -1, &error))
            {
                g_ptr_array_add(command, "-s");
                g_ptr_array_add(command, "geany_port_testbench");
                g_ptr_array_add(command, harness_path);
            }
        }
        g_ptr_array_add(command, NULL);
        const gchar *execute[] = {run->runtime, "simulation.vvp", NULL};
        if (!error && simulation_process((const gchar *const *)command->pdata, directory, bin, cancel, &error) &&
            (run->interactive ? (simulation_stage_rules(run, directory, &error) &&
             simulation_interactive(run, directory, bin, cancel, &error)) :
             simulation_process(execute, directory, bin, cancel, &error)))
        {
            gchar *vcd = simulation_find_vcd(directory, &error);
            if (vcd)
                capture = read_capture_file(vcd, cancel, &error);
            g_free(vcd);
        }
        g_ptr_array_unref(command);
        g_free(harness_path);
        g_free(bin);

        GDir *dir = g_dir_open(directory, 0, NULL);
        if (dir)
        {
            const gchar *name;
            while ((name = g_dir_read_name(dir)))
            {
                gchar *path = g_build_filename(directory, name, NULL);
                g_unlink(path);
                g_free(path);
            }
            g_dir_close(dir);
        }
        g_rmdir(directory);
        g_free(directory);
    }
    if (capture)
        g_task_return_pointer(task, capture, capture_free);
    else
        g_task_return_error(task, error);
}

/* Decode a source location from the bridge or the native vvp trace. */
/* Читает позицию исходника из моста или штатной трассы vvp. */
static gboolean simulation_trace_location(const gchar *text, gchar **filename, guint *line)
{
    const gchar *bridge = strstr(text, "GEANY-VVP-STOP\t");
    if (bridge)
    {
        const gchar *number = bridge + strlen("GEANY-VVP-STOP\t");
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

/* Collect complete control records even when the log is read in chunks. */
/* Собирает целые управляющие записи при чтении журнала частями. */
static void simulation_read_records(WaveSimulation *run, const gchar *bytes, gsize length)
{
    for (gsize i = 0; i < length; i++)
    {
        if (bytes[i] != '\n')
        {
            if (run->protocol_line->len < 8192)
                g_string_append_c(run->protocol_line, bytes[i]);
            continue;
        }
        if (run->protocol_line->len && run->protocol_line->str[run->protocol_line->len - 1] == '\r')
            g_string_truncate(run->protocol_line, run->protocol_line->len - 1);
        const gchar *text = run->protocol_line->str;
        if (strstr(text, "GEANY-VVP-FRAMES"))
            run->frames_ready = TRUE;
        if (strstr(text, "GEANY-VVP-PROCESSES"))
            run->processes_ready = TRUE;
        if (strstr(text, "GEANY-VVP-SOURCE-STEP"))
            run->source_step_ready = TRUE;
        if (strstr(text, "GEANY-VVP-READY"))
            run->bridge_ready = TRUE;
        const gchar *port = strstr(text, "GEANY-PORT\t");
        if (run->live_port_samples && port)
        {
            gchar **fields = g_strsplit(port + strlen("GEANY-PORT\t"), "\t", 3);
            if (g_strv_length(fields) == 2 && simulation_port_name(fields[0]) &&
                *fields[1] && strlen(fields[1]) <= 64 && strspn(fields[1], "01xz") == strlen(fields[1]) &&
                (g_hash_table_size(run->live_port_samples) < 128 ||
                 g_hash_table_contains(run->live_port_samples, fields[0])))
                g_hash_table_replace(run->live_port_samples, g_strdup(fields[0]), g_strdup(fields[1]));
            g_strfreev(fields);
        }
        const gchar *event = strstr(text, "GEANY-VVP-EVENT\t");
        if (event)
        {
            g_strfreev(run->pending_event);
            run->pending_event = g_strsplit(event, "\t", 7);
        }
        gchar *filename = NULL;
        guint line;
        if (simulation_trace_location(text, &filename, &line))
        {
            g_free(run->trace_file);
            run->trace_file = filename;
            run->trace_line = line;
        }
        g_string_truncate(run->protocol_line, 0);
    }
}

/* Open the latest recorded HDL statement in the editor. */
/* Открывает в редакторе последний записанный оператор HDL. */
static void simulation_open_location(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    if (run->trace_file && run->open_location)
        run->open_location(run->trace_file, run->trace_line);
}

/* Return a copy of the running simulator's scratch directory. */
/* Возвращает копию пути к рабочей папке запущенного симулятора. */
static gchar *simulation_live_directory(WaveSimulation *run)
{
    g_mutex_lock(&run->lock);
    gchar *directory = run->live_process ? g_strdup(run->live_directory) : NULL;
    g_mutex_unlock(&run->lock);
    return directory;
}

/* Request a scheduler pause through the patched runtime on either platform. */
/* Запрашивает паузу планировщика через адаптированную среду на обеих платформах. */
static void simulation_request_pause(GtkButton *button, gpointer data)
{
    WaveSimulation *run = data;
    if (!run->bridge_ready || run->pause_requested)
        return;
    gchar *directory = simulation_live_directory(run);
    if (!directory)
        return;
    gchar *path = g_build_filename(directory, ".geany-vvp-pause", NULL);
    run->pause_requested = g_file_set_contents(path, "pause\n", -1, NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(button), !run->pause_requested);
    g_free(path);
    g_free(directory);
}

static void simulation_write_command(WaveSimulation *run, const gchar *command);

/* Append newly produced Lua output and refresh its published watch values. */
/* Добавляет новый вывод Lua и обновляет опубликованные значения наблюдения. */
static void simulation_observer_output(WaveSimulation *run)
{
    const gchar *log = insight_lua_debug_observer_log(run->lua_observer);
    gsize length = strlen(log);
    if (length > run->observer_log_offset)
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gchar *message = g_utf8_make_valid(log + run->observer_log_offset, -1);
        gtk_text_buffer_insert(buffer, &end, message, -1);
        g_free(message);
    }
    run->observer_log_offset = length;

    InsightLuaWatchGraph *graph = g_new0(InsightLuaWatchGraph, 1);
    insight_lua_debug_observer_watch(run->lua_observer, graph);
    GString *text = g_string_new(NULL);
    for (unsigned i = 1; i < graph->node_count; i++)
    {
        const InsightLuaWatchNode *node = &graph->nodes[i];
        if (text->len)
            g_string_append_c(text, '\n');
        g_string_append_printf(text, "%s = %s", node->label, node->value);
    }
    gchar *valid = g_utf8_make_valid(text->str, -1);
    gtk_label_set_text(GTK_LABEL(run->lua_watches), valid);
    g_free(valid);
    g_string_free(text, TRUE);
    g_free(graph);
}

/* Let Lua decide whether a scripted point stays stopped or resumes. */
/* Позволяет Lua оставить сценарную точку на паузе или продолжить выполнение. */
static void simulation_handle_observer_event(WaveSimulation *run)
{
    gchar **fields = g_steal_pointer(&run->pending_event);
    if (!fields)
        return;
    guint64 line, tick, hits;
    if (insight_lua_debug_observer_active(run->lua_observer) &&
        g_strv_length(fields) == 7 &&
        g_ascii_string_to_unsigned(fields[1], 10, 1, G_MAXINT, &line, NULL) &&
        g_ascii_string_to_unsigned(fields[3], 10, 0, G_MAXUINT64, &tick, NULL) &&
        g_ascii_string_to_unsigned(fields[4], 10, 1, G_MAXUINT, &hits, NULL))
    {
        InsightObserverValue values[] = {
            {.scope = 0, .type = 's', .name = "tick", .text = fields[3], .length = strlen(fields[3])},
            {.scope = 0, .type = 'i', .name = "hits", .integer = (long long)hits},
            {.scope = 0, .type = 's', .name = "signal", .text = fields[5], .length = strlen(fields[5])},
            {.scope = 0, .type = 's', .name = "bits", .text = fields[6], .length = strlen(fields[6])},
            {.scope = 0, .type = 'i', .name = "ticks", .integer = tick <= G_MAXINT64 ? (long long)tick : 0}
        };
        InsightObserverEvent event = {
            .kind = "hdl-point", .file = fields[2], .name = fields[5], .line = (unsigned long)line,
            .values = values, .count = tick <= G_MAXINT64 ? G_N_ELEMENTS(values) : G_N_ELEMENTS(values) - 1
        };
        gint decision = insight_lua_debug_observe(run->lua_observer, &event);
        simulation_observer_output(run);
        if (decision == 0 && !run->pause_requested)
            simulation_write_command(run, "cont\n");
    }
    g_strfreev(fields);
}

/* Drain queued simulator bytes, then update console widgets on the GTK thread. */
/* Забирает накопленный вывод и обновляет консоль в потоке GTK. */
static gboolean simulation_refresh_console(gpointer data)
{
    WaveSimulation *run = data;
    g_mutex_lock(&run->lock);
    gboolean running = run->live_process != NULL;
    GByteArray *output = run->live_output;
    run->live_output = g_byte_array_new();
    g_cond_signal(&run->live_output_space);
    g_mutex_unlock(&run->lock);

    const gchar *bytes = (const gchar *)output->data;
    gssize count = output->len;
    if (count > 0)
    {
        simulation_read_records(run, bytes, (gsize)count);
        run->prompt_tail[0] = count > 1 ? bytes[count - 2] : run->prompt_tail[1];
        run->prompt_tail[1] = bytes[count - 1];
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
        gint excess = gtk_text_buffer_get_char_count(buffer) - 65536;
        GtkTextIter end;
        if (excess > 0)
        {
            GtkTextIter first, limit;
            gtk_text_buffer_get_start_iter(buffer, &first);
            gtk_text_buffer_get_iter_at_offset(buffer, &limit, excess);
            gtk_text_buffer_delete(buffer, &first, &limit);
        }
        gchar *valid = g_utf8_make_valid(bytes, count);
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, valid, -1);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(run->console), &end, 0, FALSE, 0, 0);
        g_free(valid);
    }
    g_byte_array_unref(output);
    gboolean paused = running && run->prompt_tail[0] == '>' && run->prompt_tail[1] == ' ';
    if (paused && run->bridge_ready && !run->restored_rules)
    {
        run->restored_rules = TRUE;
        if (g_hash_table_size(run->breakpoints))
        {
            simulation_write_command(run, "geany-load\n");
            paused = FALSE;
        }
    }
    if (paused && run->pending_event)
    {
        simulation_handle_observer_event(run);
        paused = run->prompt_tail[0] == '>' && run->prompt_tail[1] == ' ';
    }
    gtk_widget_set_sensitive(run->live_controls, paused && !run->preview_pending);
    gtk_widget_set_sensitive(run->breakpoint_controls, run->bridge_ready);
    gtk_widget_set_sensitive(run->source_step_controls, run->source_step_ready);
    gtk_widget_set_sensitive(run->process_controls, run->processes_ready);
    gtk_widget_set_sensitive(run->frame_controls, run->frames_ready);
    if (paused && run->pause_requested)
    {
        gchar *directory = simulation_live_directory(run);
        if (directory)
        {
            gchar *marker = g_build_filename(directory, ".geany-vvp-pause", NULL);
            g_unlink(marker);
            g_free(marker);
            g_free(directory);
        }
        run->pause_requested = FALSE;
    }
    gtk_widget_set_sensitive(run->pause_button, running && run->bridge_ready && !paused &&
                             !run->pause_requested);
    if (run->live_port_send)
        gtk_widget_set_sensitive(run->live_port_send, running);
    if (run->live_port_samples && count > 0 && g_hash_table_size(run->live_port_samples))
    {
        GList *keys = g_list_sort(g_hash_table_get_keys(run->live_port_samples), (GCompareFunc)g_strcmp0);
        GString *readings = g_string_new(NULL);
        for (GList *key = keys; key; key = key->next)
        {
            const gchar *bits = g_hash_table_lookup(run->live_port_samples, key->data);
            const gchar *lamp = bits[1] ? "" : bits[0] == '1' ? "●  " : bits[0] == '0' ? "○  " : "◉  ";
            guint64 number;
            g_string_append_printf(readings, "%s%s = %s", lamp, (const gchar *)key->data, bits);
            if (bits[1] && instrument_number(bits, &number))
                g_string_append_printf(readings, "  (%" G_GUINT64_FORMAT ")", number);
            g_string_append_c(readings, '\n');
        }
        gtk_label_set_text(GTK_LABEL(run->live_port_display), readings->str);
        g_string_free(readings, TRUE);
        g_list_free(keys);
    }
    gtk_widget_set_sensitive(run->source_button, paused && run->trace_file != NULL);
    return G_SOURCE_CONTINUE;
}

static void simulation_live_input(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    gchar *port = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(run->live_port_input));
    const gchar *bits = gtk_entry_get_text(GTK_ENTRY(run->live_port_value));
    gchar *directory = simulation_live_directory(run);
    if (port && directory && *bits && strlen(bits) <= 64 && strspn(bits, "01xz") == strlen(bits))
    {
        gchar *basename = g_strdup_printf("geany-input-%s", port);
        gchar *path = g_build_filename(directory, basename, NULL);
        if (!g_file_set_contents(path, bits, -1, NULL))
            gtk_widget_set_tooltip_text(run->live_port_send, "Could not update the simulation input.");
        g_free(path);
        g_free(basename);
    }
    else
        gtk_widget_set_tooltip_text(run->live_port_send, "Choose an input and enter up to 64 binary digits (0, 1, x or z).");
    g_free(directory);
    g_free(port);
}

static void simulation_live_panel(WaveSimulation *run, GtkWidget *box)
{
    const gchar *marker = "$fopen(\"geany-input-";
    GtkWidget *panel = gtk_expander_new("Live Port I/O");
    GtkWidget *contents = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(panel), contents);
    gtk_box_pack_start(GTK_BOX(box), panel, FALSE, FALSE, 0);
    run->live_port_samples = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    run->live_port_input = gtk_combo_box_text_new();
    const gchar *cursor = run->port_harness;
    guint count = 0;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '"');
        if (!end)
            break;
        gchar *name = g_strndup(cursor, end - cursor);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(run->live_port_input), name);
        g_free(name);
        cursor = end;
        count++;
    }
    if (count)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(run->live_port_input), 0);
        run->live_port_value = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->live_port_value), 64);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->live_port_value), "Binary value (0, 1, x, z)");
        run->live_port_send = gtk_button_new_with_label("Apply Input");
        gtk_widget_set_sensitive(run->live_port_send, FALSE);
        g_signal_connect(run->live_port_send, "clicked", G_CALLBACK(simulation_live_input), run);
        gtk_box_pack_start(GTK_BOX(row), run->live_port_input, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), run->live_port_value, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(row), run->live_port_send, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(contents), row, FALSE, FALSE, 0);
    }
    else
        gtk_widget_destroy(row);
    run->live_port_display = gtk_label_new("Waiting for port samples…");
    gtk_label_set_selectable(GTK_LABEL(run->live_port_display), TRUE);
    gtk_label_set_xalign(GTK_LABEL(run->live_port_display), 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, -1, 105);
    gtk_container_add(GTK_CONTAINER(scroll), run->live_port_display);
    gtk_box_pack_start(GTK_BOX(contents), scroll, TRUE, TRUE, 0);
    gtk_expander_set_expanded(GTK_EXPANDER(panel), TRUE);
}

/* Send one command while the console is at a simulator prompt. */
/* Отправляет команду, когда консоль находится на приглашении симулятора. */
static void simulation_write_command(WaveSimulation *run, const gchar *command)
{
    if (run->preview_pending || run->prompt_tail[0] != '>' || run->prompt_tail[1] != ' ')
        return;
    g_mutex_lock(&run->lock);
    GSubprocess *process = run->live_process ? g_object_ref(run->live_process) : NULL;
    g_mutex_unlock(&run->lock);
    if (!process)
        return;

    run->prompt_tail[0] = run->prompt_tail[1] = 0;
    if (!strcmp(command, "cont\n") || !strcmp(command, "step\n") ||
        g_str_has_prefix(command, "geany-step-"))
    {
        g_clear_pointer(&run->trace_file, g_free);
        gtk_widget_set_sensitive(run->source_button, FALSE);
    }
    gtk_widget_set_sensitive(run->live_controls, FALSE);
    GOutputStream *input = g_subprocess_get_stdin_pipe(process);
    g_output_stream_write_all(input, command, strlen(command), NULL, NULL, NULL);
    g_object_unref(process);
}

/* Report a configuration save failure without discarding the active session. */
/* Сообщает об ошибке записи настроек, сохраняя текущий сеанс. */
static void simulation_report_settings(WaveSimulation *run)
{
    if (!simulation_save_settings(run))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, "\nCould not save Verilog debug settings.\n", -1);
    }
}

/* Find a parked rule by its original file and line. */
/* Ищет неактивное правило по исходному файлу и номеру строки. */
static gint simulation_unresolved_index(const WaveSimulation *run, const gchar *key)
{
    gsize length = strlen(key);
    for (guint i = 0; i < run->unresolved_records->len; i++)
    {
        const gchar *record = run->unresolved_records->pdata[i];
        if (!strncmp(record, key, length) && record[length] == '\t')
            return (gint)i;
    }
    return -1;
}

/* Report parked points without sending them to the simulator. */
/* Показывает неактивные точки без передачи симулятору. */
static void simulation_list_unresolved(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    GString *text = g_string_new("\n");
    g_string_append_printf(text, "Unresolved source points: %u\n", run->unresolved_records->len);
    for (guint i = 0; i < run->unresolved_records->len; i++)
    {
        gchar **parts = g_strsplit(run->unresolved_records->pdata[i], "\t", 7);
        g_string_append_printf(text, "  %s:%s\n", parts[1], parts[0]);
        g_strfreev(parts);
    }
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gchar *valid = g_utf8_make_valid(text->str, -1);
    gtk_text_buffer_insert(buffer, &end, valid, -1);
    g_free(valid);
    g_string_free(text, TRUE);
}

/* Send the complete enabled source-breakpoint set to the paused runtime. */
/* Передаёт остановленному симулятору полный набор включённых точек. */
static void simulation_edit_breakpoint(GtkButton *button, gpointer data)
{
    WaveSimulation *run = data;
    if (!run->bridge_ready || run->prompt_tail[0] != '>' || run->prompt_tail[1] != ' ')
        return;

    gint action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "point-action"));
    gint index = gtk_combo_box_get_active(GTK_COMBO_BOX(run->breakpoint_file));
    gint line = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(run->breakpoint_line));
    const gchar *filename = index >= 0 ? run->sources[index] : NULL;
    if (action != 2 && (!filename || strpbrk(filename, "\r\n\t")))
        return;

    gchar *key = action == 2 ? NULL : g_strdup_printf("%d\t%s", line, filename);
    gchar *rule = NULL;
    if (action == 0)
    {
        const gchar *signal = gtk_entry_get_text(GTK_ENTRY(run->rule_signal));
        gchar *bits = g_ascii_strdown(gtk_entry_get_text(GTK_ENTRY(run->rule_bits)), -1);
        if (((*signal == '\0') != (*bits == '\0')) || strpbrk(signal, "\r\n\t ") ||
            strspn(bits, "01xz") != strlen(bits) ||
            (g_hash_table_size(run->breakpoints) + run->unresolved_records->len >= 128 &&
             !g_hash_table_contains(run->breakpoints, key) && simulation_unresolved_index(run, key) < 0))
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(buffer, &end,
                "\nEnter a full signal name and all bits, or leave both empty. Limit: 128 points.\n", -1);
            g_free(bits);
            g_free(key);
            return;
        }
        gint hits = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(run->rule_hits));
        gboolean log_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(run->rule_log));
        gboolean scripted = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(run->rule_script));
        if (log_only && scripted)
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_insert(buffer, &end, "\nChoose either Log and continue or Lua decides.\n", -1);
            g_free(bits);
            g_free(key);
            return;
        }
        rule = g_strdup_printf("%s\t%s\t%d\t%d\t%d", signal, bits, hits, log_only, scripted);
        g_free(bits);
    }

    gchar *directory = simulation_live_directory(run);
    if (directory)
    {
        GString *contents = g_string_new(NULL);
        if (action != 2)
        {
            GHashTableIter iter;
            gpointer old_key, old_rule;
            g_hash_table_iter_init(&iter, run->breakpoints);
            while (g_hash_table_iter_next(&iter, &old_key, &old_rule))
            {
                if (strcmp(old_key, key))
                    g_string_append_printf(contents, "%s\t%s\n", (gchar *)old_key, (gchar *)old_rule);
            }
            if (rule)
                g_string_append_printf(contents, "%s\t%s\n", key, rule);
        }
        gchar *path = g_build_filename(directory, ".geany-vvp-breakpoints", NULL);
        if (g_file_set_contents(path, contents->str, (gssize)contents->len, NULL))
        {
            if (action == 2)
            {
                g_hash_table_remove_all(run->breakpoints);
                g_hash_table_remove_all(run->point_anchors);
                g_ptr_array_set_size(run->unresolved_records, 0);
                g_ptr_array_set_size(run->unresolved_anchors, 0);
            }
            else
            {
                gint unresolved;
                while ((unresolved = simulation_unresolved_index(run, key)) >= 0)
                {
                    g_ptr_array_remove_index(run->unresolved_records, (guint)unresolved);
                    g_ptr_array_remove_index(run->unresolved_anchors, (guint)unresolved);
                }
                if (action == 1)
                {
                    g_hash_table_remove(run->breakpoints, key);
                    g_hash_table_remove(run->point_anchors, key);
                }
                else
                {
                    WavePointSource *source = simulation_point_source(filename);
                    gchar *anchor = simulation_point_anchor(source, (guint)line);
                    if (source)
                        simulation_point_source_free(source);
                    g_hash_table_replace(run->point_anchors, g_strdup(key), anchor);
                    g_hash_table_replace(run->breakpoints, g_steal_pointer(&key), g_steal_pointer(&rule));
                }
            }
            simulation_report_settings(run);
            simulation_write_command(run, "geany-load\n");
        }
        g_free(path);
        g_string_free(contents, TRUE);
        g_free(directory);
    }
    g_free(key);
    g_free(rule);
}

/* Forward a fixed debugger command from a console button. */
/* Передаёт фиксированную команду от кнопки консоли. */
static void simulation_send_command(GtkButton *button, gpointer data)
{
    const gchar *command = g_object_get_data(G_OBJECT(button), "vvp-command");
    simulation_write_command(data, command);
}

/* Select a paused simulator process by its numeric session identifier. */
/* Выбирает процесс остановленного симулятора по номеру в текущем сеансе. */
static void simulation_select_process(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    const gchar *number = gtk_entry_get_text(GTK_ENTRY(run->process_number));
    guint64 process;
    if (!g_ascii_string_to_unsigned(number, 10, 1, G_MAXUINT64, &process, NULL))
        return;

    gchar *command = g_strdup_printf("geany-select-process %" G_GUINT64_FORMAT "\n", process);
    simulation_write_command(run, command);
    g_free(command);
}

/* Accept simple identifiers that can be sent as one simulator argument. */
/* Принимает простые имена, которые передаются одним аргументом симулятора. */
static gboolean simulation_simple_name(const gchar *name)
{
    return (g_ascii_isalpha(*name) || *name == '_' || *name == '$') &&
        strspn(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_$") == strlen(name);
}

/* Change the inspection frame while retaining the process for stepping. */
/* Меняет кадр просмотра, сохраняя процесс для пошагового выполнения. */
static void simulation_select_frame(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    gint depth = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(run->frame_depth));
    gchar *command = g_strdup_printf("geany-select-frame %d\n", depth);
    simulation_write_command(run, command);
    g_free(command);
}

/* Send a validated memory name and signed word index to the paused bridge. */
/* Передаёт проверенное имя памяти и знаковый индекс остановленному мосту. */
static void simulation_read_memory_word(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(run->memory_word));
    const gchar *opening = strchr(text, '[');
    const gchar *closing = opening ? strchr(opening + 1, ']') : NULL;
    if (!closing || closing[1])
        return;

    gchar *name = g_strndup(text, opening - text);
    gchar *index_text = g_strndup(opening + 1, closing - opening - 1);
    gint64 index;
    if (simulation_simple_name(name) &&
        g_ascii_string_to_signed(index_text, 10, G_MININT, G_MAXINT, &index, NULL))
    {
        gchar *command = g_strdup_printf("geany-read-memory %s %" G_GINT64_FORMAT "\n", name, index);
        simulation_write_command(run, command);
        g_free(command);
    }
    g_free(index_text);
    g_free(name);
}

/* Query a simple signal or enter one scope in the current simulator context. */
/* Запрашивает простой сигнал или входит в область текущего контекста симулятора. */
static void simulation_inspect_name(GtkButton *button, gpointer data)
{
    WaveSimulation *run = data;
    const gchar *name = gtk_entry_get_text(GTK_ENTRY(run->signal_name));
    if (!simulation_simple_name(name))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, "\nEnter one signal or scope name from List.\n", -1);
        return;
    }
    gboolean scope = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "scope"));
    gchar *command = g_strdup_printf(scope ? "push %s\n" : "$display \"%%b\" %s\n", name);
    simulation_write_command(run, command);
    g_free(command);
}

/* Parse a snapshot of the capture while the simulator waits at its prompt. */
/* Разбирает снимок VCD, пока симулятор ожидает команду. */
static void simulation_preview_worker(GTask *task, gpointer object, gpointer data, GCancellable *cancel)
{
    (void)object;
    GError *error = NULL;
    gchar *path = simulation_find_vcd(data, &error);
    Capture *capture = path ? read_capture_file(path, cancel, &error) : NULL;
    g_free(path);
    if (capture)
        g_task_return_pointer(task, capture, capture_free);
    else
        g_task_return_error(task, error);
}

/* Show the paused snapshot only while its simulation dialog still exists. */
/* Показывает снимок на паузе, пока диалог симуляции ещё открыт. */
static void simulation_preview_done(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)data;
    GtkWidget *dialog = GTK_WIDGET(object);
    WaveSimulation *run = g_object_get_data(object, "insight-live-simulation");
    GError *error = NULL;
    Capture *capture = g_task_propagate_pointer(G_TASK(result), &error);
    if (run)
    {
        run->preview_pending = FALSE;
        if (capture)
        {
            GtkWidget *preview = show_capture(GTK_WINDOW(dialog), capture, NULL, run->navigate);
            gtk_window_set_modal(GTK_WINDOW(preview), TRUE);
        }
        else
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(buffer, &end);
            gchar *message = g_strdup_printf("\nWaveform preview: %s\n",
                                             error ? error->message : "No capture available.");
            gchar *valid = g_utf8_make_valid(message, -1);
            gtk_text_buffer_insert(buffer, &end, valid, -1);
            g_free(valid);
            g_free(message);
        }
    }
    else if (capture)
        capture_free(capture);
    g_clear_error(&error);
}

/* Open the VCD written up to the current vvp stop. */
/* Открывает VCD, записанный к текущей остановке vvp. */
static void simulation_preview(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    if (run->preview_pending || run->prompt_tail[0] != '>' || run->prompt_tail[1] != ' ')
        return;
    g_mutex_lock(&run->lock);
    gchar *directory = run->live_process ? g_strdup(run->live_directory) : NULL;
    g_mutex_unlock(&run->lock);
    if (!directory)
        return;
    run->preview_pending = TRUE;
    gtk_widget_set_sensitive(run->live_controls, FALSE);
    GTask *task = g_task_new(run->dialog, NULL, simulation_preview_done, NULL);
    g_task_set_task_data(task, directory, g_free);
    g_task_run_in_thread(task, simulation_preview_worker);
    g_object_unref(task);
}

/* Populate the rule fields when a saved file-and-line pair is selected. */
/* Заполняет поля правила при выборе сохранённой пары файла и строки. */
static void simulation_rule_selected(GtkWidget *widget, gpointer data)
{
    (void)widget;
    WaveSimulation *run = data;
    gint index = gtk_combo_box_get_active(GTK_COMBO_BOX(run->breakpoint_file));
    if (index < 0)
        return;
    gint line = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(run->breakpoint_line));
    gchar *key = g_strdup_printf("%d\t%s", line, run->sources[index]);
    const gchar *rule = g_hash_table_lookup(run->breakpoints, key);
    gint unresolved = rule ? -1 : simulation_unresolved_index(run, key);
    if (unresolved >= 0)
    {
        const gchar *record = run->unresolved_records->pdata[unresolved];
        rule = record + strlen(key) + 1;
    }
    gchar **fields = rule ? g_strsplit(rule, "\t", 5) : NULL;
    gboolean valid = fields && g_strv_length(fields) == 5;
    gtk_entry_set_text(GTK_ENTRY(run->rule_signal), valid ? fields[0] : "");
    gtk_entry_set_text(GTK_ENTRY(run->rule_bits), valid ? fields[1] : "");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(run->rule_hits),
                              valid ? g_ascii_strtoll(fields[2], NULL, 10) : 1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(run->rule_log), valid && !strcmp(fields[3], "1"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(run->rule_script), valid && !strcmp(fields[4], "1"));
    g_strfreev(fields);
    g_free(key);
}

/* Load a bounded Lua observer into the current paused session. */
/* Загружает ограниченный по размеру Lua-сценарий в текущий сеанс на паузе. */
static void simulation_load_observer(GtkButton *button, gpointer data)
{
    WaveSimulation *run = data;
    gchar *filename = NULL;
    if (GTK_WIDGET(button) == run->reload_script)
        filename = g_strdup(run->observer_file);
    else
    {
        GtkWidget *parent = g_object_ref(run->dialog);
        GtkWidget *chooser = gtk_file_chooser_dialog_new("Load Verilog Observer", GTK_WINDOW(parent),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Load", GTK_RESPONSE_ACCEPT, NULL);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(chooser), TRUE);
        GtkFileFilter *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Lua scripts");
        gtk_file_filter_add_pattern(filter, "*.[lL][uU][aA]");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
        if (run->observer_file)
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), run->observer_file);
        filename = gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT ?
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)) : NULL;
        gtk_widget_destroy(chooser);
        gboolean active = g_object_get_data(G_OBJECT(parent), "insight-live-simulation") == run;
        g_object_unref(parent);
        if (!active)
        {
            g_free(filename);
            return;
        }
    }
    if (!filename)
        return;

    gchar *source = g_malloc(64 * 1024 + 1);
    GFile *file = g_file_new_for_path(filename);
    GFileInputStream *stream = g_file_read(file, NULL, NULL);
    gsize length = 0;
    gboolean loaded = stream && g_input_stream_read_all(G_INPUT_STREAM(stream), source,
        64 * 1024 + 1, &length, NULL, NULL) && length > 0 && length <= 64 * 1024 &&
        !memchr(source, 0, length) && g_utf8_validate(source, (gssize)length, NULL);
    gchar error[1024] = "";
    if (loaded && !run->lua_observer)
        run->lua_observer = insight_lua_debug_new_observer();
    gboolean applied = loaded && run->lua_observer &&
        insight_lua_debug_set_observer(run->lua_observer, source, length, error, sizeof error);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gchar *message = g_utf8_make_valid(applied ? "\nLua observer loaded.\n" :
        *error ? error : "\nCannot load observer. Choose a UTF-8 Lua script of at most 64 KiB.\n", -1);
    gtk_text_buffer_insert(buffer, &end, message, -1);
    if (applied)
    {
        run->observer_log_offset = 0;
        g_free(run->observer_file);
        run->observer_file = g_strdup(filename);
        gtk_widget_set_sensitive(run->reload_script, TRUE);
        simulation_report_settings(run);
        simulation_observer_output(run);
    }
    g_free(message);
    g_clear_object(&stream);
    g_object_unref(file);
    g_free(source);
    g_free(filename);
}

/* Disable the current observer while keeping its file available for reload. */
/* Отключает текущий сценарий, оставляя файл доступным для повторной загрузки. */
static void simulation_disable_observer(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    g_clear_pointer(&run->lua_observer, insight_lua_debug_free);
    run->observer_log_offset = 0;
    simulation_observer_output(run);
}

/* Close the progress dialog and show either the capture or the diagnostic. */
/* Закрывает диалог выполнения и показывает диаграмму либо диагностику. */
static void simulation_done(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object;
    (void)data;
    GTask *task = G_TASK(result);
    WaveSimulation *run = g_task_get_task_data(task);
    GtkWindow *parent = g_weak_ref_get(&run->parent);
    GError *error = NULL;
    Capture *capture = g_task_propagate_pointer(task, &error);
    if (run->refresh_source)
    {
        g_source_remove(run->refresh_source);
        run->refresh_source = 0;
    }
    gtk_widget_destroy(run->dialog);
    if (capture && parent)
        show_capture(parent, capture, NULL, run->navigate);
    else if (capture)
        capture_free(capture);
    else if (parent && error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error->message);
        g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
        gtk_widget_show(dialog);
    }
    g_clear_error(&error);
    g_clear_object(&parent);
}

/* Stop a running testbench when the user closes the progress dialog. */
/* Останавливает тестбенч при закрытии диалога выполнения. */
static void simulation_cancel(GtkDialog *dialog, gint response, gpointer data)
{
    (void)response;
    gtk_widget_hide(GTK_WIDGET(dialog));
    g_cancellable_cancel(data);
}

/* Stop polling before GTK destroys the console widgets. */
/* Прекращает опрос до уничтожения виджетов консоли GTK. */
static void simulation_dialog_destroy(GtkWidget *dialog, gpointer data)
{
    (void)dialog;
    WaveSimulation *run = data;
    g_object_set_data(G_OBJECT(dialog), "insight-live-simulation", NULL);
    if (run->refresh_source)
    {
        g_source_remove(run->refresh_source);
        run->refresh_source = 0;
    }
}

/* Launch Icarus and open the resulting VCD on completion. */
/* Запускает Icarus и открывает полученный VCD после завершения. */
void insight_waveforms_simulate(GtkWindow *parent, const gchar *bin, const gchar *include_dir,
                                const gchar *const *sources, gboolean interactive, InsightWaveSource navigate,
                                InsightWaveLocation open_location,
                                const gchar *settings_directory, const gchar *project,
                                gboolean configure_ports)
{
#ifdef G_OS_WIN32
    const gchar *compiler_name = "iverilog.exe", *runtime_name = "vvp.exe";
#else
    const gchar *compiler_name = "iverilog", *runtime_name = "vvp";
#endif
    gchar *compiler = simulation_tool(bin, compiler_name);
    gchar *runtime = simulation_tool(bin, runtime_name);
    if (!compiler || !runtime)
    {
        GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Icarus Verilog is unavailable. Install the editor with its simulator.");
        g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
        gtk_widget_show(dialog);
        g_free(compiler);
        g_free(runtime);
        return;
    }

    WaveSimulation *run = g_new0(WaveSimulation, 1);
    g_weak_ref_init(&run->parent, parent);
    g_mutex_init(&run->lock);
    g_cond_init(&run->live_output_space);
    run->interactive = interactive;
    run->compiler = compiler;
    run->runtime = runtime;
    run->include_dir = g_strdup(include_dir);
    run->sources = g_strdupv((gchar **)sources);
    run->navigate = navigate;
    run->open_location = open_location;
    if (configure_ports)
    {
        gchar *key = simulation_settings_path(settings_directory, project, run->sources);
        run->port_settings = g_strconcat(key, ".ports", NULL);
        g_free(key);
        if (!simulation_edit_ports(parent, run->port_settings, &run->port_harness))
        {
            simulation_free(run);
            return;
        }
    }
    run->dialog = gtk_dialog_new_with_buttons("Verilog Simulation", parent,
        GTK_DIALOG_DESTROY_WITH_PARENT | (interactive ? 0 : GTK_DIALOG_MODAL),
        "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    g_object_ref_sink(run->dialog);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(run->dialog));
    GtkWidget *label = gtk_label_new(interactive ?
        "Compiling. Controls become available when vvp is paused." :
        "Compiling and running the Verilog testbench…");
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 12);
    if (interactive)
    {
        gtk_window_set_default_size(GTK_WINDOW(run->dialog), 680, 400);
        run->protocol_line = g_string_new(NULL);
        run->live_output = g_byte_array_new();
        run->breakpoints = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        run->point_anchors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        run->unresolved_records = g_ptr_array_new_with_free_func(g_free);
        run->unresolved_anchors = g_ptr_array_new_with_free_func(g_free);
        run->settings_path = simulation_settings_path(settings_directory, project, run->sources);
        simulation_load_settings(run);
        GtkWidget *running_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(box), running_actions, FALSE, FALSE, 0);
        if (run->port_settings)
        {
            GtkWidget *ports = gtk_button_new_with_label("Port Signals…");
            g_signal_connect(ports, "clicked", G_CALLBACK(simulation_port_settings), run);
            gtk_box_pack_start(GTK_BOX(running_actions), ports, FALSE, FALSE, 0);
        }
        if (run->port_harness && strstr(run->port_harness, "task geany_live_yield;"))
            simulation_live_panel(run, box);
        run->pause_button = gtk_button_new_with_label("Pause");
        gtk_widget_set_sensitive(run->pause_button, FALSE);
        g_signal_connect(run->pause_button, "clicked", G_CALLBACK(simulation_request_pause), run);
        gtk_box_pack_start(GTK_BOX(running_actions), run->pause_button, FALSE, FALSE, 0);
        run->source_button = gtk_button_new_with_label("Open HDL Line");
        gtk_widget_set_sensitive(run->source_button, FALSE);
        g_signal_connect(run->source_button, "clicked", G_CALLBACK(simulation_open_location), run);
        gtk_box_pack_start(GTK_BOX(running_actions), run->source_button, FALSE, FALSE, 0);
        run->console = gtk_text_view_new();
        if (run->moved_points || run->unresolved_records->len)
        {
            gchar *summary = g_strdup_printf("Restored points: %u moved, %u unresolved.\n",
                                             run->moved_points, run->unresolved_records->len);
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->console)), summary, -1);
            g_free(summary);
        }
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(run->console), TRUE);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(run->console), FALSE);
        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_container_add(GTK_CONTAINER(scroll), run->console);
        gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
        run->live_controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_pack_start(GTK_BOX(box), run->live_controls, FALSE, FALSE, 6);
        GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->live_controls), actions, FALSE, FALSE, 0);
        const gchar *labels[] = {"Step Event", "Continue", "Time", "Scope", "List", "Finish"};
        const gchar *commands[] = {"step\n", "cont\n", "time\n", "where\n", "list\n", "finish\n"};
        for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
        {
            GtkWidget *button = gtk_button_new_with_label(labels[i]);
            g_object_set_data(G_OBJECT(button), "vvp-command", (gpointer)commands[i]);
            g_signal_connect(button, "clicked", G_CALLBACK(simulation_send_command), run);
            gtk_box_pack_start(GTK_BOX(actions), button, FALSE, FALSE, 0);
        }
        run->source_step_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->live_controls), run->source_step_controls, FALSE, FALSE, 0);
        const gchar *step_labels[] = {"Step HDL", "Step Current Process", "Next Tick"};
        const gchar *step_commands[] = {"geany-step-source\n", "geany-step-process\n", "geany-step-tick\n"};
        for (guint i = 0; i < G_N_ELEMENTS(step_labels); i++)
        {
            GtkWidget *button = gtk_button_new_with_label(step_labels[i]);
            g_object_set_data(G_OBJECT(button), "vvp-command", (gpointer)step_commands[i]);
            g_signal_connect(button, "clicked", G_CALLBACK(simulation_send_command), run);
            gtk_box_pack_start(GTK_BOX(run->source_step_controls), button, FALSE, FALSE, 0);
        }
        gtk_widget_set_sensitive(run->source_step_controls, FALSE);
        run->process_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->live_controls), run->process_controls, FALSE, FALSE, 0);
        GtkWidget *processes = gtk_button_new_with_label("Processes");
        g_object_set_data(G_OBJECT(processes), "vvp-command", "geany-processes\n");
        g_signal_connect(processes, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(run->process_controls), processes, FALSE, FALSE, 0);
        run->process_number = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->process_number), 20);
        gtk_entry_set_width_chars(GTK_ENTRY(run->process_number), 10);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->process_number), "Process ID");
        gtk_box_pack_start(GTK_BOX(run->process_controls), run->process_number, FALSE, FALSE, 0);
        GtkWidget *select_process = gtk_button_new_with_label("Select Process");
        g_signal_connect(select_process, "clicked", G_CALLBACK(simulation_select_process), run);
        gtk_box_pack_start(GTK_BOX(run->process_controls), select_process, FALSE, FALSE, 0);
        GtkWidget *process_values = gtk_button_new_with_label("Process Values");
        g_object_set_data(G_OBJECT(process_values), "vvp-command", "geany-process-values\n");
        g_signal_connect(process_values, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(run->process_controls), process_values, FALSE, FALSE, 0);
        gtk_widget_set_sensitive(run->process_controls, FALSE);
        run->frame_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->live_controls), run->frame_controls, FALSE, FALSE, 0);
        run->frame_depth = gtk_spin_button_new_with_range(0, 63, 1);
        gtk_box_pack_start(GTK_BOX(run->frame_controls), run->frame_depth, FALSE, FALSE, 0);
        GtkWidget *frame = gtk_button_new_with_label("Select Frame");
        g_signal_connect(frame, "clicked", G_CALLBACK(simulation_select_frame), run);
        gtk_box_pack_start(GTK_BOX(run->frame_controls), frame, FALSE, FALSE, 0);
        run->memory_word = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->memory_word), 128);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->memory_word), "name[index]");
        gtk_box_pack_start(GTK_BOX(run->frame_controls), run->memory_word, TRUE, TRUE, 0);
        GtkWidget *memory = gtk_button_new_with_label("Read Memory Word");
        gtk_widget_set_tooltip_text(memory, "Read an element of memory, a dynamic array, or a queue");
        g_signal_connect(memory, "clicked", G_CALLBACK(simulation_read_memory_word), run);
        gtk_box_pack_start(GTK_BOX(run->frame_controls), memory, FALSE, FALSE, 0);
        gtk_widget_set_sensitive(run->frame_controls, FALSE);
        GtkWidget *inspection = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->live_controls), inspection, FALSE, FALSE, 0);
        run->signal_name = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->signal_name), 128);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->signal_name), "Signal or scope");
        gtk_box_pack_start(GTK_BOX(inspection), run->signal_name, TRUE, TRUE, 0);
        GtkWidget *read = gtk_button_new_with_label("Read Signal");
        g_signal_connect(read, "clicked", G_CALLBACK(simulation_inspect_name), run);
        gtk_box_pack_start(GTK_BOX(inspection), read, FALSE, FALSE, 0);
        GtkWidget *scope = gtk_button_new_with_label("Enter Scope");
        g_object_set_data(G_OBJECT(scope), "scope", GINT_TO_POINTER(1));
        g_signal_connect(scope, "clicked", G_CALLBACK(simulation_inspect_name), run);
        gtk_box_pack_start(GTK_BOX(inspection), scope, FALSE, FALSE, 0);
        GtkWidget *up = gtk_button_new_with_label("Parent Scope");
        g_object_set_data(G_OBJECT(up), "vvp-command", "pop\n");
        g_signal_connect(up, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(inspection), up, FALSE, FALSE, 0);

        GtkWidget *preview = gtk_button_new_with_label("Preview Waveform");
        g_signal_connect(preview, "clicked", G_CALLBACK(simulation_preview), run);
        gtk_box_pack_start(GTK_BOX(actions), preview, FALSE, FALSE, 0);
        GtkWidget *trace = gtk_button_new_with_label("Trace On");
        g_object_set_data(G_OBJECT(trace), "vvp-command", "trace on\n");
        g_signal_connect(trace, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(actions), trace, FALSE, FALSE, 0);
        GtkWidget *trace_off = gtk_button_new_with_label("Trace Off");
        g_object_set_data(G_OBJECT(trace_off), "vvp-command", "trace off\n");
        g_signal_connect(trace_off, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(actions), trace_off, FALSE, FALSE, 0);
        run->breakpoint_controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_box_pack_start(GTK_BOX(run->live_controls), run->breakpoint_controls, FALSE, FALSE, 0);
        GtkWidget *location = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->breakpoint_controls), location, FALSE, FALSE, 0);
        run->breakpoint_file = gtk_combo_box_text_new();
        for (guint i = 0; run->sources[i]; i++)
        {
            gchar *name = g_filename_display_basename(run->sources[i]);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(run->breakpoint_file), name);
            g_free(name);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(run->breakpoint_file), 0);
        gtk_box_pack_start(GTK_BOX(location), run->breakpoint_file, TRUE, TRUE, 0);
        run->breakpoint_line = gtk_spin_button_new_with_range(1, G_MAXINT, 1);
        gtk_box_pack_start(GTK_BOX(location), run->breakpoint_line, FALSE, FALSE, 0);
        GtkWidget *toggle = gtk_button_new_with_label("Set Point");
        g_signal_connect(toggle, "clicked", G_CALLBACK(simulation_edit_breakpoint), run);
        gtk_box_pack_start(GTK_BOX(location), toggle, FALSE, FALSE, 0);
        GtkWidget *remove = gtk_button_new_with_label("Remove Point");
        g_object_set_data(G_OBJECT(remove), "point-action", GINT_TO_POINTER(1));
        g_signal_connect(remove, "clicked", G_CALLBACK(simulation_edit_breakpoint), run);
        gtk_box_pack_start(GTK_BOX(location), remove, FALSE, FALSE, 0);
        GtkWidget *clear = gtk_button_new_with_label("Clear Points");
        g_object_set_data(G_OBJECT(clear), "point-action", GINT_TO_POINTER(2));
        g_signal_connect(clear, "clicked", G_CALLBACK(simulation_edit_breakpoint), run);
        gtk_box_pack_start(GTK_BOX(location), clear, FALSE, FALSE, 0);
        GtkWidget *list_points = gtk_button_new_with_label("List Points");
        g_object_set_data(G_OBJECT(list_points), "vvp-command", "geany-list\n");
        g_signal_connect(list_points, "clicked", G_CALLBACK(simulation_send_command), run);
        gtk_box_pack_start(GTK_BOX(location), list_points, FALSE, FALSE, 0);
        GtkWidget *unresolved = gtk_button_new_with_label("Unresolved Points");
        g_signal_connect(unresolved, "clicked", G_CALLBACK(simulation_list_unresolved), run);
        gtk_box_pack_start(GTK_BOX(location), unresolved, FALSE, FALSE, 0);
        GtkWidget *rule = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->breakpoint_controls), rule, FALSE, FALSE, 0);
        run->rule_signal = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->rule_signal), 512);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->rule_signal), "Full signal name");
        gtk_box_pack_start(GTK_BOX(rule), run->rule_signal, TRUE, TRUE, 0);
        run->rule_bits = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(run->rule_bits), 1024);
        gtk_entry_set_placeholder_text(GTK_ENTRY(run->rule_bits), "Binary value");
        gtk_box_pack_start(GTK_BOX(rule), run->rule_bits, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(rule), gtk_label_new("Hits"), FALSE, FALSE, 0);
        run->rule_hits = gtk_spin_button_new_with_range(1, 1000000, 1);
        gtk_box_pack_start(GTK_BOX(rule), run->rule_hits, FALSE, FALSE, 0);
        run->rule_log = gtk_check_button_new_with_label("Log and continue");
        gtk_box_pack_start(GTK_BOX(rule), run->rule_log, FALSE, FALSE, 0);
        run->rule_script = gtk_check_button_new_with_label("Lua decides");
        gtk_box_pack_start(GTK_BOX(rule), run->rule_script, FALSE, FALSE, 0);
        g_signal_connect(run->breakpoint_file, "changed", G_CALLBACK(simulation_rule_selected), run);
        g_signal_connect(run->breakpoint_line, "value-changed", G_CALLBACK(simulation_rule_selected), run);
        simulation_rule_selected(NULL, run);
        GtkWidget *load_script = gtk_button_new_with_label("Load Lua Observer…");
        g_signal_connect(load_script, "clicked", G_CALLBACK(simulation_load_observer), run);
        GtkWidget *scripts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(run->breakpoint_controls), scripts, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(scripts), load_script, FALSE, FALSE, 0);
        run->reload_script = gtk_button_new_with_label("Reload Saved Lua");
        gtk_widget_set_sensitive(run->reload_script, run->observer_file != NULL);
        g_signal_connect(run->reload_script, "clicked", G_CALLBACK(simulation_load_observer), run);
        gtk_box_pack_start(GTK_BOX(scripts), run->reload_script, FALSE, FALSE, 0);
        GtkWidget *disable_script = gtk_button_new_with_label("Disable Lua");
        g_signal_connect(disable_script, "clicked", G_CALLBACK(simulation_disable_observer), run);
        gtk_box_pack_start(GTK_BOX(scripts), disable_script, FALSE, FALSE, 0);
        run->lua_watches = gtk_label_new(NULL);
        gtk_label_set_selectable(GTK_LABEL(run->lua_watches), TRUE);
        gtk_label_set_xalign(GTK_LABEL(run->lua_watches), 0);
        gtk_box_pack_start(GTK_BOX(box), run->lua_watches, FALSE, FALSE, 0);
        gtk_widget_set_sensitive(run->breakpoint_controls, FALSE);
        g_object_set_data(G_OBJECT(run->dialog), "insight-live-simulation", run);
        gtk_widget_set_sensitive(run->live_controls, FALSE);
        run->refresh_source = g_timeout_add(100, simulation_refresh_console, run);
    }

    GCancellable *cancel = g_cancellable_new();
    g_signal_connect_object(run->dialog, "response", G_CALLBACK(simulation_cancel), cancel, 0);
    g_signal_connect(run->dialog, "destroy", G_CALLBACK(simulation_dialog_destroy), run);
    g_signal_connect_object(run->dialog, "destroy", G_CALLBACK(g_cancellable_cancel), cancel, G_CONNECT_SWAPPED);
    GTask *task = g_task_new(NULL, cancel, simulation_done, NULL);
    g_task_set_task_data(task, run, simulation_free);
    gtk_widget_show_all(run->dialog);
    g_task_run_in_thread(task, simulation_worker);
    g_object_unref(task);
    g_object_unref(cancel);
}
