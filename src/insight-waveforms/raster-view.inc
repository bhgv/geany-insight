/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Load complete simulated display frames and present them in GTK.
 * RU: Загружает полные кадры симулируемых дисплеев и показывает их в GTK.
 */

typedef struct
{
    guint id, mode, width, height;
    guint32 sequence;
    GtkWidget *image, *status, *canvas;
    GdkPixbuf *final_frame;
} RasterDisplay;

/* EN: Release the display descriptor and its final frame from data.
 * RU: Освобождает описание дисплея и его последний кадр из data. */
static void simulation_raster_free(gpointer data)
{
    RasterDisplay *display = data;
    g_clear_object(&display->final_frame);
    g_free(display);
}

/* EN: Collect display descriptors from the generated harness of run.
 * RU: Собирает описания дисплеев из тестбенча сеанса run. */
static void simulation_rasters_prepare(WaveSimulation *run)
{
    if (!run->port_harness)
        return;
    const gchar *cursor = run->port_harness;
    const gchar marker[] = "// GEANY-RASTER ";
    while ((cursor = strstr(cursor, marker)))
    {
        cursor += sizeof marker - 1;
        RasterDisplay display = {0};
        if (sscanf(cursor, "%u %u %u %u", &display.id, &display.mode,
                   &display.width, &display.height) != 4 ||
            display.id >= 128 || display.mode > 6 || !display.width ||
            display.width > 3840 || !display.height || display.height > 2160)
            continue;
        if (!run->rasters)
            run->rasters = g_ptr_array_new_with_free_func(simulation_raster_free);
        RasterDisplay *copy = g_new(RasterDisplay, 1);
        *copy = display;
        g_ptr_array_add(run->rasters, copy);
        if (run->rasters->len == 4)
            break;
    }
}

/* EN: Read the latest snapshot for display from directory newer than sequence.
 * Return a new pixbuf, updating sequence only when a complete frame was read.
 * RU: Читает последний кадр display из directory новее sequence.
 * Возвращает новый pixbuf и обновляет sequence только для полного кадра. */
static GdkPixbuf *simulation_raster_read(const RasterDisplay *display, const gchar *directory,
                                        guint32 *sequence)
{
    GdkPixbuf *result = NULL;
    gsize size = (gsize)display->width * display->height * 3;
    for (guint slot = 0; slot < 2; ++slot)
    {
        gchar *name = g_strdup_printf("geany-raster-%u-%u.rgb", display->id, slot);
        gchar *path = g_build_filename(directory, name, NULL);
        FILE *file = g_fopen(path, "rb");
        g_free(path);
        g_free(name);
        if (!file)
            continue;

        guchar header[4];
        if (fread(header, 1, sizeof header, file) == sizeof header)
        {
            guint32 number = (guint32)header[0] | ((guint32)header[1] << 8) |
                             ((guint32)header[2] << 16) | ((guint32)header[3] << 24);
            if (number > *sequence)
            {
                guchar *pixels = g_malloc(size);
                if (fread(pixels, 1, size, file) == size && fgetc(file) == EOF && !ferror(file))
                {
                    GBytes *bytes = g_bytes_new_take(pixels, size);
                    g_clear_object(&result);
                    result = gdk_pixbuf_new_from_bytes(bytes, GDK_COLORSPACE_RGB, FALSE, 8,
                        display->width, display->height, display->width * 3);
                    g_bytes_unref(bytes);
                    *sequence = number;
                }
                else
                    g_free(pixels);
            }
        }
        fclose(file);
    }
    return result;
}

/* EN: Refresh live display pages of run from its current simulator directory.
 * RU: Обновляет вкладки дисплеев run из текущего каталога симулятора. */
static void simulation_rasters_refresh(WaveSimulation *run)
{
    if (!run->rasters)
        return;
    g_mutex_lock(&run->lock);
    gchar *directory = g_strdup(run->live_directory);
    g_mutex_unlock(&run->lock);
    if (!directory)
        return;

    for (guint i = 0; i < run->rasters->len; ++i)
    {
        RasterDisplay *display = g_ptr_array_index(run->rasters, i);
        if (!display->image)
            continue;
        GdkPixbuf *frame = simulation_raster_read(display, directory, &display->sequence);
        if (frame)
        {
            gtk_image_set_from_pixbuf(GTK_IMAGE(display->image), frame);
            gtk_widget_queue_draw(display->canvas);
            gchar *status = g_strdup_printf("%u × %u · %s %u", display->width, display->height,
                                            insight_tr("Frame"), display->sequence);
            gtk_label_set_text(GTK_LABEL(display->status), status);
            g_free(status);
            g_object_unref(frame);
        }
    }
    g_free(directory);
}

