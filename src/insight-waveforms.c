/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-waveforms.h"
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
typedef struct { gchar *name; Trace *trace; } Signal;
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
      g_string_append(name, fields->pdata[3]);
      Signal *signal = g_new(Signal, 1);
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
 if (viewer->navigate) viewer->navigate(gtk_entry_get_text(GTK_ENTRY(viewer->source)));
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

static void show_capture(GtkWindow *parent, Capture *capture, const gchar *session_path, InsightWaveSource navigate) {
 Viewer *viewer = g_new0(Viewer, 1);
 viewer->capture = capture;
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
  gchar *canonical = g_canonicalize_filename(path, NULL);
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
    InsightWaveSource navigate;
} WaveSimulation;

/* Release the run configuration after the worker and dialog have finished. */
/* Освобождает параметры запуска после завершения задачи и диалога. */
static void simulation_free(gpointer data)
{
    WaveSimulation *run = data;
    g_weak_ref_clear(&run->parent);
    g_clear_object(&run->dialog);
    g_strfreev(run->sources);
    g_free(run->compiler);
    g_free(run->runtime);
    g_free(run->include_dir);
    g_free(run);
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

/* Run one phase in the scratch directory and collect bounded failure details. */
/* Запускает этап во временной папке и читает ограниченный объём диагностики. */
static gboolean simulation_process(const gchar *const *argv, const gchar *directory,
                                   const gchar *bin, GCancellable *cancel, GError **error)
{
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return FALSE;
    gchar *log = g_build_filename(directory, "simulation.log", NULL);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, directory);
    g_subprocess_launcher_set_stdout_file_path(launcher, log);
    const gchar *existing = g_getenv("PATH");
    gchar *path = g_strconcat(bin, G_SEARCHPATH_SEPARATOR_S, existing ? existing : "", NULL);
    g_subprocess_launcher_setenv(launcher, "PATH", path, TRUE);
    g_free(path);

    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, argv, error);
    g_object_unref(launcher);
    gboolean ok = process && g_subprocess_wait_check(process, cancel, error);
    if (process && g_cancellable_is_cancelled(cancel))
    {
        g_subprocess_force_exit(process);
        g_subprocess_wait(process, NULL, NULL);
    }
    if (process && !ok && !g_cancellable_is_cancelled(cancel))
    {
        GFile *file = g_file_new_for_path(log);
        GFileInputStream *stream = g_file_read(file, NULL, NULL);
        if (stream)
        {
            gchar bytes[4096];
            gssize count = g_input_stream_read(G_INPUT_STREAM(stream), bytes, sizeof bytes, NULL, NULL);
            if (count > 0)
            {
                gchar *message = g_utf8_make_valid(bytes, count);
                g_prefix_error(error, "%s\n", message);
                g_free(message);
            }
            g_object_unref(stream);
        }
        g_object_unref(file);
    }
    g_clear_object(&process);
    g_free(log);
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
        for (guint i = 0; run->sources[i]; i++)
            g_ptr_array_add(command, run->sources[i]);
        g_ptr_array_add(command, NULL);
        const gchar *execute[] = {run->runtime, "simulation.vvp", NULL};
        if (simulation_process((const gchar *const *)command->pdata, directory, bin, cancel, &error) &&
            simulation_process(execute, directory, bin, cancel, &error))
        {
            gchar *vcd = simulation_find_vcd(directory, &error);
            if (vcd)
                capture = read_capture_file(vcd, cancel, &error);
            g_free(vcd);
        }
        g_ptr_array_unref(command);
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

/* Launch Icarus and open the resulting VCD on completion. */
/* Запускает Icarus и открывает полученный VCD после завершения. */
void insight_waveforms_simulate(GtkWindow *parent, const gchar *bin, const gchar *include_dir,
                                const gchar *const *sources, InsightWaveSource navigate)
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
    run->compiler = compiler;
    run->runtime = runtime;
    run->include_dir = g_strdup(include_dir);
    run->sources = g_strdupv((gchar **)sources);
    run->navigate = navigate;
    run->dialog = gtk_dialog_new_with_buttons("Verilog Simulation", parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    g_object_ref_sink(run->dialog);
    GtkWidget *label = gtk_label_new("Compiling and running the Verilog testbench…");
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(run->dialog))), label, FALSE, FALSE, 12);

    GCancellable *cancel = g_cancellable_new();
    g_signal_connect_object(run->dialog, "response", G_CALLBACK(simulation_cancel), cancel, 0);
    g_signal_connect_object(run->dialog, "destroy", G_CALLBACK(g_cancellable_cancel), cancel, G_CONNECT_SWAPPED);
    GTask *task = g_task_new(NULL, cancel, simulation_done, NULL);
    g_task_set_task_data(task, run, simulation_free);
    gtk_widget_show_all(run->dialog);
    g_task_run_in_thread(task, simulation_worker);
    g_object_unref(task);
    g_object_unref(cancel);
}
