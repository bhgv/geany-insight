/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Implements Verilog simulation setup, live instruments, and VCD inspection.
 * RU: Реализует настройку симуляции Verilog, живые приборы и просмотр VCD. */
#include "insight-waveforms.h"
#include "msgwindow.h"
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

#define LIVE_SCOPE_POINTS 512u

typedef struct {
    guint64 time;
    InstrumentPoint point;
} LiveScopeSample;

typedef struct {
    GtkWidget *drawing, *status;
    guint axes, count, next;
    gboolean dirty;
    LiveScopeSample samples[LIVE_SCOPE_POINTS];
} LiveScope;

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
    GPtrArray *port_model_sources;
    GHashTable *port_images;
    GtkWidget *live_devices;
    GtkWidget *live_port_input, *live_port_value, *live_port_send, *live_port_display;
    GHashTable *live_port_samples, *live_indicators;
    GPtrArray *live_scopes;
    gint scope_console_prefix;
    GtkWidget *uart_window, *uart_view, *uart_entry;
    GString *uart_log;
    guint uart_send_sequence;
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
    g_clear_pointer(&run->port_model_sources, g_ptr_array_unref);
    g_clear_pointer(&run->port_images, g_hash_table_unref);
    g_clear_pointer(&run->live_port_samples, g_hash_table_unref);
    g_clear_pointer(&run->live_indicators, g_hash_table_unref);
    g_clear_pointer(&run->live_scopes, g_ptr_array_unref);
    if (run->uart_log)
        g_string_free(run->uart_log, TRUE);
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
    g_hash_table_insert(ports, g_strdup(name), GUINT_TO_POINTER(id + 1));
    g_string_append_printf(body, "%s [%" G_GUINT64_FORMAT ":0] pin%u;\n",
                           wire ? "wire" : "reg", width - 1, id);
    if (connections->len)
        g_string_append_c(connections, ',');
    g_string_append_printf(connections, ".%s(pin%u)", name, id);
    return id;
}

static gchar *simulation_memory_filename(const gchar *anchor)
{
    return *anchor == ':' ? g_strdup_printf("geany-device-%s.lua", anchor + 1) :
                            g_strdup_printf("geany-memory-%s.hex", anchor);
}

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

static void simulation_memory_argument(GString *body, GHashTable *images, const gchar *anchor)
{
    simulation_device_argument(body, images, anchor, NULL);
}

static gboolean simulation_port_sram(gchar **args, GString *body, GString *connections,
                                      GHashTable *ports, GHashTable *images)
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

static gboolean simulation_port_ddr(gchar **args, GString *body, GString *connections,
                                    GHashTable *ports, GHashTable *images, guint generation)
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
        "always @(pin%u) $geany_sdram(", id, ids[8], id, id);
    for (guint i = 0; i < G_N_ELEMENTS(ids); i++)
        g_string_append_printf(body, "pin%u,", ids[i]);
    g_string_append_printf(body, "sdram_drive%u", id);
    simulation_memory_argument(body, images, args[7]);
    return TRUE;
}

static gboolean simulation_port_spi(gchar **args, GString *body, GString *connections,
                                    GHashTable *ports, GHashTable *images, GHashTable *spi_pins,
                                    const gchar *script_key, const gchar *reset)
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
        "always @(pin%u or pin%u", cs, miso, cs, ck, cs);
    if (script_key)
        g_string_append_printf(body, " or spi_start%u", cs);
    if (reset)
        g_string_append_printf(body, " or dut.%s", reset);
    g_string_append_printf(body, ") $geany_spi_%s(pin%u,pin%u,pin%u,spi_drive%u",
                           args[0] + 4, ck, cs, mosi, cs);
    simulation_device_argument(body, images, script_key ? script_key : args[2], reset);
    return TRUE;
}

static gboolean simulation_i2c_address(const gchar *value, guint64 *address)
{
    gboolean hex = g_str_has_prefix(value, "0x") || g_str_has_prefix(value, "0X");
    return g_ascii_string_to_unsigned(value + (hex ? 2 : 0), hex ? 16 : 10, 8, 119, address, NULL);
}

static gboolean simulation_port_i2c(gchar **args, GString *body, GString *connections,
                                    GHashTable *ports, GHashTable *images, GHashTable *buses,
                                    const gchar *script_key, const gchar *reset)
{
    guint64 address;
    if (!simulation_port_name(args[1]) || !simulation_port_name(args[2]) ||
        !strcmp(args[1], args[2]) || !simulation_i2c_address(args[3], &address))
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
        "always @(pin%u or pin%u", id, sda, id, scl, sda);
    if (script_key)
        g_string_append_printf(body, " or i2c_start%u", id);
    if (reset)
        g_string_append_printf(body, " or dut.%s", reset);
    g_string_append_printf(body, ") $geany_i2c_%s(pin%u,pin%u,%" G_GUINT64_FORMAT ",i2c_drive%u",
                           script_key ? "lua" : "eeprom", scl, sda, address, id);
    simulation_device_argument(body, images, script_key ? script_key : args[2], reset);
    return TRUE;
}

static gboolean simulation_port_uart(gchar **args, gint count, GString *body,
                                      GString *connections, GHashTable *ports, GString *live)
{
    guint64 baud;
    if (g_hash_table_size(ports) > 126 || strstr(body->str, "task geany_uart_tx;") ||
        (count == 5 && strcmp(args[4], "console")) ||
        !simulation_port_name(args[1]) || !simulation_port_name(args[2]) ||
        !strcmp(args[1], args[2]) || g_hash_table_contains(ports, args[1]) ||
        g_hash_table_contains(ports, args[2]) ||
        !g_ascii_string_to_unsigned(args[3], 10, 1, 100000000, &baud, NULL))
        return FALSE;
    guint rx = simulation_port_connect(body, connections, ports, args[1], 1, FALSE);
    guint tx = simulation_port_connect(body, connections, ports, args[2], 1, TRUE);
    if (count == 5)
        g_string_append(body, "// GEANY UART CONSOLE\n");
    g_string_append_printf(body,
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
        "end end\n", baud, rx, rx, rx, rx, tx, tx, tx, tx);
    /* UART also needs wall-clock pacing, even without parallel port monitors. */
    g_string_append(live, "// UART pacing\n");
    return TRUE;
}

typedef struct {
    gchar *module;
    GString *pins, *parameters;
    GHashTable *pin_names, *parameter_names;
} SimulationModel;

static void simulation_model_free(gpointer data)
{
    SimulationModel *model = data;
    g_free(model->module);
    g_string_free(model->pins, TRUE);
    g_string_free(model->parameters, TRUE);
    g_hash_table_unref(model->pin_names);
    g_hash_table_unref(model->parameter_names);
    g_free(model);
}

static gboolean simulation_port_expression(const gchar *expression)
{
    return strlen(expression) <= 256 &&
        g_regex_match_simple("^[A-Za-z0-9_$'+*/%&|^~!=<>?:(). -]+$", expression, 0, 0) &&
        !strstr(expression, "//") && !strstr(expression, "/*");
}

static gboolean simulation_model_source(GPtrArray *sources, const gchar *directory,
                                        const gchar *filename)
{
    gchar *native = g_filename_from_utf8(filename, -1, NULL, NULL, NULL);
    if (!native)
        return FALSE;
    gchar *path = insight_canonicalize_filename(native, directory);
    g_free(native);
    const gchar *extension = path ? strrchr(path, '.') : NULL;
    if (!extension || (g_ascii_strcasecmp(extension, ".v") && g_ascii_strcasecmp(extension, ".sv")) ||
        !g_file_test(path, G_FILE_TEST_IS_REGULAR))
    {
        g_free(path);
        return FALSE;
    }
    for (guint i = 0; i < sources->len; i++)
        if (!strcmp(sources->pdata[i], path))
        {
            g_free(path);
            return TRUE;
        }
    if (sources->len >= 32)
    {
        g_free(path);
        return FALSE;
    }
    g_ptr_array_add(sources, path);
    return TRUE;
}

static void simulation_model_binding(GString *entries, GHashTable *names,
                                     const gchar *name, const gchar *expression)
{
    if (entries->len)
        g_string_append_c(entries, ',');
    g_string_append_printf(entries, ".%s(%s)", name, expression);
    g_hash_table_add(names, g_strdup(name));
}

static gboolean simulation_model_rule(gchar **args, gint count, GHashTable *models,
                                      GPtrArray *sources, const gchar *directory,
                                      GString *body, GString *connections, GHashTable *ports)
{
    const gchar *rule = args[0];
    if (!strcmp(rule, "model-source"))
        return count == 2 && simulation_model_source(sources, directory, args[1]);
    if (count < 2)
        return FALSE;
    SimulationModel *model = g_hash_table_lookup(models, args[1]);
    if (!strcmp(rule, "model"))
    {
        if (count != 4 || model || g_hash_table_size(models) >= 8 ||
            !simulation_port_name(args[1]) || !simulation_port_name(args[2]) ||
            !simulation_model_source(sources, directory, args[3]))
            return FALSE;
        model = g_new0(SimulationModel, 1);
        model->module = g_strdup(args[2]);
        model->pins = g_string_new(NULL);
        model->parameters = g_string_new(NULL);
        model->pin_names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        model->parameter_names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        g_hash_table_insert(models, g_strdup(args[1]), model);
        return TRUE;
    }
    if (!model || count < 4 || !simulation_port_name(args[2]))
        return FALSE;
    if (!strcmp(rule, "model-param") || !strcmp(rule, "model-value"))
    {
        gboolean parameter = !strcmp(rule, "model-param");
        GHashTable *names = parameter ? model->parameter_names : model->pin_names;
        if (count != 4 || g_hash_table_contains(names, args[2]) ||
            !simulation_port_expression(args[3]))
            return FALSE;
        simulation_model_binding(parameter ? model->parameters : model->pins, names, args[2], args[3]);
        return TRUE;
    }
    if (!simulation_port_name(args[3]) || g_hash_table_contains(model->pin_names, args[3]))
        return FALSE;
    gpointer existing = g_hash_table_lookup(ports, args[2]);
    guint id;
    if (!strcmp(rule, "model-share") && count == 4 && existing)
        id = GPOINTER_TO_UINT(existing) - 1;
    else if (!strcmp(rule, "model-link") && count == 5 && !existing &&
             g_hash_table_size(ports) < 128)
    {
        guint64 width;
        if (!g_ascii_string_to_unsigned(args[4], 10, 1, 4096, &width, NULL))
            return FALSE;
        id = simulation_port_connect(body, connections, ports, args[2], width, TRUE);
    }
    else
        return FALSE;
    gchar *signal = g_strdup_printf("pin%u", id);
    simulation_model_binding(model->pins, model->pin_names, args[3], signal);
    g_free(signal);
    return TRUE;
}

/* Generate a private testbench from a per-source port recipe. */
static gboolean simulation_memory_image(GHashTable *images, const gchar *anchor,
                                        const gchar *filename, const gchar *directory, GError **error)
{
    if (!simulation_port_name(*anchor == ':' ? anchor + 1 : anchor) || g_hash_table_contains(images, anchor))
        return FALSE;
    gchar *native = g_filename_from_utf8(filename, -1, NULL, NULL, error);
    if (!native)
        return FALSE;
    gchar *path = g_canonicalize_filename(native, directory);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "Device file must be a regular file: %s", filename);
        g_free(path);
        g_free(native);
        return FALSE;
    }
    GFile *file = g_file_new_for_path(path);
    GFileInputStream *stream = g_file_read(file, NULL, error);
    gboolean valid = FALSE;
    if (stream)
    {
        const gsize limit = 1024 * 1024;
        gchar *data = g_malloc(limit + 1);
        gsize length = 0;
        if (g_input_stream_read_all(G_INPUT_STREAM(stream), data, limit + 1, &length, NULL, error))
        {
            if (length <= limit && !memchr(data, 0, length))
            {
                g_hash_table_insert(images, g_strdup(anchor), g_bytes_new_take(data, length));
                data = NULL;
                valid = TRUE;
            }
            else
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                    "Use a text device file of at most 1 MiB.");
        }
        g_free(data);
        g_object_unref(stream);
    }
    g_object_unref(file);
    g_free(path);
    g_free(native);
    return valid;
}

/* Expand memory presets before applying the same validation as explicit rules. */
static void simulation_memory_defaults(gchar ***arguments, gint *count)
{
    static const struct {
        const gchar *name, *defaults;
        gint pins;
    } presets[] = {
        {"sram", "addr dq ce_n we_n oe_n 16 8", 5},
        {"sdram", "ck cke cs_n ras_n cas_n we_n a ba dq dqm", 10},
        {"ddr2", "ck cke cs_n ras_n cas_n we_n a ba dq dqs dm 3 2", 11},
        {"ddr3", "ck reset_n cke cs_n ras_n cas_n we_n a ba dq dqs dm 3 2", 12},
        {"ddr4", "ck reset_n cke cs_n act_n a ba bg dq dqs dm_n 3 2", 11},
        {"spi-nor", "sck cs_n mosi miso", 4},
        {"spi-nand", "sck cs_n mosi miso", 4},
        {"spi-eeprom", "sck cs_n mosi miso", 4},
        {"spi-psram", "sck cs_n mosi miso", 4},
        {"i2c-eeprom", "scl sda 0x50", 2},
    };
    gchar **args = *arguments;
    for (guint i = 0; i < G_N_ELEMENTS(presets); i++)
    {
        if (strcmp(args[0], presets[i].name))
            continue;
        gchar **defaults = g_strsplit(presets[i].defaults, " ", -1);
        gint expected = 1 + (gint)g_strv_length(defaults);
        if (*count == 1 || (*count > presets[i].pins && *count <= expected))
        {
            gchar **expanded = g_new0(gchar *, expected + 1);
            expanded[0] = g_strdup(args[0]);
            for (gint j = 1; j < expected; j++)
                expanded[j] = g_strdup(j < *count && strcmp(args[j], "-") ? args[j] : defaults[j - 1]);
            g_strfreev(args);
            *arguments = expanded;
            *count = expected;
        }
        g_strfreev(defaults);
        return;
    }
}

/* EN: Validate a DUT signal name or constant bit/range in name for generated HDL.
 * RU: Проверяет имя сигнала или постоянный бит/диапазон в name для генерации HDL. */
