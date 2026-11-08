/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Shared zoom, rectangular selection and capture for simulation views.
 * RU: Масштаб, выделение области и запись для окон симуляции.
 */

typedef gboolean (*CaptureDraw)(GtkWidget *, cairo_t *, gpointer);

typedef struct CaptureView CaptureView;
struct CaptureView
{
    GtkWidget *canvas, *record, *step, *manual, *point_label;
    CaptureDraw draw;
    gpointer data;
    gdouble x, y, width, height;
    gdouble start_x, start_y, end_x, end_y;
    gboolean selecting;
    guint pan_button;
    gboolean pinching, pick_pending;
    gdouble pinch_scale, pick_x, pick_y;
    void (*inspect)(CaptureView *view, gdouble x, gdouble y);
    FILE *video;
    guint timer, video_width, video_height;
    guchar *planes;
};

/* EN: Stop view's recording and report whether its output closed successfully.
 * RU: Останавливает запись view и возвращает результат закрытия файла. */
static gboolean capture_stop(CaptureView *view)
{
    if (view->timer)
    {
        g_source_remove(view->timer);
        view->timer = 0;
    }
    gboolean ok = !view->video || fclose(view->video) == 0;
    view->video = NULL;
    g_clear_pointer(&view->planes, g_free);
    return ok;
}

/* EN: Release capture state data when the canvas is finalized.
 * RU: Освобождает состояние data после уничтожения области рисования. */
static void capture_view_free(gpointer data)
{
    CaptureView *view = data;
    capture_stop(view);
    g_free(view);
}

/* EN: Paint view at output width and height using its current zoom in cr.
 * RU: Рисует view в cr с заданными width, height и текущим масштабом. */
static void capture_render(CaptureView *view, cairo_t *cr, gint width, gint height)
{
    gint source_width = MAX(1, gtk_widget_get_allocated_width(view->canvas));
    gint source_height = MAX(1, gtk_widget_get_allocated_height(view->canvas));
    cairo_save(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    gdouble scale = MIN((gdouble)width / source_width, (gdouble)height / source_height);
    cairo_translate(cr, (width - source_width * scale) / 2,
                    (height - source_height * scale) / 2);
    cairo_rectangle(cr, 0, 0, source_width * scale, source_height * scale);
    cairo_clip(cr);
    cairo_scale(cr, scale / view->width, scale / view->height);
    cairo_translate(cr, -view->x * source_width, -view->y * source_height);
    view->draw(view->canvas, cr, view->data);
    cairo_restore(cr);
}

/* EN: Draw the view and the optional selection rectangle in cr for canvas.
 * RU: Рисует вид и рамку выделения view в cr для canvas. */
static gboolean capture_draw(GtkWidget *canvas, cairo_t *cr, CaptureView *view)
{
    capture_render(view, cr, gtk_widget_get_allocated_width(canvas),
                   gtk_widget_get_allocated_height(canvas));
    if (view->selecting)
    {
        cairo_set_source_rgb(cr, .15, .45, .85);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, view->start_x, view->start_y,
                        view->end_x - view->start_x, view->end_y - view->start_y);
        cairo_stroke(cr);
    }
    return FALSE;
}

/* EN: Pan view in canvas by the pointer displacement since the last event.
 * RU: Сдвигает view в canvas на смещение указателя с предыдущего события. */
static void capture_pan(GtkWidget *canvas, CaptureView *view, gdouble x, gdouble y)
{
    gint width = MAX(1, gtk_widget_get_allocated_width(canvas));
    gint height = MAX(1, gtk_widget_get_allocated_height(canvas));
    view->x = CLAMP(view->x - (x - view->end_x) * view->width / width, 0, 1 - view->width);
    view->y = CLAMP(view->y - (y - view->end_y) * view->height / height, 0, 1 - view->height);
    view->end_x = x;
    view->end_y = y;
    gtk_widget_queue_draw(canvas);
}

/* EN: Request inspection at screen point x,y in view.
 * RU: Запрашивает данные точки x,y на экране view. */
static void capture_inspect(CaptureView *view, gdouble x, gdouble y)
{
    if (view->inspect)
        view->inspect(view, x, y);
    else
    {
        view->pick_x = x;
        view->pick_y = y;
        view->pick_pending = TRUE;
    }
}

