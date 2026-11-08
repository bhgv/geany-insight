/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Waveform drawing, cursor selection and track activation.
 * RU: Отрисовка диаграммы, выбор курсора и дорожки.
 */

/* EN: Refresh viewer's cursor, displayed values, controls and waveform drawing.
 * RU: Обновляет курсор, значения, органы управления и диаграмму viewer. */
static void update(Viewer *viewer)
{
    Signal *signal = selected(viewer);
    if (!signal)
        return;
    guint at = upper_bound(signal->trace, viewer->cursor);
    gchar *value = format_bits(at ? g_array_index(signal->trace->edges, Edge, at - 1).value : "unknown",
                               viewer->hexadecimal);
    gchar *label = g_strdup_printf(
        "Tick %" G_GUINT64_FORMAT " (%s)   View: %" G_GUINT64_FORMAT "–%" G_GUINT64_FORMAT
        "   Value: %.128s%s",
        viewer->cursor, viewer->capture->scale ? viewer->capture->scale : "no timescale", viewer->start,
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

/* EN: Draw the visible part of trace for viewer into cr across width pixels.
 * RU: Рисует видимую часть trace для viewer в cr на ширине width пикселей. */
static void draw_trace(Viewer *viewer, Trace *trace, cairo_t *cr, double width)
{
    const double height = TRACK_HEIGHT;
    guint64 stop = viewer->start + viewer->span;
    guint next = upper_bound(trace, viewer->start);
    guint64 time = viewer->start;
    double previous = -1;
    while (time < stop)
    {
        const gchar *bits = next ? g_array_index(trace->edges, Edge, next - 1).value : "x";
        guint64 until =
            next < trace->edges->len ? MIN(stop, g_array_index(trace->edges, Edge, next).time) : stop;
        /* Condense transitions closer than one pixel, keeping the full time span. */
        guint64 pixel = MAX(G_GUINT64_CONSTANT(1), (guint64)(viewer->span / MAX(1., width)));
        gboolean dense = until < stop && until - time < pixel;
        if (dense)
            until = MIN(stop, time + pixel);
        double x = (double)(time - viewer->start) / viewer->span * width;
        double right = (double)(until - viewer->start) / viewer->span * width;
        gboolean unknown = strpbrk(bits, "xz") != NULL;
        cairo_set_source_rgb(cr, unknown ? 0.9 : 0.12, unknown ? 0.54 : 0.62, 0.75);
        if (dense)
        {
            cairo_rectangle(cr, x, height * .2, MAX(1, right - x), height * .6);
            cairo_fill(cr);
            previous = -1;
        }
        else if (trace->width > 1)
        {
            cairo_rectangle(cr, x, height * .26, MAX(1, right - x), height * .48);
            cairo_stroke(cr);
            if (right - x > 70)
            {
                gchar *label = format_bits(bits, viewer->hexadecimal);
                if (strlen(label) > 12)
                    label[12] = '\0';
                cairo_move_to(cr, x + 4, height * .55);
                cairo_show_text(cr, label);
                g_free(label);
            }
        }
        else
        {
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
    for (guint i = 0; i < G_N_ELEMENTS(viewer->marker); i++)
    {
        guint64 tick = viewer->marker[i];
        if (!viewer->marker_set[i] || tick < viewer->start || tick > stop)
            continue;
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
    if (viewer->cursor >= viewer->start && viewer->cursor <= stop)
    {
        double x = (double)(viewer->cursor - viewer->start) / viewer->span * width;
        cairo_set_source_rgb(cr, .85, .28, .25);
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, height);
        cairo_stroke(cr);
    }
}

/* EN: Draw the tracks of Viewer data in widget with Cairo context cr.
 * RU: Рисует дорожки Viewer из data в widget через контекст Cairo cr. */
static gboolean draw_wave(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    Viewer *viewer = data;
    gint active = viewer->active;
    double width = MAX(1, gtk_widget_get_allocated_width(widget) - TRACK_LABEL_WIDTH);
    guint count = track_count(viewer, active);
    for (guint lane = 0; lane < count; lane++)
    {
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

/* EN: Use event on widget to set the cursor and active track of Viewer data.
 * RU: По событию event в widget выбирает курсор и дорожку Viewer из data. */
static gboolean click_wave(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    Viewer *viewer = data;
    if (event->button != 1 || event->y < 0)
        return FALSE;
    gint active = viewer->active;
    guint lane = (guint)event->y / TRACK_HEIGHT;
    if (lane >= track_count(viewer, active))
        return FALSE;
    gint signal = track_signal(viewer, lane, active);
    if (signal != active)
    {
        activate_signal(viewer, signal);
        gchar id[16];
        g_snprintf(id, sizeof id, "%d", signal);
        viewer->rebuilding = TRUE;
        if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(viewer->selector), id))
            gtk_combo_box_set_active(GTK_COMBO_BOX(viewer->selector), -1);
        viewer->rebuilding = FALSE;
    }
    if (event->x >= TRACK_LABEL_WIDTH)
    {
        double width = MAX(1, gtk_widget_get_allocated_width(widget) - TRACK_LABEL_WIDTH);
        double fraction = CLAMP((event->x - TRACK_LABEL_WIDTH) / width, 0., 1.);
        viewer->cursor = MIN(viewer->capture->end, viewer->start + (guint64)(fraction * viewer->span));
    }
    update(viewer);
    return TRUE;
}

/* EN: Select the signal at index in viewer and refresh the display.
 * RU: Выбирает сигнал index в viewer и обновляет отображение. */
static void activate_signal(Viewer *viewer, gint index)
{
    viewer->active = index;
    Signal *signal = selected(viewer);
    if (signal)
        gtk_entry_set_text(GTK_ENTRY(viewer->source), signal->name);
    update(viewer);
}

/* EN: Select the signal indicated by box in Viewer data.
 * RU: Выбирает в Viewer из data сигнал, указанный в box. */
static void select_wave(GtkComboBox *box, gpointer data)
{
    Viewer *viewer = data;
    if (viewer->rebuilding)
        return;
    const gchar *id = gtk_combo_box_get_active_id(box);
    if (id)
        activate_signal(viewer, (gint)g_ascii_strtoll(id, NULL, 10));
}