static gboolean simulation_capture_signal(const gchar *name)
{
    return g_regex_match_simple("^[A-Za-z_][A-Za-z0-9_$]*(\\.[A-Za-z_][A-Za-z0-9_$]*)*"
                                "(\\[-?[0-9]+(:-?[0-9]+)?\\])?$", name, 0, 0);
}

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
        g_string_append_printf(body, "initial if ($bits(dut.%s) > 64) "
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

static gboolean simulation_capture_trigger(gchar **args, gint count, GString *trigger)
{
    if (trigger->len || (count != 3 && count != 4) || !simulation_capture_signal(args[2]))
        return FALSE;
    if (count == 3 && (!strcmp(args[1], "rise") || !strcmp(args[1], "fall")))
    {
        gchar active = !strcmp(args[1], "rise") ? '1' : '0';
        gchar idle = active == '1' ? '0' : '1';
        g_string_append_printf(trigger,
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
    g_string_append_printf(trigger, "wait ((dut.%s %s %s) === 1'b1); ",
                           args[2], strchr(args[3], '?') ? "==?" : "===", args[3]);
    return TRUE;
}

static gboolean simulation_port_control(gchar **args, gint count, GString *body,
                                         GString *connections, GHashTable *ports, GString *live)
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
    g_string_append_printf(body, "// GEANY-CONTROL %s %s %s\ninitial pin%u=1'b%s;\n",
                           args[0], args[1], idle, pin, idle);
    if (button)
    {
        g_string_append_printf(body,
            "initial begin : geany_button%u integer fd, serial, previous; previous=0; forever begin "
            "#(geany_live_interval); fd=$fopen(\"geany-control-%s\",\"r\"); if(fd) begin "
            "if($fscanf(fd,\"%%d\",serial)==1 && serial!=previous) begin "
            "previous=previous+1; $fclose(fd); pin%u=1'b%c; #%" G_GUINT64_FORMAT "; pin%u=1'b%s; "
            "end else $fclose(fd); end end end\n",
            pin, args[1], pin, *idle == '0' ? '1' : '0', length, pin, idle);
        g_string_append(live, "// Button pacing\n");
    }
    else
        g_string_append_printf(live,
            "fd=$fopen(\"geany-control-%s\",\"r\"); "
            "if(fd) begin if($fscanf(fd,\"%%b\",bits)==1) pin%u=bits; $fclose(fd); end\n",
            args[1], pin);
    return TRUE;
}

static gboolean simulation_port_slider(gchar **args, gint count, GString *body,
                                        GString *connections, GHashTable *ports, GString *live)
{
    guint64 width = 8, initial = 0;
    if (count < 2 || count > 4 || g_hash_table_size(ports) >= 128 ||
        !simulation_port_name(args[1]) || g_hash_table_contains(ports, args[1]) ||
        (count >= 3 && !g_ascii_string_to_unsigned(args[2], 10, 1, 16, &width, NULL)))
        return FALSE;
    guint64 maximum = (1u << width) - 1;
    if (count == 4 && !g_ascii_string_to_unsigned(args[3], 10, 0, maximum, &initial, NULL))
        return FALSE;
    guint pin = simulation_port_connect(body, connections, ports, args[1], width, FALSE);
    g_string_append_printf(body,
        "// GEANY-SLIDER %s %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT "\n"
        "initial pin%u=%" G_GUINT64_FORMAT ";\n", args[1], maximum, initial, pin, initial);
    g_string_append_printf(live,
        "fd=$fopen(\"geany-control-%s\",\"r\"); "
        "if(fd) begin if($fscanf(fd,\"%%d\",bits)==1) pin%u=bits; $fclose(fd); end\n", args[1], pin);
    return TRUE;
}

static gboolean simulation_port_indicator(gchar **args, gint count, GString *body,
                                           GString *connections, GHashTable *ports, GString *live)
{
    gboolean lamp = !strcmp(args[0], "led");
    guint64 width = lamp ? 1 : 8;
    if (count < 2 || count > (lamp ? 3 : 4) || g_hash_table_size(ports) >= 128 ||
        !simulation_port_name(args[1]) || g_hash_table_contains(ports, args[1]))
        return FALSE;
    const gchar *format = lamp ? (count == 3 ? args[2] : "1") : (count == 4 ? args[3] : "hex");
    if (lamp ? (strcmp(format, "0") && strcmp(format, "1")) :
        ((count >= 3 && !g_ascii_string_to_unsigned(args[2], 10, 1, 64, &width, NULL)) ||
         (strcmp(format, "hex") && strcmp(format, "dec") && strcmp(format, "bin"))))
        return FALSE;
    guint pin = simulation_port_connect(body, connections, ports, args[1], width, TRUE);
    g_string_append_printf(body, "// GEANY-INDICATOR %s %s\n", args[1], format);
    g_string_append_printf(live, "$display(\"GEANY-PORT%%c%s%%c%%b\",9,9,pin%u);\n", args[1], pin);
    return TRUE;
}

typedef struct {
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
    if (ABS(limits[0] - limits[1]) >= 4096 ||
        limits[2] < MIN(limits[0], limits[1]) || limits[2] > MAX(limits[0], limits[1]) ||
        limits[3] < MIN(limits[0], limits[1]) || limits[3] > MAX(limits[0], limits[1]) ||
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
static gboolean simulation_slice_connect(GPtrArray *slices, GHashTable *ports,
                                          GString *connections, GString *body)
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
            g_string_append_printf(body, "wire [%d:%d] geany_slice_net%u;\n",
                                   slice->left, slice->right, i);
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
        gchar *part = slice->first == slice->last ?
            g_strdup_printf("geany_slice_net%u[%d]", slice->net, slice->first) :
            g_strdup_printf("geany_slice_net%u[%d:%d]", slice->net, slice->first, slice->last);
        if (slice->first == slice->last)
            g_string_append_printf(body, "// GEANY-PORT-SLICE %s %s[%d]\n",
                                   slice->alias, slice->port, slice->first);
        else
            g_string_append_printf(body, "// GEANY-PORT-SLICE %s %s[%d:%d]\n",
                                   slice->alias, slice->port, slice->first, slice->last);
        guint width = ABS(slice->first - slice->last) + 1;
        g_string_append_printf(body, "initial if ($bits(pin%u) != %u) "
            "$fatal(1, \"Port slice width mismatch: %s\");\n", pin, width, slice->port);
        if (slice->direction == 'i')
            g_string_append_printf(body, "assign %s = pin%u;\n", part, pin);
        else if (slice->direction == 'o')
            g_string_append_printf(body, "assign pin%u = %s;\n", pin, part);
        else
            g_string_append_printf(body, "tran geany_slice_bridge%u[%u:0](pin%u,%s);\n",
                                   i, width - 1, pin, part);
        g_free(part);
    }
    return TRUE;
}

/* EN: Generate a harness from recipe, resolving files in directory. Append HDL
 * dependencies to sources and staged bytes to images. Return owned HDL or set error.
 * RU: Генерирует тестовый модуль из recipe, разрешая файлы в directory. Добавляет
 * зависимости в sources и данные в images. Возвращает новый HDL или задаёт error. */
static gchar *simulation_port_harness(const gchar *recipe, const gchar *directory,
                                      GPtrArray *sources, GHashTable *images, GError **error)
{
    gchar **lines = g_strsplit(recipe, "\n", -1);
    GString *body = g_string_new("`timescale 1ns/1ns\nmodule geany_port_testbench;\n");
    GString *connections = g_string_new(NULL);
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
            valid = g_ascii_string_to_unsigned(args[1], 10, 1, 1000000000, &duration, NULL);
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
        else if (valid && (!strcmp(args[0], "led") || !strcmp(args[0], "display")))
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
        else if (valid && ((count == 14 && !strcmp(args[0], "ddr4")) ||
                           (count == 15 && !strcmp(args[0], "ddr3")) ||
                           (count == 14 && !strcmp(args[0], "ddr2"))))
        {
            guint generation = args[0][3] - '0';
            valid = simulation_port_ddr(args, body, connections, ports, images, generation);
            attached_images += valid && g_hash_table_contains(images, args[generation == 2 ? 7 : generation == 3 ? 8 : 6]);
        }
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
            valid = (!reset || simulation_capture_signal(reset)) && simulation_i2c_address(pins[3], &address);
            if (valid)
            {
                gchar *key = g_strdup_printf(":i2c_%s_%" G_GUINT64_FORMAT, pins[2], address);
                valid = simulation_memory_image(images, key, pins[4], directory, error) &&
                        simulation_port_i2c(pins, body, connections, ports, images, i2c_buses, key, reset);
                attached_images += valid;
                g_free(key);
            }
        }
        else if (valid && count == 5 && (!strcmp(args[0], "spi-nor") ||
                 !strcmp(args[0], "spi-nand") || !strcmp(args[0], "spi-eeprom") ||
                 !strcmp(args[0], "spi-psram")))
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
            valid = simulation_model_rule(args, count, models, sources, directory, body, connections, ports);
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
                valid = simulation_port_expression(args[j]);
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
                    g_string_append_printf(live, "$display(\"GEANY-PORT%%c%s%%c%%b\",9,9,pin%u);\n", args[1], id);
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
        if (!valid && (!error || !*error))
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid port recipe on line %u.", i + 1);
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
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Place memory-image before the memory rule and use its address port, SPI chip-select port or I2C SDA.");
    }
    if (valid && !module && (trigger->len || scopes || connections->len || sources->len || g_hash_table_size(models)))
    {
        valid = FALSE;
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Add a module line for the configured ports.");
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
            g_string_append_printf(body, "geany_model_%s(%s);\n", (const gchar *)it->data, model->pins->str);
        }
        g_list_free(instances);
        g_string_append_printf(body, "%s dut(%s);\n", module, connections->str);
        if (trigger->len)
            g_string_append(body, "// GEANY-CAPTURE-TRIGGER\n");
        g_string_append_printf(body,
            "initial begin %s$dumpfile(\"ports.vcd\"); $dumpvars(0,geany_port_testbench); %s end\n"
            "initial begin #%" G_GUINT64_FORMAT "; $finish; end\nendmodule\n", trigger->str,
            trigger->len ? "$display(\"Waveform capture started at %0t\",$time); $fflush();" : "", duration);
        if (live->len)
        {
            /* Put the pacing checkpoint at line 3 of the private source. */
            gchar *preamble = g_strdup_printf(
                "task geany_live_yield; $fflush(); endtask\n"
                "localparam time geany_live_interval = %" G_GUINT64_FORMAT ";\n",
                live_interval ? live_interval : 10);
            g_string_insert(body, strlen("`timescale 1ns/1ns\nmodule geany_port_testbench;\n"), preamble);
            g_free(preamble);
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
    g_hash_table_unref(spi_pins);
    g_hash_table_unref(i2c_buses);
    g_hash_table_unref(models);
    g_string_free(connections, TRUE);
    g_string_free(live, TRUE);
    g_string_free(trigger, TRUE);
    g_strfreev(lines);
    return result;
}

typedef struct {
    gchar direction[8];
    gint left, right;
    guint width, order;
} PortInfo;

enum { PORT_NAME, PORT_DIRECTION, PORT_BINDINGS, PORT_WIDTH_TEXT, PORT_WIDTH,
       PORT_LEFT, PORT_RIGHT, PORT_ORDER, PORT_OCCUPIED, PORT_DIRECTION_RANK, PORT_COLUMNS };

typedef struct {
    const gchar *source, *expanded, *preprocess_error;
    GtkTextBuffer *recipe;
    GtkListStore *store;
    GHashTable *ports;
    gchar *module;
    GtkWidget *tree, *search, *compatible, *regex_toggle, *sort_choice, *reverse;
    GtkTreeModelFilter *filter;
    GtkTreeSortable *sorted;
    GRegex *query_regex;
    gboolean regex_invalid;
    gchar direction;
    guint required_width;
    gboolean choosing;
} PortBrowser;

/* EN: Read a source snapshot from path for the modal setup. Return UTF-8 text or NULL.
 * RU: Читает снимок исходника по пути path для модального окна. Возвращает UTF-8 или NULL. */
static gchar *port_browser_source(const gchar *path)
{
    gchar *source = NULL;
    gsize size;
    if (!g_file_get_contents(path, &source, &size, NULL))
        return NULL;
    if (!g_utf8_validate(source, size, NULL))
    {
        gchar *valid = g_utf8_make_valid(source, size);
        g_free(source);
        return valid;
    }
    return source;
}

/* EN: Preprocess the run sources in compiler order using the simulation include path.
 * Return owned UTF-8 HDL, or NULL and an owned diagnostic in error.
 * RU: Раскрывает макросы в исходниках run в порядке компиляции и с путём include
 * симуляции. Возвращает новый текст UTF-8 либо NULL и сообщение в error. */
static gchar *port_browser_preprocess(const WaveSimulation *run, gchar **error)
{
    GError *failure = NULL;
    gchar *directory = g_dir_make_tmp("geany-verilog-ports-XXXXXX", &failure);
    gchar *expanded = NULL;
    if (directory)
    {
        GSubprocessLauncher *launcher = g_subprocess_launcher_new(
            G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
        g_subprocess_launcher_set_cwd(launcher, directory);
        gchar *bin = g_path_get_dirname(run->compiler);
        const gchar *existing = g_getenv("PATH");
        gchar *path = g_strconcat(bin, G_SEARCHPATH_SEPARATOR_S, existing ? existing : "", NULL);
        g_subprocess_launcher_setenv(launcher, "PATH", path, TRUE);
        g_free(path);
        g_free(bin);

        const gchar *options[] = {run->compiler, "-g2012", "-I", run->include_dir, "-E", "-o", "-"};
        GPtrArray *command = g_ptr_array_new();
        for (guint i = 0; i < G_N_ELEMENTS(options); i++)
            g_ptr_array_add(command, (gpointer)options[i]);
        for (guint i = 0; run->sources[i]; i++)
            g_ptr_array_add(command, run->sources[i]);
        g_ptr_array_add(command, NULL);
        GSubprocess *process = g_subprocess_launcher_spawnv(
            launcher, (const gchar *const *)command->pdata, &failure);
        g_ptr_array_unref(command);
        g_object_unref(launcher);
        if (process)
        {
            GBytes *output = NULL, *diagnostics = NULL;
            if (g_subprocess_communicate(process, NULL, NULL, &output, &diagnostics, &failure))
            {
                gsize length = 0;
                const gchar *bytes;
                if (g_subprocess_wait_check(process, NULL, &failure))
                {
                    bytes = g_bytes_get_data(output, &length);
                    expanded = g_utf8_make_valid(length ? bytes : "", length);
                }
                else
                {
                    bytes = g_bytes_get_data(diagnostics, &length);
                    if (length)
                        *error = g_utf8_make_valid(bytes, MIN(length, 2048));
                }
            }
            g_clear_pointer(&output, g_bytes_unref);
            g_clear_pointer(&diagnostics, g_bytes_unref);
            g_object_unref(process);
        }
        g_rmdir(directory);
        g_free(directory);
    }
    if (!expanded && !*error)
        *error = g_strdup(failure ? failure->message : "No preprocessor output.");
    g_clear_error(&failure);
    return expanded;
}

/* EN: Return the packed atom width for a port declaration prefix. Return zero
 * for an unknown type; any explicit packed range is handled by the caller.
 * RU: Возвращает ширину типа порта из prefix. Для неизвестного типа возвращает
 * ноль. Явный упакованный диапазон обрабатывается вызывающей стороной. */
static guint port_browser_type_width(const gchar *prefix)
{
    gchar **words = g_strsplit_set(prefix, " \t\r\n", -1);
    guint width = 1;
    for (guint i = 0; words[i]; i++)
    {
        const gchar *word = words[i];
        if (!*word || !strcmp(word, "input") || !strcmp(word, "output") ||
            !strcmp(word, "inout") || !strcmp(word, "ref") ||
            !strcmp(word, "wire") || !strcmp(word, "reg") || !strcmp(word, "logic") ||
            !strcmp(word, "bit") || !strcmp(word, "tri") || !strcmp(word, "var") ||
            !strcmp(word, "signed") || !strcmp(word, "unsigned"))
            continue;
        if (!strcmp(word, "byte"))
            width = 8;
        else if (!strcmp(word, "shortint"))
            width = 16;
        else if (!strcmp(word, "integer") || !strcmp(word, "int"))
            width = 32;
        else if (!strcmp(word, "longint") || !strcmp(word, "time"))
            width = 64;
        else
        {
            width = 0;
            break;
        }
    }
    g_strfreev(words);
    return width;
}

/* EN: Extract port names and inherited directions from list into ports. With existing_only,
 * update only names declared in the module header.
 * RU: Извлекает имена и направления портов из list в ports. При existing_only обновляет
 * только имена, объявленные в заголовке модуля. */
static void port_browser_declarations(const gchar *list, GHashTable *ports, gboolean existing_only)
{
    GRegex *direction = g_regex_new("^\\s*(input|output|inout|ref)\\b", 0, 0, NULL);
    GRegex *name = g_regex_new("([A-Za-z_][A-Za-z0-9_$]*)\\s*(?:\\[[^]]*\\]\\s*)*$", 0, 0, NULL);
    PortInfo current = {.direction = "—"};
    GRegex *range = g_regex_new("\\[\\s*(-?[0-9]+)\\s*:\\s*(-?[0-9]+)\\s*\\]", 0, 0, NULL);
    const gchar *start = list;
    gint depth = 0;
    for (const gchar *p = list; ; p++)
    {
        if (!*p || (*p == ',' && depth == 0))
        {
            gchar *part = g_strndup(start, p - start);
            GMatchInfo *match = NULL;
            gboolean explicit_direction = g_regex_match(direction, part, 0, &match);
            if (explicit_direction)
            {
                gchar *value = g_match_info_fetch(match, 1);
                g_strlcpy(current.direction, value, sizeof current.direction);
                g_free(value);
                current.width = 1;
                current.left = current.right = 0;
            }
            g_match_info_free(match);
            gchar *assignment = strchr(part, '=');
            if (assignment)
                *assignment = 0;
            if (g_regex_match(name, part, 0, &match))
            {
                gint start_name;
                g_match_info_fetch_pos(match, 1, &start_name, NULL);
                gchar *port = g_match_info_fetch(match, 1);
                gchar *prefix = g_strndup(part, start_name);
                if (explicit_direction && !strchr(prefix, '['))
                {
                    current.width = port_browser_type_width(prefix);
                    current.left = current.width ? (gint)current.width - 1 : 0;
                }
                if (strchr(prefix, '['))
                {
                    GMatchInfo *packed;
                    current.width = 0;
                    if (g_regex_match(range, prefix, 0, &packed) &&
                        !strchr(strchr(prefix, '[') + 1, '['))
                    {
                        gchar *left = g_match_info_fetch(packed, 1);
                        gchar *right = g_match_info_fetch(packed, 2);
                        gint64 first, last;
                        if (g_ascii_string_to_signed(left, 10, -1000000, 1000000, &first, NULL) &&
                            g_ascii_string_to_signed(right, 10, -1000000, 1000000, &last, NULL) &&
                            ABS(first - last) < 4096)
                        {
                            current.left = first;
                            current.right = last;
                            current.width = ABS(first - last) + 1;
                        }
                        g_free(left);
                        g_free(right);
                    }
                    g_match_info_free(packed);
                }
                PortInfo *previous = g_hash_table_lookup(ports, port);
                if (!existing_only || (previous && !strcmp(previous->direction, "—")))
                {
                    PortInfo *info = g_new(PortInfo, 1);
                    *info = current;
                    info->order = previous ? previous->order : g_hash_table_size(ports);
                    if (strchr(part + start_name, '['))
                        info->width = 0;
                    g_hash_table_replace(ports, port, info);
                }
                else
                    g_free(port);
                g_free(prefix);
            }
            g_match_info_free(match);
            g_free(part);
            if (!*p)
                break;
            start = p + 1;
        }
        else if (strchr("([{", *p))
            depth++;
        else if (strchr(")]}", *p))
            depth--;
    }
    g_regex_unref(range);
    g_regex_unref(direction);
    g_regex_unref(name);
}

/* EN: Find the named module in source and return its port-to-direction map. With no
 * module name, inspect the first module. The caller owns the map.
 * RU: Находит модуль module в source и возвращает таблицу направлений портов.
 * Без имени берёт первый модуль. Таблица принадлежит вызывающей стороне. */
static GHashTable *port_browser_ports(const gchar *source, const gchar *module)
{
    GHashTable *ports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (!source)
        return ports;
    GRegex *comments = g_regex_new("//[^\\n]*|/\\*[\\s\\S]*?\\*/|\"(?:\\\\.|[^\"\\\\])*\"", 0, 0, NULL);
    gchar *clean = g_regex_replace_literal(comments, source, -1, 0, " ", 0, NULL);
    g_regex_unref(comments);
    GRegex *modules = g_regex_new("\\bmodule\\s+(?:(?:automatic|static)\\s+)?([A-Za-z_][A-Za-z0-9_$]*)", 0, 0, NULL);
    GMatchInfo *match;
    g_regex_match(modules, clean, 0, &match);
    while (g_match_info_matches(match))
    {
        gchar *name = g_match_info_fetch(match, 1);
        gboolean selected = !module || !strcmp(name, module);
        g_free(name);
        if (selected)
        {
            gint end;
            g_match_info_fetch_pos(match, 0, NULL, &end);
            const gchar *cursor = clean + end;
            while (g_ascii_isspace(*cursor))
                cursor++;
            if (*cursor == '#')
            {
                cursor++;
                while (g_ascii_isspace(*cursor))
                    cursor++;
                if (*cursor == '(')
                {
                    guint depth = 1;
                    for (cursor++; *cursor && depth; cursor++)
                    {
                        if (*cursor == '(')
                            depth++;
                        else if (*cursor == ')')
                            depth--;
                    }
                }
            }
            while (g_ascii_isspace(*cursor))
                cursor++;
            if (*cursor == '(')
            {
                const gchar *start = ++cursor;
                guint depth = 1;
                for (; *cursor && depth; cursor++)
                {
                    if (*cursor == '(')
                        depth++;
                    else if (*cursor == ')')
                        depth--;
                }
                if (!depth)
                {
                    gchar *header = g_strndup(start, cursor - start - 1);
                    port_browser_declarations(header, ports, FALSE);
                    g_free(header);
                    GRegex *body = g_regex_new("\\b(input|output|inout|ref)\\b[^;]*;", 0, 0, NULL);
                    GMatchInfo *declaration;
                    const gchar *limit = strstr(cursor, "endmodule");
                    gchar *contents = limit ? g_strndup(cursor, limit - cursor) : g_strdup(cursor);
                    g_regex_match(body, contents, 0, &declaration);
                    while (g_match_info_matches(declaration))
                    {
                        gchar *line = g_match_info_fetch(declaration, 0);
                        line[strlen(line) - 1] = 0;
                        port_browser_declarations(line, ports, TRUE);
                        g_free(line);
                        g_match_info_next(declaration, NULL);
                    }
                    g_match_info_free(declaration);
                    g_regex_unref(body);
                    g_free(contents);
                }
            }
            break;
        }
        g_match_info_next(match, NULL);
    }
    g_match_info_free(match);
    g_regex_unref(modules);
    g_free(clean);
    return ports;
}

/* EN: Return the argument indexes that designate DUT ports for rule args, with argc tokens.
 * RU: Возвращает индексы аргументов, задающих порты схемы для правила args из argc токенов. */
static guint port_browser_pin_mask(gchar **args, gint argc)
{
    const gchar *kind = args[0];
    if (!strcmp(kind, "spi-lua") && argc == 2)
        return 0;
    if (!strcmp(kind, "i2c-lua") && argc == 2)
        return 0;
    if (g_str_has_prefix(kind, "spi-"))
        return 0x1e | (!strcmp(kind, "spi-lua") && argc == 7 ? 1u << 6 : 0);
    if (g_str_has_prefix(kind, "i2c-"))
        return 6 | (!strcmp(kind, "i2c-lua") && argc == 6 ? 1u << 5 : 0);
    if (!strcmp(kind, "model-link") || !strcmp(kind, "model-share") ||
        !strcmp(kind, "capture-trigger"))
        return 1u << 2;
    if (!strcmp(kind, "scope-xyz"))
        return 14;
    if (!strcmp(kind, "scope-xy") || !strcmp(kind, "uart"))
        return 6;
    const gchar *single[] = {"clock", "clock-hz", "pulse", "button", "switch", "slider", "led",
                             "display", "scope", "live-input", "monitor", "output", "value", "bus"};
    for (guint i = 0; i < G_N_ELEMENTS(single); i++)
        if (!strcmp(kind, single[i]))
            return 2;
    const gchar *memories[] = {"sram", "sdram", "ddr2", "ddr3", "ddr4"};
    const guint pins[] = {5, 10, 11, 12, 11};
    for (guint i = 0; i < G_N_ELEMENTS(memories); i++)
        if (!strcmp(kind, memories[i]))
            return (1u << (pins[i] + 1)) - 2;
    return 0;
}

/* EN: Give direction a stable sort rank for input, output, inout, ref, and unknown.
 * RU: Возвращает порядок сортировки direction для input, output, inout, ref и неизвестного. */
static guint port_browser_direction_rank(const gchar *direction)
{
    const gchar *roles[] = {"input", "output", "inout", "ref"};
    for (guint i = 0; i < G_N_ELEMENTS(roles); i++)
        if (!strcmp(direction, roles[i]))
            return i;
    return G_N_ELEMENTS(roles);
}

/* EN: Refresh browser rows from the current recipe buffer. The buffer owns the edited text.
 * RU: Обновляет строки browser по текущим правилам из buffer, которому принадлежит текст. */
static void port_browser_refresh(GtkTextBuffer *buffer, gpointer data)
{
    PortBrowser *browser = data;
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(buffer, &first, &last);
    gchar *recipe = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
    gchar **lines = g_strsplit(recipe, "\n", -1);
    gchar *module = NULL;
    GHashTable *aliases = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (guint line = 0; lines[line]; line++)
    {
        gchar **args = NULL;
        gint count;
        if (g_shell_parse_argv(lines[line], &count, &args, NULL))
        {
            if (count == 8 && !strcmp(args[0], "port-slice"))
                g_hash_table_replace(aliases, g_strdup(args[1]), g_strdup(args[2]));
            g_strfreev(args);
        }
    }
    GHashTable *bindings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (guint line = 0; lines[line]; line++)
    {
        gchar **args = NULL;
        gint argc;
        if (!g_shell_parse_argv(lines[line], &argc, &args, NULL))
            continue;
        if (argc == 2 && !strcmp(args[0], "module") && !module)
            module = g_strdup(args[1]);
        simulation_memory_defaults(&args, &argc);
        guint mask = port_browser_pin_mask(args, argc);
        gchar *fallback[] = {args[0], "sck", "cs_n", "mosi", "miso"};
        gchar *i2c[] = {args[0], "scl", "sda"};
        gchar **pins = args;
        gint count = argc;
        if (argc == 2 && !strcmp(args[0], "spi-lua"))
        {
            pins = fallback;
            count = G_N_ELEMENTS(fallback);
            mask = 0x1e;
        }
        else if (argc == 2 && !strcmp(args[0], "i2c-lua"))
        {
            pins = i2c;
            count = G_N_ELEMENTS(i2c);
            mask = 6;
        }
        for (gint i = 1; i < MIN(count, 16); i++)
        {
            if (!(mask & (1u << i)))
                continue;
            const gchar *actual = g_hash_table_lookup(aliases, pins[i]);
            gchar *port = actual ? g_strdup(actual) : g_strndup(pins[i], strcspn(pins[i], "["));
            const gchar *previous = g_hash_table_lookup(bindings, port);
            const gchar *device = g_str_has_prefix(args[0], "model-") ? args[1] : args[0];
            gchar *entry = g_strdup_printf("%s (line %u)", device, line + 1);
            gchar *combined = previous ? g_strconcat(previous, ", ", entry, NULL) : g_strdup(entry);
            g_hash_table_replace(bindings, port, combined);
            g_free(entry);
        }
        g_strfreev(args);
    }
    if (!browser->ports || g_strcmp0(browser->module, module))
    {
        g_clear_pointer(&browser->ports, g_hash_table_unref);
        g_free(browser->module);
        browser->module = g_strdup(module);
        browser->ports = port_browser_ports(browser->expanded, module);
    }
    GList *names = g_list_sort(g_hash_table_get_keys(browser->ports), (GCompareFunc)g_strcmp0);
    gtk_list_store_clear(browser->store);
    for (GList *item = names; item; item = item->next)
    {
        const gchar *name = item->data;
        const gchar *attached = g_hash_table_lookup(bindings, name);
        PortInfo *info = g_hash_table_lookup(browser->ports, name);
        gchar *width = info->width ? g_strdup_printf("%u", info->width) : g_strdup("?");
        GtkTreeIter row;
        gtk_list_store_insert_with_values(browser->store, &row, -1,
            PORT_NAME, name, PORT_DIRECTION, info->direction,
            PORT_BINDINGS, attached ? attached : "Unassigned", PORT_WIDTH_TEXT, width,
            PORT_WIDTH, info->width, PORT_LEFT, info->left, PORT_RIGHT, info->right,
            PORT_ORDER, info->order, PORT_OCCUPIED, (guint)(attached != NULL),
            PORT_DIRECTION_RANK, port_browser_direction_rank(info->direction), -1);
        g_free(width);
    }
    g_hash_table_unref(aliases);
    g_list_free(names);
    g_hash_table_unref(bindings);
    g_strfreev(lines);
    g_free(module);
    g_free(recipe);
}

/* EN: Copy the selected port from tree (callback data) when button is clicked.
 * RU: По нажатию button копирует выбранный порт из tree, переданного через data. */
static void port_browser_copy(GtkButton *button, gpointer data)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
    GtkTreeModel *model;
    GtkTreeIter row;
    if (gtk_tree_selection_get_selected(selection, &model, &row))
    {
        gchar *port;
        gtk_tree_model_get(model, &row, 0, &port, -1);
        GtkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(button), GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(clipboard, port, -1);
        g_free(port);
    }
}

/* EN: Return whether direction suits the DUT role required by field.
 * RU: Проверяет, подходит ли направление direction для роли поля field. */
static gboolean port_browser_matches(const gchar *direction, gchar field)
{
    return (field == 'i' && !strcmp(direction, "input")) ||
           (field == 'o' && !strcmp(direction, "output")) ||
           (field == 'b' && !strcmp(direction, "inout")) ||
           (field == 'c' && (!strcmp(direction, "output") || !strcmp(direction, "inout"))) ||
           (field == 'a' && strcmp(direction, "—"));
}

/* EN: Return whether row in model matches the browser filters stored in data.
 * RU: Проверяет, соответствует ли row из model фильтрам списка из data. */
static gboolean port_browser_visible(GtkTreeModel *model, GtkTreeIter *row, gpointer data)
{
    PortBrowser *browser = data;
    gchar *name, *direction;
    guint width;
    gtk_tree_model_get(model, row, PORT_NAME, &name, PORT_DIRECTION, &direction,
                       PORT_WIDTH, &width, -1);
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(browser->search));
    gboolean visible;
    if (browser->regex_invalid)
        visible = FALSE;
    else if (browser->query_regex)
        visible = g_regex_match(browser->query_regex, name, 0, NULL);
    else
    {
        gchar *folded_name = g_ascii_strdown(name, -1);
        gchar *folded_query = g_ascii_strdown(query, -1);
        visible = strstr(folded_name, folded_query) != NULL;
        g_free(folded_query);
        g_free(folded_name);
    }
    if (visible && browser->direction && browser->compatible &&
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(browser->compatible)))
    {
        visible = port_browser_matches(direction, browser->direction) && width &&
                  (browser->direction == 'a' ||
                   (browser->required_width && width >= browser->required_width));
    }
    g_free(direction);
    g_free(name);
    return visible;
}

