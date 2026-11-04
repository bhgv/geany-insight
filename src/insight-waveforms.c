/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-waveforms.h"
#include <gio/gio.h>
#include <string.h>

#define VCD_BYTES (32u * 1024u * 1024u)
#define VCD_SIGNALS 4096u
#define VCD_EVENTS 250000u

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
 GtkWidget *window, *selector, *canvas, *value, *source;
 guint64 cursor, start, span;
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
 gint index = gtk_combo_box_get_active(GTK_COMBO_BOX(viewer->selector));
 return index < 0 ? NULL : viewer->capture->signals->pdata[index];
}

static guint upper_bound(Trace *trace, guint64 time) {
 guint lo = 0, hi = trace->edges->len;
 while (lo < hi) {
  guint mid = lo + (hi - lo) / 2;
  if (g_array_index(trace->edges, Edge, mid).time <= time) lo = mid + 1;
  else hi = mid;
 }
 return lo;
}

static void update(Viewer *viewer) {
 Signal *signal = selected(viewer);
 if (!signal) return;
 guint at = upper_bound(signal->trace, viewer->cursor);
 const gchar *value = at ? g_array_index(signal->trace->edges, Edge, at - 1).value : "unknown";
 gchar *label = g_strdup_printf("Tick %" G_GUINT64_FORMAT " (%s)   Value: %.128s%s",
     viewer->cursor, viewer->capture->scale ? viewer->capture->scale : "no timescale",
     value, strlen(value) > 128 ? "…" : "");
 gtk_label_set_text(GTK_LABEL(viewer->value), label);
 g_free(label);
 gtk_widget_queue_draw(viewer->canvas);
}

static gboolean draw_wave(GtkWidget *widget, cairo_t *cr, gpointer data) {
 Viewer *viewer = data;
 Signal *signal = selected(viewer);
 if (!signal) return FALSE;
 Trace *trace = signal->trace;
 double width = gtk_widget_get_allocated_width(widget);
 double height = gtk_widget_get_allocated_height(widget);
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
    gchar *label = g_strndup(bits, MIN((gsize)12, strlen(bits)));
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
 if (viewer->cursor >= viewer->start && viewer->cursor <= stop) {
  double x = (double)(viewer->cursor - viewer->start) / viewer->span * width;
  cairo_set_source_rgb(cr, .85, .28, .25);
  cairo_move_to(cr, x, 0);
  cairo_line_to(cr, x, height);
  cairo_stroke(cr);
 }
 return FALSE;
}

static gboolean click_wave(GtkWidget *widget, GdkEventButton *event, gpointer data) {
 Viewer *viewer = data;
 viewer->cursor = MIN(viewer->capture->end, viewer->start +
     (guint64)(CLAMP(event->x / MAX(1, gtk_widget_get_allocated_width(widget)), 0., 1.) * viewer->span));
 update(viewer);
 return TRUE;
}

static void select_wave(GtkComboBox *box, gpointer data) {
 (void)box;
 Viewer *viewer = data;
 Signal *signal = selected(viewer);
 if (signal) gtk_entry_set_text(GTK_ENTRY(viewer->source), signal->name);
 update(viewer);
}

static void wave_action(GtkButton *button, gpointer data) {
 Viewer *viewer = data;
 Signal *signal = selected(viewer);
 if (!signal) return;
 gint action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
 Trace *trace = signal->trace;
 guint next = upper_bound(trace, viewer->cursor);
 if (action == 1 && next < trace->edges->len)
  viewer->cursor = g_array_index(trace->edges, Edge, next).time;
 if (action == -1 && next) {
  guint index = next - 1;
  if (index && g_array_index(trace->edges, Edge, index).time == viewer->cursor) index--;
  viewer->cursor = g_array_index(trace->edges, Edge, index).time;
 }
 guint64 total = MAX(G_GUINT64_CONSTANT(1), viewer->capture->end);
 if (action == 2) viewer->span = MAX(G_GUINT64_CONSTANT(1), viewer->span / 2);
 if (action == 3) viewer->span = MIN(total, viewer->span * 2);
 viewer->start = MIN(total - viewer->span, viewer->cursor > viewer->span / 2 ?
                    viewer->cursor - viewer->span / 2 : 0);
 update(viewer);
}

static void source_clicked(GtkButton *button, gpointer data) {
 (void)button;
 Viewer *viewer = data;
 if (viewer->navigate) viewer->navigate(gtk_entry_get_text(GTK_ENTRY(viewer->source)));
}