/* EN: Compare a projected scope sample at x,y with the pending click in view.
 * RU: Сравнивает проекцию отсчёта x,y с ожидающей обработки точкой view. */
static gdouble capture_point_distance(CaptureView *view, gdouble x, gdouble y)
{
    gint width = gtk_widget_get_allocated_width(view->canvas);
    gint height = gtk_widget_get_allocated_height(view->canvas);
    gdouble dx = (x - view->x * width) / view->width - view->pick_x;
    gdouble dy = (y - view->y * height) / view->height - view->pick_y;
    return dx * dx + dy * dy;
}

/* EN: Complete view's pending inspection with axes entries from values, or no sample.
 * RU: Завершает выбор view и показывает axes значений из values или отсутствие отсчёта. */
static void capture_scope_point(CaptureView *view, const guint64 *values, guint axes)
{
    view->pick_pending = FALSE;
    if (!values)
    {
        gtk_label_set_text(GTK_LABEL(view->point_label), insight_tr("No known sample near this point."));
        return;
    }
    GString *text = g_string_new(NULL);
    for (guint i = 0; i < axes; ++i)
        g_string_append_printf(text, "%s%c: %" G_GUINT64_FORMAT,
                               i ? "  ·  " : "", "XYZ"[i], values[i]);
    gtk_label_set_text(GTK_LABEL(view->point_label), text->str);
    g_string_free(text, TRUE);
}

/* EN: Handle event in canvas, updating view's rectangular zoom or pan gesture.
 * RU: Обрабатывает event в canvas, меняя выделение или положение view. */
static gboolean capture_button(GtkWidget *canvas, GdkEventButton *event, CaptureView *view)
{
    if (view->pinching)
        return TRUE;
    if (event->type == GDK_BUTTON_RELEASE && event->button == view->pan_button)
    {
        capture_pan(canvas, view, event->x, event->y);
        view->pan_button = 0;
        return TRUE;
    }
    if (event->type == GDK_BUTTON_PRESS && !view->selecting && !view->pan_button &&
        (event->button == 2 || (event->button == 1 && (event->state & GDK_SHIFT_MASK))))
    {
        view->pan_button = event->button;
        view->end_x = event->x;
        view->end_y = event->y;
        return TRUE;
    }
    if (event->button != 1 || view->pan_button)
        return FALSE;
    if (event->type == GDK_BUTTON_PRESS)
    {
        view->selecting = TRUE;
        view->start_x = view->end_x = event->x;
        view->start_y = view->end_y = event->y;
    }
    else if (view->selecting)
    {
        gdouble width = gtk_widget_get_allocated_width(canvas);
        gdouble height = gtk_widget_get_allocated_height(canvas);
        gdouble left = CLAMP(MIN(view->start_x, event->x), 0, width);
        gdouble top = CLAMP(MIN(view->start_y, event->y), 0, height);
        gdouble right = CLAMP(MAX(view->start_x, event->x), 0, width);
        gdouble bottom = CLAMP(MAX(view->start_y, event->y), 0, height);
        if (right - left >= 5 && bottom - top >= 5)
        {
            gdouble factor = MAX((right - left) / width, (bottom - top) / height);
            gdouble new_width = MAX(view->width * factor, 1.0 / 256);
            gdouble new_height = MAX(view->height * factor, 1.0 / 256);
            view->x = CLAMP(view->x + view->width * (left + right) / (2 * width) - new_width / 2,
                            0, 1 - new_width);
            view->y = CLAMP(view->y + view->height * (top + bottom) / (2 * height) - new_height / 2,
                            0, 1 - new_height);
            view->width = new_width;
            view->height = new_height;
        }
        else if (right - left < 5 && bottom - top < 5)
            capture_inspect(view, event->x, event->y);
        view->selecting = FALSE;
    }
    gtk_widget_queue_draw(canvas);
    return TRUE;
}

/* EN: Apply motion event to the selection or pan in view's canvas.
 * RU: Применяет движение event к выделению или сдвигу view в canvas. */