/* EN: Apply filter changes from widget to the port browser in data.
 * RU: Применяет изменения фильтра из widget к списку портов из data. */
static void port_browser_refilter(GtkWidget *widget, gpointer data)
{
    (void)widget;
    PortBrowser *browser = data;
    gtk_tree_model_filter_refilter(browser->filter);
    gtk_widget_queue_draw(browser->tree);
}

/* EN: Compile the current name pattern and refilter browser from data.
 * widget triggered the change. Invalid expressions are shown beside the query.
 * RU: Компилирует шаблон имени и обновляет список из data. widget вызвал
 * изменение. Ошибка выражения показывается рядом с запросом. */
static void port_browser_search_changed(GtkWidget *widget, gpointer data)
{
    PortBrowser *browser = data;
    g_clear_pointer(&browser->query_regex, g_regex_unref);
    GError *error = NULL;
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(browser->search));
    if (*query && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(browser->regex_toggle)))
        browser->query_regex = g_regex_new(query, G_REGEX_CASELESS, 0, &error);
    browser->regex_invalid = error != NULL;
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(browser->search), GTK_ENTRY_ICON_PRIMARY,
                                     error ? "dialog-error-symbolic" : "edit-find-symbolic");
    gtk_entry_set_icon_tooltip_text(GTK_ENTRY(browser->search), GTK_ENTRY_ICON_PRIMARY,
                                   error ? error->message : NULL);
    g_clear_error(&error);
    port_browser_refilter(widget, browser);
}

