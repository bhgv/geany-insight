/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Waveform instruments, axis selection and plot rendering.
 * RU: Приборы диаграмм, выбор осей и отрисовка графиков.
 */

/* EN: Return viewer's selected trace for axis, or NULL for an unbound axis.
 * RU: Возвращает выбранную трассу оси axis в viewer либо NULL. */
static Trace *instrument_trace(Viewer *viewer, guint axis)
{
    gint index = gtk_combo_box_get_active(GTK_COMBO_BOX(viewer->instrument_signal[axis])) - 1;
    if (index < 0)
        return NULL;
    Signal *signal = viewer->capture->signals->pdata[index];
    return signal->trace;
}

/* EN: Return the value recorded in trace at tick, or NULL before the first edge.
 * RU: Возвращает значение trace в tick либо NULL до первого перехода. */
static const gchar *instrument_bits(const Trace *trace, guint64 tick)
{
    guint at = upper_bound(trace, tick);
    return at ? g_array_index(trace->edges, Edge, at - 1).value : NULL;
}

/* EN: Convert known bits into number. Return false for X/Z or more than 64 bits.
 * RU: Преобразует известные bits в number. Возвращает false для X/Z и ширины более 64 бит. */
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

/* EN: Refresh viewer's instrument values at the cursor and queue a plot redraw.
 * RU: Обновляет показания приборов viewer на курсоре и запрашивает перерисовку. */
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
        const gchar *lamp = !trace || trace->width != 1 ? " "
                            : !numeric                  ? "<span foreground='#a56818'>◉</span>"
                            : number                    ? "<span foreground='#228447'>●</span>"
                                                        : "○";
        gtk_label_set_markup(GTK_LABEL(viewer->instrument_lamp[i]), lamp);
        gchar *value = numeric ? g_strdup_printf("%" G_GUINT64_FORMAT "  (0x%" G_GINT64_MODIFIER "x)",
                                                 number, number)
                               : g_strdup_printf("%.100s", bits ? bits : "—");
        gtk_label_set_text(GTK_LABEL(viewer->instrument_value[i]), value);
        g_free(value);
    }
    gtk_widget_queue_draw(viewer->instrument_plot);
}

#define INSTRUMENT_POINTS 2048u

typedef struct
{
    guint64 values[3];
    gboolean valid;
} InstrumentPoint;

#define LIVE_SCOPE_POINTS 512u

typedef struct
{
    guint64 time;
    InstrumentPoint point;
} LiveScopeSample;

typedef struct
{
    GtkWidget *drawing, *status;
    guint axes, count, next;
    gboolean dirty;
    LiveScopeSample samples[LIVE_SCOPE_POINTS];
} LiveScope;

/* EN: Sample axes entries in traces at tick and mark the point invalid on X/Z.
 * RU: Считывает axes трасс traces в tick и помечает точку недействительной при X/Z. */
static InstrumentPoint instrument_point(Trace *const *traces, guint axes, guint64 tick)
{
    InstrumentPoint point = {{0}, TRUE};
    for (guint i = 0; i < axes; i++)
        if (!instrument_number(instrument_bits(traces[i], tick), &point.values[i]))
            point.valid = FALSE;
    return point;
}

/* EN: Project point using axis bounds low and high. Write the normalized axes coordinates to x and y.
 * RU: Проецирует point по границам low и high. Записывает нормированные координаты axes осей в x и y. */
static void instrument_project(const InstrumentPoint *point, const guint64 *low, const guint64 *high,
                               guint axes, double *x, double *y)
{
    double v[3] = {0};
    for (guint i = 0; i < axes; i++)
        v[i] = high[i] == low[i] ? 0.5 : (double)(point->values[i] - low[i]) / (double)(high[i] - low[i]);
    *x = axes == 3 ? 0.72 * v[0] + 0.28 * v[2] : v[0];
    *y = axes == 3 ? 0.72 * v[1] + 0.28 * v[2] : v[1];
}

/* EN: Draw the instrument plot for Viewer data on widget with Cairo context cr.
 * RU: Рисует график Viewer из data в widget через контекст Cairo cr. */
static gboolean instruments_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    Viewer *viewer = data;
    Trace *traces[] = {instrument_trace(viewer, 0), instrument_trace(viewer, 1),
                       instrument_trace(viewer, 2)};
    if (!traces[0] || !traces[1])
    {
        gtk_label_set_text(GTK_LABEL(viewer->instrument_range),
                           "Choose X and Y for an XY plot. Add Z for a projection.");
        CaptureView *view = g_object_get_data(G_OBJECT(widget), "capture-view");
        if (view->pick_pending)
            capture_scope_point(view, NULL, 0);
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
    gtk_style_context_get_color(gtk_widget_get_style_context(widget), gtk_widget_get_state_flags(widget),
                                &color);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.35);
    cairo_rectangle(cr, 16, 16, width, height);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, .18, .52, .82);
    gboolean previous = FALSE;
    CaptureView *view = g_object_get_data(G_OBJECT(widget), "capture-view");
    const guint64 *selected = NULL;
    gdouble nearest = 144;
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
        if (view->pick_pending)
        {
            gdouble distance = capture_point_distance(view, x, y);
            if (distance <= nearest)
            {
                nearest = distance;
                selected = points[i].values;
            }
        }
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
    if (view->pick_pending)
        capture_scope_point(view, selected, axes);
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
            g_string_append_printf(summary, "   %s: %" G_GUINT64_FORMAT "–%" G_GUINT64_FORMAT, axis[i],
                                   low[i], high[i]);
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

/* EN: Update the axis choices of Viewer data after combo changes.
 * RU: Обновляет выбор осей Viewer из data после изменения combo. */
static void instruments_changed(GtkComboBox *combo, gpointer data)
{
    Viewer *viewer = data;
    guint axis = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(combo), "instrument-axis"));
    viewer->instrument_index[axis] = gtk_combo_box_get_active(combo);
    instruments_update(viewer);
}

/* EN: Clear the instrument references in Viewer data when widget closes.
 * RU: Очищает ссылки на приборы в Viewer из data при закрытии widget. */
static void instruments_closed(GtkWidget *widget, gpointer data)
{
    Viewer *viewer = data;
    (void)widget;
    viewer->instruments = NULL;
}

/* EN: Open the tool from button for the current view or simulation in data.
 * RU: Открывает инструмент по button для текущего просмотра или симуляции из data. */
static void instruments_show(GtkButton *button, gpointer data)
{
    (void)button;
    Viewer *viewer = data;
    if (viewer->instruments)
    {
        gtk_window_present(GTK_WINDOW(viewer->instruments));
        return;
    }
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons("Signal Instruments", GTK_WINDOW(viewer->window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
    simulation_dialog_tools(
        dialog,
        "Choose signals from the capture and inspect their values at the waveform cursor. "
        "Choose X and Y for an XY plot, or add Z for the fixed projection.",
        "Выберите сигналы записи и смотрите значения у курсора. Выберите X и Y для XY или "
        "добавьте Z для фиксированной проекции.");
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
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), selected_index >= 0 ? selected_index
                                                       : i == 0            ? viewer->active + 1
                                                                           : 0);
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
    capture_view_new(box, viewer->instrument_plot, instruments_draw, viewer);
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
        g_signal_connect(viewer->instrument_signal[i], "changed", G_CALLBACK(instruments_changed),
                         viewer);
    instruments_update(viewer);
    gtk_widget_show_all(dialog);
}