static gboolean capture_motion(GtkWidget *canvas, GdkEventMotion *event, CaptureView *view)
{
    if (view->pan_button)
    {
        capture_pan(canvas, view, event->x, event->y);
        return TRUE;
    }
    if (!view->selecting)
        return FALSE;
    view->end_x = event->x;
    view->end_y = event->y;
    gtk_widget_queue_draw(canvas);
    return TRUE;
}

/* EN: Zoom view by factor, keeping normalized anchor x,y under the pointer.
 * RU: Меняет масштаб view в factor раз, удерживая точку x,y под указателем. */
static void capture_zoom_at(CaptureView *view, gdouble factor, gdouble x, gdouble y)
{
    gdouble width = CLAMP(view->width * factor, 1.0 / 256, 1);
    gdouble height = CLAMP(view->height * factor, 1.0 / 256, 1);
    view->x = CLAMP(view->x + (view->width - width) * x, 0, 1 - width);
    view->y = CLAMP(view->y + (view->height - height) * y, 0, 1 - height);
    view->width = width;
    view->height = height;
    gtk_widget_queue_draw(view->canvas);
}

/* EN: Apply the zoom associated with button, centered on view.
 * RU: Применяет масштаб кнопки button вокруг центра view. */
static void capture_zoom(GtkButton *button, CaptureView *view)
{
    gint direction = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "direction"));
    capture_zoom_at(view, direction > 0 ? .8 : direction < 0 ? 1.25 : 256, .5, .5);
}

/* EN: Zoom view under the pointer from a vertical scroll event on canvas.
 * RU: Меняет масштаб view под указателем по вертикальной прокрутке event в canvas. */
static gboolean capture_scroll(GtkWidget *canvas, GdkEventScroll *event, CaptureView *view)
{
    if (view->selecting || view->pan_button)
        return FALSE;

    gdouble delta_x, delta_y;
    if (event->direction == GDK_SCROLL_UP)
        delta_y = -1;
    else if (event->direction == GDK_SCROLL_DOWN)
        delta_y = 1;
    else if (!gdk_event_get_scroll_deltas((GdkEvent *)event, &delta_x, &delta_y) || delta_y == 0)
        return FALSE;

    gdouble x = CLAMP(event->x / MAX(1, gtk_widget_get_allocated_width(canvas)), 0, 1);
    gdouble y = CLAMP(event->y / MAX(1, gtk_widget_get_allocated_height(canvas)), 0, 1);
    gdouble step = MIN(1.0, ABS(delta_y)) * .2;
    capture_zoom_at(view, delta_y < 0 ? 1 - step : 1 / (1 - step), x, y);
    return TRUE;
}

#include "capture-pinch.inc"

/* EN: Render view into a new RGB24 Cairo surface of width by height.
 * RU: Рисует view на новой поверхности RGB24 размером width на height. */
static cairo_surface_t *capture_surface(CaptureView *view, gint width, gint height)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    cairo_t *cr = cairo_create(surface);
    capture_render(view, cr, width, height);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    return surface;
}

/* EN: Show capture failure message with widget's window as parent.
 * RU: Показывает ошибку записи message в окне виджета widget. */
static void capture_error(GtkWidget *widget, const gchar *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show(dialog);
}

/* EN: Refresh the recording controls for view after a start or stop.
 * RU: Обновляет кнопки записи view после запуска или остановки. */
static void capture_record_state(CaptureView *view)
{
    gtk_button_set_label(GTK_BUTTON(view->record),
                         insight_tr(view->video ? "Stop recording" : "Record video…"));
    gtk_widget_set_sensitive(view->manual, !view->video);
    gtk_widget_set_sensitive(view->step, view->video && !view->timer);
}

/* EN: Write the RGB24 surface to view's Y4M stream as one planar frame.
 * RU: Записывает RGB24 surface в поток Y4M из view одним планарным кадром. */