/* EN: Compare rows first by the numeric column in data, then by declaration order.
 * RU: Сравнивает строки сначала по числовому столбцу data, затем по порядку объявления. */
static gint port_browser_compare_group(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
                                       gpointer data)
{
    gint column = GPOINTER_TO_INT(data);
    guint first, second, order_a, order_b;
    gtk_tree_model_get(model, a, column, &first, PORT_ORDER, &order_a, -1);
    gtk_tree_model_get(model, b, column, &second, PORT_ORDER, &order_b, -1);
    if (first != second)
        return first < second ? -1 : 1;
    return order_a == order_b ? 0 : order_a < order_b ? -1 : 1;
}

/* EN: Apply the sort selection and reverse flag to browser in data. widget is unused.
 * RU: Применяет порядок сортировки и направление к списку из data. widget не используется. */
static void port_browser_sort_changed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    PortBrowser *browser = data;
    const gint columns[] = {PORT_NAME, PORT_DIRECTION_RANK, PORT_OCCUPIED, PORT_ORDER};
    gint selected = gtk_combo_box_get_active(GTK_COMBO_BOX(browser->sort_choice));
    if (selected < 0 || (guint)selected >= G_N_ELEMENTS(columns))
        return;
    gboolean reverse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(browser->reverse));
    gtk_tree_sortable_set_sort_column_id(browser->sorted, columns[selected],
                                         reverse ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}

/* EN: Color cell for row in model when its direction matches the focused browser
 * field in data. column is unused.
 * RU: Подсвечивает cell строки row из model при совпадении направления с полем
 * в data. column не используется. */
static void port_browser_color(GtkTreeViewColumn *column, GtkCellRenderer *cell,
                               GtkTreeModel *model, GtkTreeIter *row, gpointer data)
{
    (void)column;
    PortBrowser *browser = data;
    gchar *direction;
    gtk_tree_model_get(model, row, PORT_DIRECTION, &direction, -1);
    gboolean matches = browser->direction && port_browser_matches(direction, browser->direction);
    if (matches)
        g_object_set(cell, "cell-background", "#cde8ff", "foreground", "#102a43", NULL);
    g_object_set(cell, "cell-background-set", matches, "foreground-set", matches, NULL);
    g_free(direction);
}

/* EN: Create the Ports and Source notebook for browser. The caller keeps browser alive
 * until the dialog and its recipe change handler are destroyed.
 * RU: Создаёт вкладки портов и исходника для browser. Вызывающий хранит browser до
 * уничтожения диалога и отключения обработчика изменений текста. */
static GtkWidget *port_browser_widget(PortBrowser *browser)
{
    GtkWidget *notebook = gtk_notebook_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    if (browser->preprocess_error)
    {
        gchar *message = g_strdup_printf("Port preprocessing failed.\n%s", browser->preprocess_error);
        GtkWidget *diagnostic = gtk_label_new(message);
        gtk_label_set_line_wrap(GTK_LABEL(diagnostic), TRUE);
        gtk_label_set_selectable(GTK_LABEL(diagnostic), TRUE);
        gtk_box_pack_start(GTK_BOX(box), diagnostic, FALSE, FALSE, 0);
        g_free(message);
    }
    browser->store = gtk_list_store_new(PORT_COLUMNS, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INT, G_TYPE_INT,
        G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
    GtkWidget *filters = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    browser->search = gtk_search_entry_new();
    gtk_widget_set_name(browser->search, "insight-port-search");
    gtk_entry_set_placeholder_text(GTK_ENTRY(browser->search), "Find port by name");
    gtk_box_pack_start(GTK_BOX(filters), browser->search, TRUE, TRUE, 0);
    browser->regex_toggle = gtk_check_button_new_with_label("Regex");
    gtk_box_pack_start(GTK_BOX(filters), browser->regex_toggle, FALSE, FALSE, 0);
    if (browser->choosing)
    {
        browser->compatible = gtk_check_button_new_with_label("Compatible only");
        gtk_widget_set_sensitive(browser->compatible, FALSE);
        gtk_box_pack_start(GTK_BOX(filters), browser->compatible, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), filters, FALSE, FALSE, 0);
    GtkWidget *sorting = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(sorting), gtk_label_new("Sort by"), FALSE, FALSE, 0);
    browser->sort_choice = gtk_combo_box_text_new();
    gtk_widget_set_name(browser->sort_choice, "insight-port-sort");
    const gchar *choices[] = {"Name", "Direction", "Occupancy", "Declaration order"};
    for (guint i = 0; i < G_N_ELEMENTS(choices); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(browser->sort_choice), choices[i]);
    gtk_box_pack_start(GTK_BOX(sorting), browser->sort_choice, TRUE, TRUE, 0);
    browser->reverse = gtk_check_button_new_with_label("Reverse");
    gtk_box_pack_start(GTK_BOX(sorting), browser->reverse, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), sorting, FALSE, FALSE, 0);

    GtkTreeModel *filtered = gtk_tree_model_filter_new(GTK_TREE_MODEL(browser->store), NULL);
    browser->filter = GTK_TREE_MODEL_FILTER(filtered);
    gtk_tree_model_filter_set_visible_func(browser->filter, port_browser_visible, browser, NULL);
    GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(filtered);
    browser->sorted = GTK_TREE_SORTABLE(sorted);
    gtk_tree_sortable_set_sort_func(browser->sorted, PORT_DIRECTION_RANK,
                                   port_browser_compare_group, GINT_TO_POINTER(PORT_DIRECTION_RANK), NULL);
    gtk_tree_sortable_set_sort_func(browser->sorted, PORT_OCCUPIED,
                                   port_browser_compare_group, GINT_TO_POINTER(PORT_OCCUPIED), NULL);
    GtkWidget *tree = gtk_tree_view_new_with_model(sorted);
    browser->tree = tree;
    gtk_widget_set_name(tree, "insight-test-device-ports");
    g_object_unref(sorted);
    g_object_unref(filtered);
    g_object_unref(browser->store);
    g_signal_connect(browser->search, "changed", G_CALLBACK(port_browser_search_changed), browser);
    g_signal_connect(browser->regex_toggle, "toggled", G_CALLBACK(port_browser_search_changed), browser);
    g_signal_connect(browser->sort_choice, "changed", G_CALLBACK(port_browser_sort_changed), browser);
    g_signal_connect(browser->reverse, "toggled", G_CALLBACK(port_browser_sort_changed), browser);
    gtk_combo_box_set_active(GTK_COMBO_BOX(browser->sort_choice), 0);
    if (browser->compatible)
        g_signal_connect(browser->compatible, "toggled", G_CALLBACK(port_browser_refilter), browser);
    const gchar *titles[] = {"Port", "Direction", "Bits", "Devices / rules"};
    const gint columns[] = {PORT_NAME, PORT_DIRECTION, PORT_WIDTH_TEXT, PORT_BINDINGS};
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
    {
        GtkCellRenderer *cell = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            titles[i], cell, "text", columns[i], NULL);
        gtk_tree_view_column_set_cell_data_func(column, cell, port_browser_color, browser, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    }
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    GtkWidget *copy = gtk_button_new_with_label("Copy Port Name");
    g_signal_connect(copy, "clicked", G_CALLBACK(port_browser_copy), tree);
    gtk_box_pack_start(GTK_BOX(box), copy, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, gtk_label_new("Ports"));
    GtkWidget *source = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(source), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(source), TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(source)),
                             browser->source ? browser->source : "Source unavailable.", -1);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), source);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll, gtk_label_new("Source"));
    port_browser_refresh(browser->recipe, browser);
    return notebook;
}

typedef struct {
    const gchar *rule, *fields, *pins;
    gint image;
    gboolean script;
} DevicePreset;

static const DevicePreset device_presets[] = {
    {"module", "Module name=", "", -1, FALSE},
    {"clock", "Port=clk|Start (ns)=0|High (ns)=5|Low (ns)=5", "i1", -1, FALSE},
    {"pulse", "Port=reset|Start (ns)=10|Length (ns)=100|Active level=1", "i1", -1, FALSE},
    {"button", "Port=button|Idle level=0|Length (ns)=100", "i1", -1, FALSE},
    {"switch", "Port=enable|Initial level=0", "i1", -1, FALSE},
    {"slider", "Port=level|Width=8|Initial value=0", "i@1", -1, FALSE},
    {"led", "Port=ready|Active level=1", "o1", -1, FALSE},
    {"display", "Port=status|Width=8|Format (hex, dec, bin)=hex", "o@1", -1, FALSE},
    {"scope", "Signal=", "a64", -1, FALSE},
    {"scope-xy", "X signal=|Y signal=", "a64|a64", -1, FALSE},
    {"scope-xyz", "X signal=|Y signal=|Z signal=", "a64|a64|a64", -1, FALSE},
    {"uart", "RX=rx|TX=tx|Baud=115200|Console=console", "i1|o1", -1, FALSE},
    {"sram",
     "Address=addr|Data=dq|CE#=ce_n|"
     "WE#=we_n|OE#=oe_n|Address bits=16|Data bits=8",
     "o@5|b@6|o1|o1|o1", 0, FALSE},
    {"sdram",
     "CK=ck|CKE=cke|CS#=cs_n|RAS#=ras_n|"
     "CAS#=cas_n|WE#=we_n|A=a|BA=ba|DQ=dq|DQM=dqm",
     "o1|o1|o1|o1|o1|o1|o13|o2|b16|o2", 6, FALSE},
    {"ddr2",
     "CK=ck|CKE=cke|CS#=cs_n|RAS#=ras_n|CAS#=cas_n|WE#=we_n|"
     "A=a|BA=ba|DQ=dq|DQS=dqs|DM=dm|Read latency=3|Write latency=2",
     "o1|o1|o1|o1|o1|o1|o14|o3|b8|b1|o1", 6, FALSE},
    {"ddr3",
     "CK=ck|RESET#=reset_n|CKE=cke|CS#=cs_n|RAS#=ras_n|CAS#=cas_n|"
     "WE#=we_n|A=a|BA=ba|DQ=dq|DQS=dqs|DM=dm|Read latency=3|Write latency=2",
     "o1|o1|o1|o1|o1|o1|o1|o16|o3|b8|b1|o1", 7, FALSE},
    {"ddr4",
     "CK=ck|RESET#=reset_n|CKE=cke|CS#=cs_n|ACT#=act_n|A=a|BA=ba|"
     "BG=bg|DQ=dq|DQS=dqs|DM#=dm_n|Read latency=3|Write latency=2",
     "o1|o1|o1|o1|o1|o17|o2|o2|b8|b1|o1", 5, FALSE},
    {"spi-nor", "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso", "o1|o1|o1|i1", 1, FALSE},
    {"spi-nand", "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso", "o1|o1|o1|i1", 1, FALSE},
    {"spi-eeprom", "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso", "o1|o1|o1|i1", 1, FALSE},
    {"spi-psram", "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso", "o1|o1|o1|i1", 1, FALSE},
    {"i2c-eeprom", "SCL=scl|SDA=sda|Address=0x50", "c1|b1", 1, FALSE},
    {"spi-lua", "SCK=sck|CS#=cs_n|MOSI=mosi|MISO=miso", "o1|o1|o1|i1", -1, TRUE},
    {"i2c-lua", "SCL=scl|SDA=sda|Address=0x50", "c1|b1", -1, TRUE},
};