/* EN: Preserve run's last complete frames before directory is removed.
 * Called by the worker after the simulator exits.
 * RU: Сохраняет последние полные кадры run до удаления directory.
 * Вызывается в рабочем потоке после выхода симулятора. */
static void simulation_rasters_finish(WaveSimulation *run, const gchar *directory)
{
    for (guint i = 0; run->rasters && i < run->rasters->len; ++i)
    {
        RasterDisplay *display = g_ptr_array_index(run->rasters, i);
        guint32 sequence = 0;
        display->final_frame = simulation_raster_read(display, directory, &sequence);
    }
}

/* EN: Calculate the fitted frame's position and scale in canvas.
 * RU: Вычисляет положение и масштаб кадра frame, вписанного в canvas. */
static gdouble simulation_raster_fit(GtkWidget *canvas, GdkPixbuf *frame,
                                     gdouble *left, gdouble *top)
{
    gint width = gtk_widget_get_allocated_width(canvas);
    gint height = gtk_widget_get_allocated_height(canvas);
    gint frame_width = gdk_pixbuf_get_width(frame);
    gint frame_height = gdk_pixbuf_get_height(frame);
    gdouble scale = MIN((gdouble)width / frame_width, (gdouble)height / frame_height);
    *left = (width - frame_width * scale) / 2;
    *top = (height - frame_height * scale) / 2;
    return scale;
}

/* EN: Paint the fitted pixbuf held in data on canvas using cr.
 * RU: Рисует вписанный в canvas кадр из data через контекст cr. */