static gboolean capture_write_frame(CaptureView *view, cairo_surface_t *surface)
{
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
        return FALSE;

    guint width = view->video_width, height = view->video_height;
    gsize pixels = (gsize)width * height;
    const guchar *rgb = cairo_image_surface_get_data(surface);
    gint stride = cairo_image_surface_get_stride(surface);
    for (guint y = 0; y < height; ++y)
    {
        const guint32 *row = (const guint32 *)(rgb + (gsize)y * stride);
        for (guint x = 0; x < width; ++x)
        {
            gint r = (row[x] >> 16) & 255, g = (row[x] >> 8) & 255, b = row[x] & 255;
            gsize i = (gsize)y * width + x;
            view->planes[i] = 16 + (66 * r + 129 * g + 25 * b + 128) / 256;
            view->planes[i + pixels] = (32768 - 38 * r - 74 * g + 112 * b + 128) / 256;
            view->planes[i + 2 * pixels] = (32768 + 112 * r - 94 * g - 18 * b + 128) / 256;
        }
    }
    return fputs("FRAME\n", view->video) != EOF &&
           fwrite(view->planes, 1, 3 * pixels, view->video) == 3 * pixels;
}

/* EN: Render and append the next displayed frame from data to the video.
 * RU: Рисует и добавляет в видео текущий кадр из data. */
static gboolean capture_video_frame(gpointer data)
{
    CaptureView *view = data;
    cairo_surface_t *surface = capture_surface(view, view->video_width, view->video_height);
    gboolean ok = capture_write_frame(view, surface);
    cairo_surface_destroy(surface);
    if (!ok)
    {
        view->timer = 0;
        capture_stop(view);
        capture_record_state(view);
        capture_error(view->canvas, insight_tr("Could not write the video frame."));
    }
    return ok ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

/* EN: Append one frame to view's manual recording when button is clicked.
 * RU: По кнопке button добавляет один кадр к ручной записи view. */
static void capture_step(GtkButton *button, CaptureView *view)
{
    (void)button;
    if (view->video && !view->timer)
        capture_video_frame(view);
}

/* EN: Choose an output file for view from button; save PNG or toggle video.
 * RU: Выбирает файл для view по кнопке button, сохраняет PNG или переключает запись. */
static void capture_output(GtkButton *button, CaptureView *view)
{
    gboolean video = GTK_WIDGET(button) == view->record;
    if (video && view->video)
    {
        gboolean ok = capture_stop(view);
        capture_record_state(view);
        if (!ok)
            capture_error(view->canvas, insight_tr("Could not close the video file."));
        return;
    }

    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(view->canvas));
    GtkWidget *chooser = gtk_file_chooser_dialog_new(
        video ? insight_tr("Record video") : insight_tr("Save visible area"), parent,
        GTK_FILE_CHOOSER_ACTION_SAVE, insight_tr("_Cancel"), GTK_RESPONSE_CANCEL,
        insight_tr("_Save"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), video ? "capture.y4m" : "view.png");
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
    gint width = MAX(1, gtk_widget_get_allocated_width(view->canvas));
    gint height = MAX(1, gtk_widget_get_allocated_height(view->canvas));
    cairo_surface_t *snapshot = video ? NULL : capture_surface(view, width, height);
    gboolean manual = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->manual));
    g_object_ref(view->canvas);
    gchar *path = gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT
                  ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser)) : NULL;
    gtk_widget_destroy(chooser);
    if (path && gtk_widget_get_mapped(view->canvas))
    {
        if (video)
        {
            gdouble scale = MIN(1.0, MIN(1280.0 / width, 720.0 / height));
            view->video_width = MAX(1, (guint)(width * scale));
            view->video_height = MAX(1, (guint)(height * scale));
            view->video = g_fopen(path, "wb");
            if (view->video && fprintf(view->video, "YUV4MPEG2 W%u H%u F10:1 Ip A1:1 C444\n",
                                        view->video_width, view->video_height) > 0)
            {
                view->planes = g_malloc((gsize)view->video_width * view->video_height * 3);
                if (!manual)
                    view->timer = g_timeout_add(100, capture_video_frame, view);
                capture_record_state(view);
            }
            else
            {
                capture_stop(view);
                capture_error(view->canvas, insight_tr("Could not start video recording."));
            }
        }
        else
        {
            GdkPixbuf *pixbuf = cairo_surface_status(snapshot) == CAIRO_STATUS_SUCCESS
                                ? gdk_pixbuf_get_from_surface(snapshot, 0, 0, width, height) : NULL;
            GError *error = NULL;
            if (!pixbuf || !gdk_pixbuf_save(pixbuf, path, "png", &error, NULL))
                capture_error(view->canvas, error ? error->message :
                              insight_tr("Could not save the visible area."));
            g_clear_error(&error);
            g_clear_object(&pixbuf);
        }
    }
    if (snapshot)
        cairo_surface_destroy(snapshot);
    g_free(path);
    g_object_unref(view->canvas);
}