typedef struct {
    GtkWidget *grid, *file, *reset, *status, *active;
    GPtrArray *entries;
    PortBrowser *browser;
    const gchar *directory;
} DeviceForm;

/* EN: Resolve the required width from spec, using the indexed entry in form for @.
 * RU: Определяет требуемую ширину по spec, используя поле form по индексу после @. */
static guint device_width(const DeviceForm *form, const gchar *spec)
{
    if (!spec || !*spec)
        return 0;
    const gchar *value = spec + 1;
    if (*value == '@')
    {
        guint index = (guint)g_ascii_strtoull(value + 1, NULL, 10);
        if (index >= form->entries->len)
            return 0;
        value = gtk_entry_get_text(GTK_ENTRY(form->entries->pdata[index]));
    }
    guint64 width;
    return g_ascii_string_to_unsigned(value, 10, 1, 4096, &width, NULL) ? (guint)width : 0;
}

/* EN: Update form's active direction and width, then refresh the port filters.
 * RU: Обновляет направление и ширину выбранного поля form и применяет фильтры. */
static void device_port_filter(DeviceForm *form)
{
    PortBrowser *browser = form->browser;
    const gchar *spec = form->active ? g_object_get_data(G_OBJECT(form->active), "port-spec") : NULL;
    gchar direction = spec ? *spec : 0;
    guint width = device_width(form, spec);
    if (browser->direction == direction && browser->required_width == width)
        return;
    browser->direction = direction;
    browser->required_width = width;
    gtk_widget_set_sensitive(browser->compatible, direction != 0);
    port_browser_refilter(NULL, browser);
}

/* EN: Refresh width captions after editable changes; data points to the device form.
 * RU: Обновляет подписи ширины при изменении editable. data указывает на форму. */
static void device_width_changed(GtkEditable *editable, gpointer data)
{
    (void)editable;
    DeviceForm *form = data;
    for (guint i = 0; i < form->entries->len; i++)
    {
        GtkWidget *entry = form->entries->pdata[i];
        const gchar *spec = g_object_get_data(G_OBJECT(entry), "port-spec");
        GtkLabel *label = g_object_get_data(G_OBJECT(entry), "port-label");
        const gchar *title = g_object_get_data(G_OBJECT(entry), "port-title");
        if (!spec || !label)
            continue;
        guint width = device_width(form, spec);
        gchar *caption = *spec == 'a' ? g_strdup_printf("%s (1–64 bits)", title) :
                         width ? g_strdup_printf("%s (%u %s)", title, width, width == 1 ? "bit" : "bits") :
                                 g_strdup_printf("%s (width unspecified)", title);
        gtk_label_set_text(label, caption);
        g_free(caption);
    }
    device_port_filter(form);
}

/* EN: Remember the focused port entry and highlight matching directions. event is unused.
 * data is the form. Return FALSE to preserve normal GTK focus behavior.
 * RU: Запоминает выбранное поле entry и подсвечивает подходящие направления.
 * event не используется, data — форма. Возвращает FALSE для обычного поведения GTK. */
static gboolean device_port_focus(GtkWidget *entry, GdkEventFocus *event, gpointer data)
{
    (void)event;
    DeviceForm *form = data;
    const gchar *spec = g_object_get_data(G_OBJECT(entry), "port-spec");
    form->active = spec ? entry : NULL;
    device_port_filter(form);
    return FALSE;
}

typedef struct {
    gchar *name;
    guint available, width;
    gint left, right;
} DevicePortChoice;

/* EN: Read row from model into choice for the active field of form. Return FALSE
 * and show a diagnostic for an incompatible port. The caller frees choice->name.
 * RU: Читает row из model в choice для активного поля form. При несовместимости
 * показывает причину и возвращает FALSE. Вызывающий освобождает choice->name. */
static gboolean device_port_choice(DeviceForm *form, GtkTreeModel *model,
                                   GtkTreeIter *row, DevicePortChoice *choice)
{
    if (!form->active)
    {
        gtk_label_set_text(GTK_LABEL(form->status), "Select a port field first.");
        return FALSE;
    }
    gchar *direction;
    gtk_tree_model_get(model, row, PORT_NAME, &choice->name, PORT_DIRECTION, &direction,
                       PORT_WIDTH, &choice->available, PORT_LEFT, &choice->left,
                       PORT_RIGHT, &choice->right, -1);
    const gchar *spec = g_object_get_data(G_OBJECT(form->active), "port-spec");
    choice->width = device_width(form, spec);
    if (*spec == 'a' && choice->available)
        choice->width = MIN(choice->width, choice->available);
    const gchar *error = NULL;
    if (!port_browser_matches(direction, *spec))
        error = "The port direction does not match this field.";
    else if (!choice->available || !choice->width || choice->available < choice->width)
        error = "The port width is unknown or too small for this field.";
    gtk_label_set_text(GTK_LABEL(form->status), error ? error : "");
    g_free(direction);
    return error == NULL;
}

/* EN: Format an owned Verilog expression from choice and offset from its least
 * significant end. The caller validates offset against the available width.
 * RU: Формирует новое выражение Verilog из choice и смещения offset от младшего
 * края. Вызывающий проверяет offset по доступной ширине. */
static gchar *device_port_slice(const DevicePortChoice *choice, guint offset)
{
    if (choice->available == choice->width)
        return g_strdup(choice->name);
    gint step = choice->left >= choice->right ? 1 : -1;
    gint last = choice->right + step * (gint)offset;
    gint first = last + step * ((gint)choice->width - 1);
    if (choice->width == 1)
        return g_strdup_printf("%s[%d]", choice->name, last);
    return g_strdup_printf("%s[%d:%d]", choice->name, first, last);
}

/* EN: Replace the focused field of form with choice at offset, preserving HDL order.
 * RU: Заменяет активное поле form выражением choice со смещением offset в порядке HDL. */
static void device_port_apply(DeviceForm *form, const DevicePortChoice *choice, guint offset)
{
    gchar *value = device_port_slice(choice, offset);
    gtk_entry_set_text(GTK_ENTRY(form->active), value);
    g_free(value);
}

/* EN: Use the low-order slice from path in tree for the form in data. column is unused.
 * RU: Назначает младший срез порта из tree по path в форму из data. column не используется. */
static void device_port_activate(GtkTreeView *tree, GtkTreePath *path,
                                 GtkTreeViewColumn *column, gpointer data)
{
    (void)column;
    DeviceForm *form = data;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter row;
    DevicePortChoice choice = {0};
    if (gtk_tree_model_get_iter(model, &row, path) &&
        device_port_choice(form, model, &row, &choice))
        device_port_apply(form, &choice, 0);
    g_free(choice.name);
}

typedef struct {
    const DevicePortChoice *choice;
    GtkLabel *label;
} DeviceSlicePreview;

/* EN: Refresh the preview in data after the offset spin button changes.
 * RU: Обновляет предварительный просмотр из data после изменения смещения spin. */
static void device_slice_preview(GtkSpinButton *spin, gpointer data)
{
    DeviceSlicePreview *preview = data;
    gchar *value = device_port_slice(preview->choice, gtk_spin_button_get_value_as_int(spin));
    gtk_label_set_text(preview->label, value);
    g_free(value);
}

/* EN: Open a modal offset picker from button for the selected port and active
 * field in form (data). Apply the expression only on confirmation.
 * RU: Открывает по button модальный выбор смещения для порта и поля формы из
 * data. Применяет выражение только после подтверждения. */
static void device_port_choose_slice(GtkButton *button, gpointer data)
{
    DeviceForm *form = data;
    GtkTreeView *tree = GTK_TREE_VIEW(form->browser->tree);
    GtkTreeModel *model;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(tree), &model, &row))
    {
        gtk_label_set_text(GTK_LABEL(form->status), "Select a port in the list.");
        return;
    }
    DevicePortChoice choice = {0};
    if (device_port_choice(form, model, &row, &choice))
    {
        if (choice.available == choice.width)
            device_port_apply(form, &choice, 0);
        else
        {
            GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
            GtkWidget *dialog = gtk_dialog_new_with_buttons("Choose Port Slice", parent,
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                "_Cancel", GTK_RESPONSE_CANCEL, "_Assign", GTK_RESPONSE_ACCEPT, NULL);
            GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
            gtk_container_set_border_width(GTK_CONTAINER(box), 12);
            GtkWidget *caption = gtk_label_new("Offset from the least significant bit");
            gtk_box_pack_start(GTK_BOX(box), caption, FALSE, FALSE, 4);
            GtkWidget *spin = gtk_spin_button_new_with_range(0, choice.available - choice.width, 1);
            gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 4);
            GtkWidget *label = gtk_label_new(NULL);
            gtk_label_set_selectable(GTK_LABEL(label), TRUE);
            gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 4);
            DeviceSlicePreview preview = {&choice, GTK_LABEL(label)};
            g_signal_connect(spin, "value-changed", G_CALLBACK(device_slice_preview), &preview);
            device_slice_preview(GTK_SPIN_BUTTON(spin), &preview);
            gtk_widget_show_all(dialog);
            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
                device_port_apply(form, &choice, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)));
            gtk_widget_destroy(dialog);
        }
    }
    g_free(choice.name);
}

/* EN: Translate value for entry into a device argument. For a fixed-width slice,
 * append an alias bridge to prefix using id. Return an owned argument or NULL.
 * RU: Преобразует value поля entry в аргумент устройства. Для среза фиксированной
 * ширины добавляет соединение в prefix с id. Возвращает новый аргумент или NULL. */
static gchar *device_port_argument(const DeviceForm *form, GtkWidget *entry, const gchar *value,
                                   guint id, GString *prefix)
{
    const gchar *spec = g_object_get_data(G_OBJECT(entry), "port-spec");
    const gchar *bracket = strchr(value, '[');
    if (!spec || *spec == 'a' || !bracket || entry == form->reset)
        return g_strdup(value);
    gchar *name = g_strndup(value, bracket - value);
    PortInfo *port = g_hash_table_lookup(form->browser->ports, name);
    GRegex *range = g_regex_new("^\[(-?[0-9]+)(?::(-?[0-9]+))?\]$", 0, 0, NULL);
    GMatchInfo *match = NULL;
    gchar *alias = NULL;
    gboolean matched = g_regex_match(range, bracket, 0, &match);
    if (port && port->width && port_browser_matches(port->direction, *spec) && matched)
    {
        gchar *left = g_match_info_fetch(match, 1);
        gchar *right = g_match_info_fetch(match, 2);
        gint64 first, last;
        if (g_ascii_string_to_signed(left, 10, -1000000, 1000000, &first, NULL) &&
            g_ascii_string_to_signed(*right ? right : left, 10, -1000000, 1000000, &last, NULL) &&
            ABS(first - last) + 1 == device_width(form, spec))
        {
            guint offset = (guint)gtk_text_buffer_get_char_count(form->browser->recipe);
            alias = g_strdup_printf("geany_slice_%u_%u", offset, id);
            gchar direction = *spec == 'c' ? (!strcmp(port->direction, "output") ? 'o' : 'b') : *spec;
            g_string_append_printf(prefix, "port-slice %s %s %c %d %d %" G_GINT64_FORMAT
                                   " %" G_GINT64_FORMAT "\n", alias, name, direction,
                                   port->left, port->right, first, last);
        }
        g_free(left);
        g_free(right);
    }
    g_match_info_free(match);
    g_regex_unref(range);
    g_free(name);
    return alias;
}

/* EN: Attach field and its title to row in form and return field.
 * RU: Добавляет field с подписью title в строку row формы form и возвращает field. */
static GtkWidget *device_field(DeviceForm *form, const gchar *title, GtkWidget *field, guint row)
{
    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), field);
    g_object_set_data(G_OBJECT(field), "port-label", label);
    g_object_set_data_full(G_OBJECT(field), "port-title", g_strdup(title), g_free);
    gtk_widget_set_hexpand(field, TRUE);
    gtk_grid_attach(GTK_GRID(form->grid), label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(form->grid), field, 1, row, 1, 1);
    return field;
}

/* EN: Rebuild the fields for combo's selected preset in the form stored in data.
 * RU: Перестраивает поля формы из data для шаблона, выбранного в combo. */