static void viewer_free(gpointer data) {
 Viewer *viewer = data;
 capture_free(viewer->capture);
 g_free(viewer);
}

static void show_capture(GtkWindow *parent, Capture *capture, InsightWaveSource navigate) {
 Viewer *viewer = g_new0(Viewer, 1);
 viewer->capture = capture;
 viewer->span = MAX(G_GUINT64_CONSTANT(1), capture->end);
 viewer->navigate = navigate;
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
 for (guint i = 0; i < capture->signals->len; i++)
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(viewer->selector), ((Signal *)capture->signals->pdata[i])->name);
 gtk_box_pack_start(GTK_BOX(box), viewer->selector, FALSE, FALSE, 0);
 viewer->canvas = gtk_drawing_area_new();
 gtk_widget_add_events(viewer->canvas, GDK_BUTTON_PRESS_MASK);
 gtk_box_pack_start(GTK_BOX(box), viewer->canvas, TRUE, TRUE, 0);
 viewer->value = gtk_label_new(NULL);
 gtk_label_set_ellipsize(GTK_LABEL(viewer->value), PANGO_ELLIPSIZE_END);
 gtk_box_pack_start(GTK_BOX(box), viewer->value, FALSE, FALSE, 0);
 GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
 gtk_box_pack_start(GTK_BOX(box), controls, FALSE, FALSE, 0);
 const gchar *labels[] = {"Previous transition", "Next transition", "Zoom in", "Zoom out"};
 const gint actions[] = {-1, 1, 2, 3};
 for (guint i = 0; i < G_N_ELEMENTS(labels); i++) {
  GtkWidget *button = gtk_button_new_with_label(labels[i]);
  g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(actions[i]));
  g_signal_connect(button, "clicked", G_CALLBACK(wave_action), viewer);
  gtk_box_pack_start(GTK_BOX(controls), button, FALSE, FALSE, 0);
 }
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
 gtk_combo_box_set_active(GTK_COMBO_BOX(viewer->selector), 0);
 gtk_widget_show_all(viewer->window);
}

typedef struct { GWeakRef parent; gchar *path; InsightWaveSource navigate; } Load;
static void load_free(gpointer data) {
 Load *load = data;
 g_weak_ref_clear(&load->parent);
 g_free(load->path);
 g_free(load);
}
static void load_capture(GTask *task, gpointer object, gpointer data, GCancellable *cancel) {
 (void)object; (void)cancel;
 Load *load = data;
 GError *error = NULL;
 GFile *file = g_file_new_for_path(load->path);
 GFileInputStream *stream = g_file_read(file, cancel, &error);
 g_object_unref(file);
 Capture *capture = NULL;
 if (stream) {
  GByteArray *bytes = g_byte_array_new();
  guint8 buffer[16384];
  gssize size;
  while ((size = g_input_stream_read(G_INPUT_STREAM(stream), buffer, sizeof buffer, cancel, &error)) > 0) {
   if (bytes->len + (gsize)size > VCD_BYTES) break;
   g_byte_array_append(bytes, buffer, (guint)size);
  }
  if (size == 0 && bytes->len && !memchr(bytes->data, 0, bytes->len))
   capture = parse_capture((const gchar *)bytes->data, bytes->len, &error);
  else if (!error)
   g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "VCD must be text, 1 byte to 32 MiB");
  g_byte_array_unref(bytes);
  g_object_unref(stream);
 }
 if (capture) g_task_return_pointer(task, capture, capture_free);
 else g_task_return_error(task, error);
}
static void load_done(GObject *object, GAsyncResult *result, gpointer data) {
 (void)object; (void)data;
 GTask *task = G_TASK(result);
 Load *load = g_task_get_task_data(task);
 GtkWindow *parent = g_weak_ref_get(&load->parent);
 GError *error = NULL;
 Capture *capture = g_task_propagate_pointer(task, &error);
 if (parent && capture) show_capture(parent, capture, load->navigate);
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

void insight_waveforms_open(GtkWindow *parent, InsightWaveSource navigate) {
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
 load->navigate = navigate;
 GTask *task = g_task_new(NULL, NULL, load_done, NULL);
 g_task_set_task_data(task, load, load_free);
 g_task_run_in_thread(task, load_capture);
 g_object_unref(task);
}