/* EN: Stop recording when canvas is destroyed, while data is still alive.
 * RU: Останавливает запись при уничтожении canvas, пока data существует. */
static void capture_destroy(GtkWidget *canvas, gpointer data)
{
    (void)canvas;
    capture_stop(data);
}

/* EN: Install common controls in page for canvas, rendered by draw with data.
 * RU: Добавляет в page общие инструменты для canvas, рисуемого draw с data. */
static CaptureView *capture_view_new(GtkWidget *page, GtkWidget *canvas,
                                     CaptureDraw draw, gpointer data)
{
    CaptureView *view = g_new0(CaptureView, 1);
    view->canvas = canvas;
    view->draw = draw;
    view->data = data;
    view->width = view->height = 1;
    g_object_set_data_full(G_OBJECT(canvas), "capture-view", view, capture_view_free);
    gtk_widget_add_events(canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
                                 GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(canvas, "draw", G_CALLBACK(capture_draw), view);
    g_signal_connect(canvas, "button-press-event", G_CALLBACK(capture_button), view);
    g_signal_connect(canvas, "button-release-event", G_CALLBACK(capture_button), view);
    g_signal_connect(canvas, "motion-notify-event", G_CALLBACK(capture_motion), view);
    g_signal_connect(canvas, "scroll-event", G_CALLBACK(capture_scroll), view);
    g_signal_connect(canvas, "destroy", G_CALLBACK(capture_destroy), view);
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(page), bar, FALSE, FALSE, 0);
    const gchar *labels[] = {"−", "Fit", "+", "Save view…", "Record video…"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); ++i)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        gtk_box_pack_start(GTK_BOX(bar), button, FALSE, FALSE, 0);
        if (i < 3)
        {
            g_object_set_data(G_OBJECT(button), "direction", GINT_TO_POINTER((gint)i - 1));
            g_signal_connect(button, "clicked", G_CALLBACK(capture_zoom), view);
        }
        else
        {
            if (i == 4)
                view->record = button;
            g_signal_connect(button, "clicked", G_CALLBACK(capture_output), view);
        }
    }
    GtkWidget *recording = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(page), recording, FALSE, FALSE, 0);
    view->manual = gtk_check_button_new_with_label(insight_tr("Manual frames"));
    gtk_box_pack_start(GTK_BOX(recording), view->manual, FALSE, FALSE, 0);
    view->step = gtk_button_new_with_label(insight_tr("Add frame"));
    gtk_box_pack_start(GTK_BOX(recording), view->step, FALSE, FALSE, 0);
    g_signal_connect(view->step, "clicked", G_CALLBACK(capture_step), view);
    capture_record_state(view);
    GtkGesture *pinch = gtk_gesture_zoom_new(canvas);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(pinch), GTK_PHASE_CAPTURE);
    g_signal_connect(pinch, "begin", G_CALLBACK(capture_pinch_begin), view);
    g_signal_connect(pinch, "scale-changed", G_CALLBACK(capture_pinch_scale), view);
    g_signal_connect(pinch, "end", G_CALLBACK(capture_pinch_end), view);
    g_signal_connect(pinch, "cancel", G_CALLBACK(capture_pinch_end), view);
    g_object_set_data_full(G_OBJECT(canvas), "capture-pinch", pinch, g_object_unref);
    view->point_label = gtk_label_new("");
    gtk_label_set_selectable(GTK_LABEL(view->point_label), TRUE);
    gtk_box_pack_start(GTK_BOX(page), view->point_label, FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(bar, insight_tr("Drag a rectangle or scroll to zoom. Middle-drag or Shift-drag to pan."));
    return view;
}