static void simulation_device_type(GtkComboBox *combo, gpointer data)
{
    DeviceForm *form = data;
    gint selected = gtk_combo_box_get_active(combo);
    if (selected < 0)
        return;
    GList *children = gtk_container_get_children(GTK_CONTAINER(form->grid));
    g_ptr_array_set_size(form->entries, 0);
    g_list_free_full(children, (GDestroyNotify)gtk_widget_destroy);
    form->file = form->reset = form->active = NULL;
    device_port_filter(form);
    const DevicePreset *preset = &device_presets[selected];
    gchar **fields = g_strsplit(preset->fields, "|", -1);
    gchar **specs = g_strsplit(preset->pins, "|", -1);
    guint pin_count = g_strv_length(specs);
    guint row = 0;
    for (; fields[row]; row++)
    {
        gchar *value = strchr(fields[row], '=');
        *value++ = 0;
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), value);
        if (row < pin_count)
            g_object_set_data_full(G_OBJECT(entry), "port-spec", g_strdup(specs[row]), g_free);
        g_signal_connect(entry, "focus-in-event", G_CALLBACK(device_port_focus), form);
        g_signal_connect(entry, "changed", G_CALLBACK(device_width_changed), form);
        if (row == 0)
            gtk_widget_set_name(entry, "insight-device-first-field");
        g_ptr_array_add(form->entries, device_field(form, fields[row], entry, row));
    }
    g_strfreev(fields);
    g_strfreev(specs);
    if (preset->script || preset->image >= 0)
    {
        GtkWidget *chooser = gtk_file_chooser_button_new("Choose device file", GTK_FILE_CHOOSER_ACTION_OPEN);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), form->directory);
        form->file = device_field(form, preset->script ? "Lua script" : "Memory image (optional)", chooser, row++);
        if (!preset->script)
        {
            GtkWidget *clear = gtk_button_new_with_label("Clear image");
            g_signal_connect_swapped(clear, "clicked", G_CALLBACK(gtk_file_chooser_unselect_all), chooser);
            gtk_grid_attach(GTK_GRID(form->grid), clear, 1, row++, 1, 1);
        }
    }
    if (preset->script)
    {
        form->reset = device_field(form, "Reset signal (optional, 1 bit)", gtk_entry_new(), row);
        g_object_set_data(G_OBJECT(form->reset), "port-spec", "o1");
        g_signal_connect(form->reset, "focus-in-event", G_CALLBACK(device_port_focus), form);
    }
    device_width_changed(NULL, form);
    gtk_widget_show_all(form->grid);
}

/* EN: Append value to rule as one shell-quoted token.
 * RU: Добавляет value в rule одним аргументом в кавычках. */
static void device_argument(GString *rule, const gchar *value)
{
    gchar *quoted = g_shell_quote(value);
    g_string_append_printf(rule, " %s", quoted);
    g_free(quoted);
}

/* EN: Open the modal device form from button, read the source snapshot and append a
 * validated rule to the recipe buffer in data.
 * RU: Открывает модальную форму по button, показывает исходник и добавляет проверенное
 * правило в буфер рецепта, переданный через data. */
static void simulation_device_wizard(GtkButton *button, gpointer data)
{
    GtkTextBuffer *buffer = data;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    DeviceForm form = {.entries = g_ptr_array_new(),
        .directory = g_object_get_data(G_OBJECT(button), "source-directory")};
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Test Device", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Insert Rule", GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 960, 520);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *combo = gtk_combo_box_text_new();
    gtk_widget_set_name(combo, "insight-test-device-type");
    for (guint i = 0; i < G_N_ELEMENTS(device_presets); i++)
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), device_presets[i].rule, device_presets[i].rule);
    gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 6);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    form.grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(form.grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(form.grid), 4);
    gtk_container_add(GTK_CONTAINER(scroll), form.grid);
    PortBrowser browser = {
        .source = g_object_get_data(G_OBJECT(button), "source-snapshot"),
        .expanded = g_object_get_data(G_OBJECT(button), "expanded-source"),
        .preprocess_error = g_object_get_data(G_OBJECT(button), "preprocess-error"),
        .recipe = buffer, .choosing = TRUE
    };
    GtkWidget *panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(panes), scroll, TRUE, FALSE);
    GtkWidget *browser_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(browser_box), port_browser_widget(&browser), TRUE, TRUE, 0);
    GtkWidget *choose_slice = gtk_button_new_with_label("Choose Slice…");
    g_signal_connect(choose_slice, "clicked", G_CALLBACK(device_port_choose_slice), &form);
    gtk_box_pack_start(GTK_BOX(browser_box), choose_slice, FALSE, FALSE, 0);
    gtk_paned_pack2(GTK_PANED(panes), browser_box, TRUE, FALSE);
    form.browser = &browser;
    g_signal_connect(browser.tree, "row-activated", G_CALLBACK(device_port_activate), &form);
    gtk_paned_set_position(GTK_PANED(panes), 420);
    gtk_box_pack_start(GTK_BOX(box), panes, TRUE, TRUE, 0);
    GtkWidget *status = gtk_label_new(NULL);
    form.status = status;
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 6);
    g_signal_connect(combo, "changed", G_CALLBACK(simulation_device_type), &form);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_widget_show_all(dialog);
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        gint selected = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (selected < 0)
            continue;
        const DevicePreset *preset = &device_presets[selected];
        gchar *native = form.file ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(form.file)) : NULL;
        gchar *file = native ? g_filename_to_utf8(native, -1, NULL, NULL, NULL) : NULL;
        gboolean valid = (!native || file) && (!preset->script || file) && (!file || !strpbrk(file, "\r\n"));
        GString *rule = g_string_new(NULL);
        GPtrArray *arguments = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; i < form.entries->len; i++)
        {
            GtkWidget *entry = form.entries->pdata[i];
            const gchar *value = gtk_entry_get_text(GTK_ENTRY(entry));
            gchar *argument = device_port_argument(&form, entry, value, i, rule);
            valid = valid && argument && *value && !strpbrk(value, "\r\n");
            g_ptr_array_add(arguments, argument ? argument : g_strdup(""));
        }
        if (file && preset->image >= 0)
        {
            g_string_append(rule, "memory-image");
            device_argument(rule, arguments->pdata[preset->image]);
            device_argument(rule, file);
            g_string_append_c(rule, '\n');
        }
        g_string_append(rule, preset->rule);
        for (guint i = 0; i < arguments->len; i++)
            device_argument(rule, arguments->pdata[i]);
        g_ptr_array_unref(arguments);
        if (preset->script && file)
            device_argument(rule, file);
        const gchar *reset = form.reset ? gtk_entry_get_text(GTK_ENTRY(form.reset)) : "";
        if (*reset)
        {
            valid = valid && simulation_capture_signal(reset);
            device_argument(rule, reset);
        }
        GError *error = NULL;
        if (valid)
        {
            gchar *recipe = !strcmp(preset->rule, "module") ? g_strdup(rule->str) :
                            g_strconcat("module geany_device_check\n", rule->str, NULL);
            GPtrArray *sources = g_ptr_array_new_with_free_func(g_free);
            GHashTable *images = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                       (GDestroyNotify)g_bytes_unref);
            gchar *generated = simulation_port_harness(recipe, form.directory, sources, images, &error);
            valid = generated != NULL;
            g_free(generated);
            g_hash_table_unref(images);
            g_ptr_array_unref(sources);
            g_free(recipe);
        }
        g_free(native);
        g_free(file);
        if (valid)
        {
            GtkTextIter end, previous;
            gtk_text_buffer_get_end_iter(buffer, &end);
            previous = end;
            gtk_text_buffer_begin_user_action(buffer);
            if (gtk_text_iter_backward_char(&previous) && gtk_text_iter_get_char(&previous) != '\n')
                gtk_text_buffer_insert(buffer, &end, "\n", 1);
            gtk_text_buffer_insert(buffer, &end, rule->str, rule->len);
            gtk_text_buffer_insert(buffer, &end, "\n", 1);
            gtk_text_buffer_end_user_action(buffer);
        }
        else
            gtk_label_set_text(GTK_LABEL(status), error ? error->message :
                               "Check the required fields and select a Lua script when requested.");
        g_clear_error(&error);
        g_string_free(rule, TRUE);
        if (valid)
            break;
    }
    g_clear_pointer(&browser.ports, g_hash_table_unref);
    g_free(browser.module);
    g_clear_pointer(&browser.query_regex, g_regex_unref);
    gtk_widget_destroy(dialog);
    g_ptr_array_unref(form.entries);
}

/* EN: Edit the recipe saved at path in a modal child of parent. run supplies the compiler,
 * input sources, and include path. On cancellation, publish preprocessing errors.
 * On acceptance, optional output
 * pointers harness, model_sources and port_images receive the generated configuration.
 * RU: Редактирует рецепт из path в модальном окне parent. run задаёт компилятор,
 * исходники и путь include. При отмене публикует ошибки препроцессора.
 * При подтверждении необязательные указатели harness,
 * model_sources и port_images получают сгенерированную конфигурацию. */
static gboolean simulation_edit_ports(GtkWindow *parent, const gchar *path, const WaveSimulation *run,
                                      gchar **harness, GPtrArray **model_sources, GHashTable **port_images)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Port Signals", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        harness ? "_Start Debugging" : "_Save for Next Run", GTK_RESPONSE_ACCEPT, NULL);
    g_object_ref_sink(dialog);
    gchar *source_directory = g_path_get_dirname(run->sources[0]);
    gchar *source_snapshot = port_browser_source(run->sources[0]);
    gchar *preprocess_error = NULL;
    gchar *expanded = port_browser_preprocess(run, &preprocess_error);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 390);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hint = gtk_label_new(
        "Leave empty to run an existing testbench. Otherwise enter one rule per line (times in ns).\n"
        "module NAME   ·   duration NS\n"
        "clock PORT START HIGH LOW   ·   clock-hz PORT START HZ\n"
        "pulse PORT START LENGTH 0|1   ·   value PORT WIDTH EXPR\n"
        "bus PORT WIDTH INTERVAL EXPR   ·   output PORT WIDTH\n"
        "capture-trigger rise|fall SIGNAL   ·   capture-trigger match SIGNAL SIZED_HEX_OR_BINARY\n"
        "button PORT [IDLE [LENGTH_NS]]   ·   switch PORT [INITIAL]\n"
        "slider PORT [WIDTH=8 [INITIAL=0]]\n"
        "scope SIGNAL   ·   scope-xy X Y   ·   scope-xyz X Y Z\n"
        "led PORT [ACTIVE=1]   ·   display PORT [WIDTH=8 [hex|dec|bin]]\n"
        "live-interval NS   ·   live-input PORT WIDTH INITIAL   ·   monitor PORT WIDTH\n"
        "Memory presets: sram, sdram, ddr2, ddr3, ddr4, spi-nor, spi-nand, spi-eeprom, spi-psram, i2c-eeprom.\n"
        "Use a preset alone for standard port names, or list your ports below. '-' uses the default.\n"
        "sram ADDR DQ CE_N WE_N OE_N [ADDRESS_BITS=16 [DATA_BITS=8]]\n"
        "sdram CK CKE CS_N RAS_N CAS_N WE_N A BA DQ DQM\n"
        "memory-image ADDR_OR_SPI_CS_N_OR_SDA FILE (before the memory rule)\n"
        "i2c-eeprom SCL SDA [ADDRESS=0x50]\n"
        "spi-lua [SCK CS_N MOSI MISO] SCRIPT [RESET_N]\n"
        "i2c-lua [SCL SDA ADDRESS] SCRIPT [RESET_N]\n"
        "spi-nor / spi-nand / spi-eeprom / spi-psram SCK CS_N MOSI MISO\n"
        "ddr2 CK CKE CS_N RAS_N CAS_N WE_N A BA DQ DQS DM [RL=3 [WL=2]] (functional x8)\n"
        "ddr3 CK RESET_N CKE CS_N RAS_N CAS_N WE_N A BA DQ DQS DM [RL=3 [WL=2]] (functional x8)\n"
        "ddr4 CK RESET_N CKE CS_N ACT_N A BA BG DQ DQS DM_N [RL=3 [WL=2]] (functional x8)\n"
        "uart RX TX BAUD [console]\n"
        "model INSTANCE MODULE SOURCE   ·   model-link INSTANCE DUT_PORT MODEL_PORT WIDTH\n"
        "model-share INSTANCE DUT_PORT MODEL_PORT   ·   model-param / model-value INSTANCE NAME EXPR\n"
        "Use step in bus expressions and $time for simulation time. Quote formulas containing spaces.");
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    GtkWidget *syntax = gtk_expander_new("Text rule reference");
    gtk_container_add(GTK_CONTAINER(syntax), hint);
    gtk_box_pack_start(GTK_BOX(box), syntax, FALSE, FALSE, 4);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkWidget *wizard = gtk_button_new_with_label("Add Test Device…");
    gtk_widget_set_halign(wizard, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(wizard), "source-directory", source_directory);
    g_object_set_data(G_OBJECT(wizard), "source-snapshot", source_snapshot);
    g_object_set_data(G_OBJECT(wizard), "expanded-source", expanded);
    g_object_set_data(G_OBJECT(wizard), "preprocess-error", preprocess_error);
    g_signal_connect(wizard, "clicked", G_CALLBACK(simulation_device_wizard), buffer);
    gtk_box_pack_start(GTK_BOX(box), wizard, FALSE, FALSE, 4);
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
    PortBrowser browser = {
        .source = source_snapshot, .expanded = expanded, .preprocess_error = preprocess_error,
        .recipe = buffer
    };
    GtkWidget *panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(panes), scroll, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(panes), port_browser_widget(&browser), TRUE, FALSE);
    gtk_paned_set_position(GTK_PANED(panes), 320);
    gulong changed = g_signal_connect(buffer, "changed", G_CALLBACK(port_browser_refresh), &browser);
    gtk_box_pack_start(GTK_BOX(box), panes, TRUE, TRUE, 0);
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
        GPtrArray *sources = g_ptr_array_new_with_free_func(g_free);
        GHashTable *images = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                    (GDestroyNotify)g_bytes_unref);
        gchar *generated = strlen(recipe) <= 16384 ?
            simulation_port_harness(recipe, source_directory, sources, images, &error) : NULL;
        gchar *directory = g_path_get_dirname(path);
        if (!error && strlen(recipe) <= 16384 && g_mkdir_with_parents(directory, 0700) == 0 &&
            g_file_set_contents(path, recipe, -1, &error))
        {
            accepted = TRUE;
            if (harness)
                *harness = g_steal_pointer(&generated);
            if (model_sources)
                *model_sources = g_steal_pointer(&sources);
            if (port_images)
                *port_images = g_steal_pointer(&images);
        }
        if (!accepted)
            gtk_label_set_text(GTK_LABEL(status), error ? error->message : "Cannot save the port recipe.");
        g_free(directory);
        g_clear_pointer(&sources, g_ptr_array_unref);
        g_clear_pointer(&images, g_hash_table_unref);
        g_free(generated);
        g_free(recipe);
        g_clear_error(&error);
        if (accepted)
            break;
    }
    g_signal_handler_disconnect(buffer, changed);
    g_free(source_snapshot);
    g_free(expanded);
    g_free(source_directory);
    g_clear_pointer(&browser.ports, g_hash_table_unref);
    g_free(browser.module);
    g_clear_pointer(&browser.query_regex, g_regex_unref);
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
    if (!accepted && preprocess_error)
        msgwin_debugger_report(preprocess_error, run->include_dir, NULL);
    g_free(preprocess_error);
    return accepted;
}