static gboolean simulation_raster_draw(GtkWidget *canvas, cairo_t *cr, gpointer data)
{
    GtkImage *image = data;
    if (gtk_image_get_storage_type(image) != GTK_IMAGE_PIXBUF)
        return FALSE;
    GdkPixbuf *frame = gtk_image_get_pixbuf(image);
    gdouble left, top;
    gdouble scale = simulation_raster_fit(canvas, frame, &left, &top);
    cairo_translate(cr, left, top);
    cairo_scale(cr, scale, scale);
    gdk_cairo_set_source_pixbuf(cr, frame, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_rectangle(cr, 0, 0, gdk_pixbuf_get_width(frame), gdk_pixbuf_get_height(frame));
    cairo_fill(cr);
    return FALSE;
}

/* EN: Show the source pixel beneath x,y in tooltip. view owns the zoom.
 * Keyboard queries on canvas have no pointer coordinate.
 * RU: Показывает исходный пиксель под x,y в tooltip с учётом масштаба view.
 * Запрос с клавиатуры в canvas не задаёт координаты указателя. */
static gboolean simulation_raster_pixel_tooltip(GtkWidget *canvas, gint x, gint y,
                                                 gboolean keyboard, GtkTooltip *tooltip,
                                                 CaptureView *view)
{
    GtkImage *image = view->data;
    if (keyboard || view->selecting || view->pan_button || gtk_image_get_storage_type(image) != GTK_IMAGE_PIXBUF)
        return FALSE;

    GdkPixbuf *frame = gtk_image_get_pixbuf(image);
    gdouble left, top;
    gdouble scale = simulation_raster_fit(canvas, frame, &left, &top);
    gdouble source_x = (view->x * gtk_widget_get_allocated_width(canvas) + x * view->width - left) / scale;
    gdouble source_y = (view->y * gtk_widget_get_allocated_height(canvas) + y * view->height - top) / scale;
    if (source_x < 0 || source_y < 0 ||
        source_x >= gdk_pixbuf_get_width(frame) || source_y >= gdk_pixbuf_get_height(frame))
        return FALSE;
    x = (gint)source_x;
    y = (gint)source_y;

    const guchar *pixel = gdk_pixbuf_read_pixels(frame) +
                          (gsize)y * gdk_pixbuf_get_rowstride(frame) + (gsize)x * 3;
    gchar *label = g_strdup_printf("X: %d  Y: %d  ·  RGB(%u, %u, %u)  ·  #%02X%02X%02X",
                                    x, y, (guint)pixel[0], (guint)pixel[1], (guint)pixel[2],
                                    (guint)pixel[0], (guint)pixel[1], (guint)pixel[2]);
    gtk_tooltip_set_text(tooltip, label);
    g_free(label);
    return TRUE;
}

/* EN: Save the frame currently held by image as PNG. button locates the parent.
 * A retained reference keeps the chosen frame stable during the file dialog.
 * RU: Сохраняет текущий кадр image как PNG. button задаёт родительское окно.
 * Ссылка на кадр сохраняет его, пока открыт диалог выбора файла. */
static void simulation_raster_save(GtkButton *button, GtkImage *image)
{
    if (gtk_image_get_storage_type(image) != GTK_IMAGE_PIXBUF)
        return;

    GdkPixbuf *frame = g_object_ref(gtk_image_get_pixbuf(image));
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    GtkWidget *chooser = gtk_file_chooser_dialog_new(insight_tr("Save display frame"), parent,
        GTK_FILE_CHOOSER_ACTION_SAVE, insight_tr("_Cancel"), GTK_RESPONSE_CANCEL,
        insight_tr("_Save"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), "frame.png");
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
    {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        GError *error = NULL;
        if (filename && !gdk_pixbuf_save(frame, filename, "png", &error, NULL))
        {
            GtkWidget *message = gtk_message_dialog_new(GTK_WINDOW(chooser),
                GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                "%s", error->message);
            gtk_dialog_run(GTK_DIALOG(message));
            gtk_widget_destroy(message);
            g_clear_error(&error);
        }
        g_free(filename);
    }
    gtk_widget_destroy(chooser);
    g_object_unref(frame);
}

/* EN: Enable button only when image owns a frame. property identifies the GTK notification.
 * RU: Включает button, когда image содержит кадр. property задаёт уведомление GTK. */
static void simulation_raster_save_state(GtkImage *image, GParamSpec *property, GtkWidget *button)
{
    (void)property;
    gtk_widget_set_sensitive(button, gtk_image_get_storage_type(image) == GTK_IMAGE_PIXBUF);
}

/* EN: Build pages for run. final selects the retained frames after simulation.
 * RU: Создаёт вкладки run. final выбирает кадры, сохранённые после симуляции. */
static GtkWidget *simulation_raster_notebook(WaveSimulation *run, gboolean final)
{
    GtkWidget *tabs = gtk_notebook_new();
    const gchar *names[] = {"RGB", "LVDS JEIDA", "LVDS VESA", "HDMI", "DVI",
                             "Dual LVDS JEIDA", "Dual LVDS VESA"};
    for (guint i = 0; i < run->rasters->len; ++i)
    {
        RasterDisplay *display = g_ptr_array_index(run->rasters, i);
        if (final && !display->final_frame)
            continue;
        GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *canvas = gtk_drawing_area_new();
        gtk_widget_set_size_request(canvas, 260, 130);
        GtkWidget *image = gtk_image_new_from_pixbuf(final ? display->final_frame : NULL);
        g_object_set_data_full(G_OBJECT(canvas), "raster-image", g_object_ref_sink(image), g_object_unref);
        CaptureView *view = capture_view_new(page, canvas, simulation_raster_draw, image);
        gtk_widget_set_has_tooltip(canvas, TRUE);
        g_signal_connect(canvas, "query-tooltip", G_CALLBACK(simulation_raster_pixel_tooltip), view);
        gtk_box_pack_start(GTK_BOX(page), canvas, TRUE, TRUE, 0);
        if (!final)
        {
            display->image = image;
            display->canvas = canvas;
            display->status = gtk_label_new(insight_tr("Waiting for a complete display frame."));
            gtk_box_pack_start(GTK_BOX(page), display->status, FALSE, FALSE, 0);
        }
        GtkWidget *save = gtk_button_new_with_label(insight_tr("Save PNG…"));
        gtk_widget_set_halign(save, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(page), save, FALSE, FALSE, 0);
        g_signal_connect(save, "clicked", G_CALLBACK(simulation_raster_save), image);
        g_signal_connect_object(image, "notify::storage-type", G_CALLBACK(simulation_raster_save_state), save, 0);
        simulation_raster_save_state(GTK_IMAGE(image), NULL, save);

        gchar *name = g_strdup_printf("%s %u · %u × %u", names[display->mode], i + 1,
                                      display->width, display->height);
        gtk_notebook_append_page(GTK_NOTEBOOK(tabs), page, gtk_label_new(name));
        g_free(name);
    }
    return tabs;
}

/* EN: Present run's retained display frames in a window owned by parent.
 * RU: Показывает сохранённые кадры run в окне, принадлежащем parent. */
static void simulation_rasters_show_final(WaveSimulation *run, GtkWindow *parent)
{
    gboolean available = FALSE;
    for (guint i = 0; run->rasters && i < run->rasters->len; ++i)
    {
        RasterDisplay *display = g_ptr_array_index(run->rasters, i);
        available |= display->final_frame != NULL;
    }
    if (!available)
        return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(insight_tr("Display frames"), parent,
        GTK_DIALOG_DESTROY_WITH_PARENT, insight_tr("_Close"), GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 540);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       simulation_raster_notebook(run, TRUE), TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
}