/* EN: Reopen saved port settings from button for the running simulation in data.
 * RU: Повторно открывает настройки портов по кнопке button для сеанса из data. */
static void simulation_port_settings(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    gchar *path = g_strdup(run->port_settings);
    simulation_edit_ports(GTK_WINDOW(run->dialog), path, run, NULL, NULL, NULL);
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

/* EN: Run argv in directory with bin on PATH. cancel stops the child. On failure,
 * return FALSE and capture up to 256 KiB of diagnostics in error.
 * RU: Запускает argv в directory с bin в PATH. cancel останавливает процесс.
 * При ошибке возвращает FALSE и сохраняет до 256 КиБ диагностики в error. */
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
    gchar bytes[8192];
    GString *diagnostic = g_string_new(NULL);
    const gsize limit = 256 * 1024;
    gboolean truncated = FALSE;
    gssize count;
    while ((count = g_input_stream_read(output, bytes, sizeof bytes, cancel, error)) > 0)
    {
        gsize available = MIN((gsize)count, limit - diagnostic->len);
        g_string_append_len(diagnostic, bytes, available);
        truncated |= available < (gsize)count;
    }
    gboolean ok = count == 0 && g_subprocess_wait_check(process, cancel, error);
    if (count < 0 || g_cancellable_is_cancelled(cancel))
    {
        g_subprocess_force_exit(process);
        g_subprocess_wait(process, NULL, NULL);
    }
    if (!ok && diagnostic->len && !g_cancellable_is_cancelled(cancel))
    {
        if (truncated)
            g_string_append(diagnostic, "\nAdditional diagnostics omitted.\n");
        gchar *message = g_utf8_make_valid(diagnostic->str, diagnostic->len);
        g_prefix_error(error, "%s\n", message);
        g_free(message);
    }
    g_string_free(diagnostic, TRUE);
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
    gchar *runtime_bin = g_path_get_dirname(run->runtime);
    gchar *runtime_prefix = g_path_get_dirname(runtime_bin);
    gchar *modules = g_build_filename(runtime_prefix, "lib", "ivl", NULL);
    const gchar *argv[] = {run->runtime, "-i", "-s", "simulation.vvp", NULL};
    const gchar *device_argv[] = {run->runtime, "-M", modules, "-m", "geany_devices",
                                  "-i", "-s", "simulation.vvp", NULL};
    gboolean devices = run->port_harness &&
        (strstr(run->port_harness, "$geany_sram(") || strstr(run->port_harness, "$geany_sdram(") ||
         strstr(run->port_harness, "$geany_ddr2(") || strstr(run->port_harness, "$geany_ddr3(") || strstr(run->port_harness, "$geany_ddr4(") ||
         strstr(run->port_harness, "$geany_spi_") || strstr(run->port_harness, "$geany_i2c_"));
    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, devices ? device_argv : argv, error);
    g_free(modules);
    g_free(runtime_prefix);
    g_free(runtime_bin);
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
        GPtrArray *model_directories = g_ptr_array_new_with_free_func(g_free);
        if (run->port_model_sources)
            for (guint i = 0; i < run->port_model_sources->len; i++)
            {
                const gchar *source = run->port_model_sources->pdata[i];
                gchar *model_directory = g_path_get_dirname(source);
                g_ptr_array_add(model_directories, model_directory);
                g_ptr_array_add(command, "-I");
                g_ptr_array_add(command, model_directory);
                if (!g_strv_contains((const gchar *const *)run->sources, source))
                    g_ptr_array_add(command, (gpointer)source);
            }
        if (run->port_images)
        {
            GHashTableIter iter;
            gpointer anchor, image;
            g_hash_table_iter_init(&iter, run->port_images);
            while (!error && g_hash_table_iter_next(&iter, &anchor, &image))
            {
                gchar *name = simulation_memory_filename(anchor);
                gchar *path = g_build_filename(directory, name, NULL);
                gsize size;
                const gchar *bytes = g_bytes_get_data(image, &size);
                g_file_set_contents(path, size ? bytes : "", size, &error);
                g_free(path);
                g_free(name);
            }
        }
        gchar *harness_path = NULL;
        if (!error && run->port_harness)
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
            if (!vcd && run->port_harness && strstr(run->port_harness, "// GEANY-CAPTURE-TRIGGER") &&
                g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            {
                g_clear_error(&error);
                g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                    "The capture trigger did not fire before the simulation ended.");
            }
            if (vcd)
                capture = read_capture_file(vcd, cancel, &error);
            g_free(vcd);
        }
        g_ptr_array_unref(command);
        g_ptr_array_unref(model_directories);
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

static void simulation_update_indicator(WaveSimulation *run, const gchar *port, const gchar *bits)
{
    GtkLabel *label = run->live_indicators ? g_hash_table_lookup(run->live_indicators, port) : NULL;
    if (!label)
        return;
    const gchar *format = g_object_get_data(G_OBJECT(label), "geany-format");
    if (*format == '0' || *format == '1')
    {
        const gchar *state = *bits == *format ? "●  ON" :
                             *bits == '0' || *bits == '1' ? "○  OFF" :
                             *bits == 'z' ? "◉  Z" : "◉  X";
        gtk_label_set_text(label, state);
        return;
    }
    guint64 value;
    gchar *reading;
    if (!strcmp(format, "bin") || !instrument_number(bits, &value))
        reading = g_strdup_printf("0b%s", bits);
    else if (!strcmp(format, "hex"))
        reading = g_strdup_printf("0x%0*" G_GINT64_MODIFIER "X", (gint)((strlen(bits) + 3) / 4), value);
    else
        reading = g_strdup_printf("%" G_GUINT64_FORMAT, value);
    gtk_label_set_text(label, reading);
    g_free(reading);
}

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
        if (run->live_scopes && g_str_has_prefix(text, "GEANY-SCOPE\t"))
            simulation_scope_record(run, text + strlen("GEANY-SCOPE\t"));
        const gchar *port = strstr(text, "GEANY-PORT\t");
        if (run->live_port_samples && port)
        {
            gchar **fields = g_strsplit(port + strlen("GEANY-PORT\t"), "\t", 3);
            if (g_strv_length(fields) == 2 && simulation_port_name(fields[0]) &&
                *fields[1] && strlen(fields[1]) <= 64 && strspn(fields[1], "01xz") == strlen(fields[1]) &&
                (g_hash_table_size(run->live_port_samples) < 128 ||
                 g_hash_table_contains(run->live_port_samples, fields[0])))
            {
                const gchar *previous = g_hash_table_lookup(run->live_port_samples, fields[0]);
                if (g_strcmp0(previous, fields[1]))
                {
                    g_hash_table_replace(run->live_port_samples, g_strdup(fields[0]), g_strdup(fields[1]));
                    simulation_update_indicator(run, fields[0], fields[1]);
                }
            }
            g_strfreev(fields);
        }
        const gchar *uart = strstr(text, "GEANY-UART\t");
        if (run->uart_log && uart && strlen(uart) == strlen("GEANY-UART\t") + 2 &&
            g_ascii_isxdigit(uart[strlen("GEANY-UART\t")]) &&
            g_ascii_isxdigit(uart[strlen("GEANY-UART\t") + 1]))
        {
            guint byte = (guint)g_ascii_strtoull(uart + strlen("GEANY-UART\t"), NULL, 16);
            if (byte == '\n' || byte == '\r' || byte == '\t' || (byte >= 32 && byte < 127))
                g_string_append_c(run->uart_log, byte);
            else
                g_string_append_printf(run->uart_log, "\\x%02x", byte);
            if (run->uart_log->len > 65536)
                g_string_erase(run->uart_log, 0, 4096);
            if (run->uart_view)
            {
                GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->uart_view));
                gtk_text_buffer_set_text(buffer, run->uart_log->str, run->uart_log->len);
                GtkTextIter end;
                gtk_text_buffer_get_end_iter(buffer, &end);
                gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(run->uart_view), &end, 0, FALSE, 0, 0);
            }
        }
        const gchar *sent = strstr(text, "GEANY-UART-SENT\t");
        if (run->uart_log && sent &&
            g_ascii_strtoull(sent + strlen("GEANY-UART-SENT\t"), NULL, 10) == run->uart_send_sequence)
        {
            run->uart_send_sequence = 0;
            if (run->uart_entry)
                gtk_widget_set_sensitive(run->uart_entry, TRUE);
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

/* Hide high-rate samples from the console, including records split across reads. */
static GString *simulation_scope_console(WaveSimulation *run, const gchar *bytes, gsize count)
{
    static const gchar prefix[] = "GEANY-SCOPE\t";
    GString *visible = g_string_sized_new(count);
    gint matched = run->scope_console_prefix;
    for (gsize i = 0; i < count; i++)
    {
        gchar c = bytes[i];
        if (matched == (gint)sizeof prefix - 1)
        {
            if (c == '\n')
                matched = 0;
            continue;
        }
        if (matched >= 0)
        {
            if (c == prefix[matched])
            {
                matched++;
                continue;
            }
            g_string_append_len(visible, prefix, matched);
            matched = -1;
        }
        g_string_append_c(visible, c);
        if (c == '\n')
            matched = 0;
    }
    run->scope_console_prefix = matched;
    return visible;
}

static gboolean simulation_scope_bounds(const LiveScope *scope, guint start, guint64 *low, guint64 *high)
{
    gboolean known = FALSE;
    for (guint axis = 0; axis < scope->axes; axis++)
    {
        low[axis] = G_MAXUINT64;
        high[axis] = 0;
    }
    for (guint i = 0; i < scope->count; i++)
    {
        const InstrumentPoint *point = &scope->samples[(start + i) % LIVE_SCOPE_POINTS].point;
        if (!point->valid)
            continue;
        known = TRUE;
        for (guint axis = 0; axis < scope->axes; axis++)
        {
            low[axis] = MIN(low[axis], point->values[axis]);
            high[axis] = MAX(high[axis], point->values[axis]);
        }
    }
    return known;
}

static void simulation_scope_refresh(WaveSimulation *run)
{
    for (guint i = 0; run->live_scopes && i < run->live_scopes->len; i++)
    {
        LiveScope *scope = run->live_scopes->pdata[i];
        if (!scope->dirty)
            continue;
        scope->dirty = FALSE;
        guint first = (scope->next + LIVE_SCOPE_POINTS - scope->count) % LIVE_SCOPE_POINTS;
        guint last = (scope->next + LIVE_SCOPE_POINTS - 1) % LIVE_SCOPE_POINTS;
        GString *status = g_string_new(NULL);
        g_string_append_printf(status, "%u samples · %" G_GUINT64_FORMAT "–%" G_GUINT64_FORMAT " ns",
                               scope->count, scope->samples[first].time, scope->samples[last].time);
        guint64 low[3], high[3];
        if (simulation_scope_bounds(scope, first, low, high))
            for (guint axis = 0; axis < scope->axes; axis++)
                g_string_append_printf(status, " · %c %" G_GUINT64_FORMAT "–%" G_GUINT64_FORMAT,
                                       scope->axes == 1 ? 'Y' : "XYZ"[axis], low[axis], high[axis]);
        else
            g_string_append(status, " · no known samples");
        gtk_label_set_text(GTK_LABEL(scope->status), status->str);
        g_string_free(status, TRUE);
        gtk_widget_queue_draw(scope->drawing);
    }
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
        simulation_scope_refresh(run);
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
        GString *visible = run->live_scopes ? simulation_scope_console(run, bytes, (gsize)count) : NULL;
        gchar *valid = visible ? g_utf8_make_valid(visible->str, visible->len) :
                                 g_utf8_make_valid(bytes, count);
        if (visible)
            g_string_free(visible, TRUE);
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
    if (run->live_devices)
        gtk_widget_set_sensitive(run->live_devices, running);
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

static void simulation_uart_enter(GtkEntry *entry, gpointer data)
{
    WaveSimulation *run = data;
    const gchar *text = gtk_entry_get_text(entry);
    if (!*text || run->uart_send_sequence)
        return;
    gchar *directory = simulation_live_directory(run);
    if (!directory)
        return;
    gchar *path = g_build_filename(directory, "geany-uart-input", NULL);
    /* Alternate sequence numbers; the harness remembers the last accepted record. */
    guint next = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(run->dialog), "uart-next")) == 1 ? 2 : 1;
    GString *record = g_string_new(NULL);
    g_string_append_printf(record, "%u\n", next);
    for (const guchar *byte = (const guchar *)text; *byte; byte++)
        g_string_append_printf(record, "%02x\n", (guint)*byte);
    if (g_file_set_contents(path, record->str, record->len, NULL))
    {
        g_object_set_data(G_OBJECT(run->dialog), "uart-next", GUINT_TO_POINTER(next));
        run->uart_send_sequence = next;
        gtk_entry_set_text(entry, "");
        gtk_widget_set_sensitive(GTK_WIDGET(entry), FALSE);
    }
    g_string_free(record, TRUE);
    g_free(path);
    g_free(directory);
}

static void simulation_uart_closed(GtkWidget *window, gpointer data)
{
    (void)window;
    WaveSimulation *run = data;
    run->uart_window = run->uart_view = run->uart_entry = NULL;
}

static void simulation_uart_show(GtkButton *button, gpointer data)
{
    (void)button;
    WaveSimulation *run = data;
    if (run->uart_window)
    {
        gtk_window_present(GTK_WINDOW(run->uart_window));
        return;
    }
    GtkWidget *window = gtk_dialog_new_with_buttons("UART Console", GTK_WINDOW(run->dialog),
        GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    run->uart_window = window;
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 330);
    g_signal_connect_swapped(window, "response", G_CALLBACK(gtk_widget_destroy), window);
    g_signal_connect(window, "destroy", G_CALLBACK(simulation_uart_closed), run);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(window));
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    run->uart_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(run->uart_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(run->uart_view), TRUE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(run->uart_view)), run->uart_log->str, -1);
    gtk_container_add(GTK_CONTAINER(scroll), run->uart_view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    run->uart_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(run->uart_entry), 256);
    gtk_entry_set_placeholder_text(GTK_ENTRY(run->uart_entry), "Text to send; press Enter");
    gtk_widget_set_sensitive(run->uart_entry, !run->uart_send_sequence);
    g_signal_connect(run->uart_entry, "activate", G_CALLBACK(simulation_uart_enter), run);
    gtk_box_pack_start(GTK_BOX(box), run->uart_entry, FALSE, FALSE, 4);
    gtk_widget_show_all(window);
}

static gboolean simulation_control_write(WaveSimulation *run, GtkWidget *widget, guint value)
{
    const gchar *port = g_object_get_data(G_OBJECT(widget), "geany-port");
    gchar *directory = simulation_live_directory(run);
    if (!directory)
        return FALSE;
    gchar *name = g_strdup_printf("geany-control-%s", port);
    gchar *path = g_build_filename(directory, name, NULL);
    gchar *text = g_strdup_printf("%u\n", value);
    gboolean written = g_file_set_contents(path, text, -1, NULL);
    if (!written)
        gtk_widget_set_tooltip_text(widget, "Could not update the simulation input.");
    g_free(text);
    g_free(path);
    g_free(name);
    g_free(directory);
    return written;
}

static void simulation_control_input(GtkButton *button, gpointer data)
{
    gboolean toggle = GTK_IS_TOGGLE_BUTTON(button);
    guint sequence = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "geany-sequence"));
    guint value = toggle ? (guint)gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)) : sequence + 1;
    if (simulation_control_write(data, GTK_WIDGET(button), value) && !toggle)
        g_object_set_data(G_OBJECT(button), "geany-sequence", GUINT_TO_POINTER(value));
}

static void simulation_slider_input(GtkRange *slider, gpointer data)
{
    simulation_control_write(data, GTK_WIDGET(slider), (guint)gtk_range_get_value(slider));
}

static GtkWidget *simulation_live_device_box(WaveSimulation *run, GtkWidget *contents)
{
    if (!run->live_devices)
    {
        run->live_devices = gtk_flow_box_new();
        gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(run->live_devices), GTK_SELECTION_NONE);
        gtk_widget_set_sensitive(run->live_devices, FALSE);
        gtk_box_pack_start(GTK_BOX(contents), run->live_devices, FALSE, FALSE, 0);
    }
    return run->live_devices;
}

/* EN: Return the human-readable port or slice name for alias in run's harness.
 * RU: Возвращает читаемое имя порта или среза для alias из тестового модуля run. */
static gchar *simulation_live_port_caption(const WaveSimulation *run, const gchar *alias)
{
    gchar *marker = g_strdup_printf("// GEANY-PORT-SLICE %s ", alias);
    const gchar *start = strstr(run->port_harness, marker);
    if (start)
        start += strlen(marker);
    g_free(marker);
    const gchar *end = start ? strchr(start, '\n') : NULL;
    return end ? g_strndup(start, end - start) : g_strdup(alias);
}

/* EN: Create the live buttons and switches for run in contents.
 * RU: Создаёт живые кнопки и переключатели run в contents. */
static void simulation_live_controls(WaveSimulation *run, GtkWidget *contents)
{
    const gchar *marker = "// GEANY-CONTROL ";
    const gchar *cursor = run->port_harness;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '\n');
        if (!end)
            break;
        gchar *line = g_strndup(cursor, end - cursor);
        gchar **parts = g_strsplit(line, " ", 3);
        gboolean toggle = !strcmp(parts[0], "switch");
        gchar *caption = simulation_live_port_caption(run, parts[1]);
        GtkWidget *widget = toggle ? gtk_check_button_new_with_label(caption) :
                                     gtk_button_new_with_label(caption);
        g_free(caption);
        if (toggle)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), *parts[2] == '1');
        g_object_set_data_full(G_OBJECT(widget), "geany-port", g_strdup(parts[1]), g_free);
        g_signal_connect(widget, toggle ? "toggled" : "clicked", G_CALLBACK(simulation_control_input), run);
        gtk_container_add(GTK_CONTAINER(simulation_live_device_box(run, contents)), widget);
        g_strfreev(parts);
        g_free(line);
        cursor = end;
    }
}

/* EN: Create the live sliders for run in contents.
 * RU: Создаёт живые ползунки run в contents. */
static void simulation_live_sliders(WaveSimulation *run, GtkWidget *contents)
{
    const gchar *marker = "// GEANY-SLIDER ";
    const gchar *cursor = run->port_harness;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '\n');
        if (!end)
            break;
        gchar *line = g_strndup(cursor, end - cursor);
        gchar **parts = g_strsplit(line, " ", 3);
        gdouble maximum = g_ascii_strtod(parts[1], NULL);
        gchar *caption = simulation_live_port_caption(run, parts[0]);
        GtkWidget *frame = gtk_frame_new(caption);
        GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, maximum, 1);
        gchar *name = g_strconcat("insight-slider-", caption, NULL);
        g_free(caption);
        gtk_widget_set_name(scale, name);
        g_free(name);
        gtk_widget_set_size_request(scale, 160, -1);
        gtk_scale_set_digits(GTK_SCALE(scale), 0);
        gtk_range_set_round_digits(GTK_RANGE(scale), 0);
        gtk_range_set_value(GTK_RANGE(scale), g_ascii_strtod(parts[2], NULL));
        g_object_set_data_full(G_OBJECT(scale), "geany-port", g_strdup(parts[0]), g_free);
        g_signal_connect(scale, "value-changed", G_CALLBACK(simulation_slider_input), run);
        gtk_container_add(GTK_CONTAINER(frame), scale);
        gtk_container_add(GTK_CONTAINER(simulation_live_device_box(run, contents)), frame);
        g_strfreev(parts);
        g_free(line);
        cursor = end;
    }
}

/* EN: Create live indicator cards for run in contents.
 * RU: Создаёт панели индикаторов run в contents. */
static void simulation_live_indicators(WaveSimulation *run, GtkWidget *contents)
{
    const gchar *marker = "// GEANY-INDICATOR ";
    const gchar *cursor = run->port_harness;
    GtkWidget *flow = NULL;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '\n');
        if (!end)
            break;
        gchar *line = g_strndup(cursor, end - cursor);
        gchar **parts = g_strsplit(line, " ", 2);
        if (!flow)
        {
            flow = gtk_flow_box_new();
            gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
            gtk_box_pack_start(GTK_BOX(contents), flow, FALSE, FALSE, 0);
            run->live_indicators = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        }
        gchar *caption = simulation_live_port_caption(run, parts[0]);
        GtkWidget *frame = gtk_frame_new(caption);
        g_free(caption);
        GtkWidget *value = gtk_label_new("—");
        gchar *name = g_strconcat("insight-indicator-", parts[0], NULL);
        gtk_widget_set_name(value, name);
        g_free(name);
        gtk_label_set_selectable(GTK_LABEL(value), TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(value), TRUE);
        gtk_label_set_line_wrap_mode(GTK_LABEL(value), PANGO_WRAP_CHAR);
        gtk_label_set_max_width_chars(GTK_LABEL(value), 32);
        gtk_container_set_border_width(GTK_CONTAINER(frame), 4);
        g_object_set_data_full(G_OBJECT(value), "geany-format", g_strdup(parts[1]), g_free);
        g_hash_table_insert(run->live_indicators, g_strdup(parts[0]), value);
        gtk_container_add(GTK_CONTAINER(frame), value);
        gtk_container_add(GTK_CONTAINER(flow), frame);
        g_strfreev(parts);
        g_free(line);
        cursor = end;
    }
}

static gboolean simulation_scope_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    LiveScope *scope = data;
    guint64 low[3], high[3];
    guint start = (scope->next + LIVE_SCOPE_POINTS - scope->count) % LIVE_SCOPE_POINTS;
    guint64 first = scope->samples[start].time;
    guint64 last = scope->samples[(scope->next + LIVE_SCOPE_POINTS - 1) % LIVE_SCOPE_POINTS].time;
    simulation_scope_bounds(scope, start, low, high);
    gdouble w = MAX(1, gtk_widget_get_allocated_width(widget) - 24);
    gdouble h = MAX(1, gtk_widget_get_allocated_height(widget) - 24);
    GdkRGBA color;
    gtk_style_context_get_color(gtk_widget_get_style_context(widget), gtk_widget_get_state_flags(widget), &color);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, .35);
    cairo_rectangle(cr, 12, 12, w, h);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, .18, .52, .82);
    gboolean previous = FALSE;
    gdouble previous_y = 0;
    for (guint i = 0; i < scope->count; i++)
    {
        const LiveScopeSample *sample = &scope->samples[(start + i) % LIVE_SCOPE_POINTS];
        if (!sample->point.valid)
        {
            previous = FALSE;
            continue;
        }
        gdouble x, y;
        instrument_project(&sample->point, low, high, scope->axes, &x, &y);
        if (scope->axes == 1)
        {
            y = x;
            x = last == first ? 0 : (gdouble)(sample->time - first) / (gdouble)(last - first);
        }
        x = 12 + x * w;
        y = 12 + (1 - y) * h;
        if (!previous)
        {
            cairo_move_to(cr, x, y);
            cairo_rel_line_to(cr, .5, 0);
            cairo_move_to(cr, x, y);
        }
        else
        {
            if (scope->axes == 1)
                cairo_line_to(cr, x, previous_y);
            cairo_line_to(cr, x, y);
        }
        previous = TRUE;
        previous_y = y;
    }
    cairo_stroke(cr);
    return FALSE;
}

static void simulation_live_scopes(WaveSimulation *run, GtkWidget *contents)
{
    const gchar *marker = "// GEANY-SCOPE ";
    const gchar *cursor = run->port_harness;
    GtkWidget *notebook = NULL;
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += strlen(marker);
        const gchar *end = strchr(cursor, '\n');
        if (!end)
            break;
        if (!notebook)
        {
            notebook = gtk_notebook_new();
            GtkWidget *expander = gtk_expander_new("Live Plots");
            gtk_container_add(GTK_CONTAINER(expander), notebook);
            gtk_box_pack_start(GTK_BOX(contents), expander, FALSE, FALSE, 0);
            run->live_scopes = g_ptr_array_new_with_free_func(g_free);
        }
        LiveScope *scope = g_new0(LiveScope, 1);
        scope->axes = (guint)(*cursor - '0');
        gchar *signals = g_strndup(cursor + 2, end - cursor - 2);
        GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget *heading = gtk_label_new(signals);
        gtk_label_set_line_wrap(GTK_LABEL(heading), TRUE);
        gtk_box_pack_start(GTK_BOX(page), heading, FALSE, FALSE, 0);
        g_free(signals);
        scope->drawing = gtk_drawing_area_new();
        gtk_widget_set_size_request(scope->drawing, 260, 130);
        g_signal_connect(scope->drawing, "draw", G_CALLBACK(simulation_scope_draw), scope);
        gtk_box_pack_start(GTK_BOX(page), scope->drawing, TRUE, TRUE, 0);
        scope->status = gtk_label_new("Waiting for samples…");
        gtk_label_set_line_wrap(GTK_LABEL(scope->status), TRUE);
        gchar *status_name = g_strdup_printf("insight-live-scope-%u", run->live_scopes->len + 1);
        gtk_widget_set_name(scope->status, status_name);
        g_free(status_name);
        gtk_box_pack_start(GTK_BOX(page), scope->status, FALSE, FALSE, 0);
        const gchar *kind = scope->axes == 1 ? "Time" : scope->axes == 2 ? "XY" : "XYZ";
        gchar *tab = g_strdup_printf("%s %u", kind, run->live_scopes->len + 1);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, gtk_label_new(tab));
        g_free(tab);
        g_ptr_array_add(run->live_scopes, scope);
        cursor = end;
    }
}

static void simulation_live_panel(WaveSimulation *run, GtkWidget *box)
{
    const gchar *marker = "$fopen(\"geany-input-";
    GtkWidget *panel = gtk_expander_new("Live Port I/O");
    GtkWidget *contents = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(panel), contents);
    gtk_box_pack_start(GTK_BOX(box), panel, FALSE, FALSE, 0);
    run->live_port_samples = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    simulation_live_controls(run, contents);
    simulation_live_sliders(run, contents);
    simulation_live_indicators(run, contents);
    simulation_live_scopes(run, contents);
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
    gtk_widget_set_name(run->live_port_display, "insight-live-port-readings");
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

/* EN: Finish the simulation task in result, display its capture or publish diagnostics.
 * object and data are unused.
 * RU: Завершает задачу из result, открывает осциллограмму или публикует ошибки.
 * object и data не используются. */
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
        msgwin_debugger_report(error->message, run->include_dir, NULL);
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

/* EN: Launch Icarus from bin with include_dir and sources, owned by the caller.
 * parent owns setup dialogs, interactive enables debugger controls, navigate and
 * open_location handle HDL navigation, settings_directory and project key saved
 * settings, and configure_ports opens the modal port setup. Errors appear in the UI.
 * RU: Запускает Icarus из bin для sources с include_dir. parent владеет диалогами,
 * interactive включает отладку, navigate и open_location открывают исходник,
 * settings_directory и project определяют сохранённые настройки, configure_ports
 * открывает модальную настройку портов. Ошибки показываются в интерфейсе. */
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
        gboolean accepted = simulation_edit_ports(parent, run->port_settings, run,
                                                   &run->port_harness, &run->port_model_sources, &run->port_images);
        if (!accepted)
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
        if (run->port_harness && strstr(run->port_harness, "// GEANY UART CONSOLE\n"))
        {
            run->uart_log = g_string_new(NULL);
            GtkWidget *uart = gtk_button_new_with_label("UART Console…");
            g_signal_connect(uart, "clicked", G_CALLBACK(simulation_uart_show), run);
            gtk_box_pack_start(GTK_BOX(running_actions), uart, FALSE, FALSE, 0);
        }
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
