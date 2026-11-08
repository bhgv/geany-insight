/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Load complete simulated display frames and present them in GTK.
 * RU: Загружает полные кадры симулируемых дисплеев и показывает их в GTK.
 */

typedef struct
{
    guint id, mode, width, height;
    guint32 sequence;
    GtkWidget *image, *status;
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
            display.id >= 128 || display.mode > 4 || !display.width ||
            display.width > 2048 || !display.height || display.height > 1200)
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

/* EN: Build pages for run. final selects the retained frames after simulation.
 * RU: Создаёт вкладки run. final выбирает кадры, сохранённые после симуляции. */
static GtkWidget *simulation_raster_notebook(WaveSimulation *run, gboolean final)
{
    GtkWidget *tabs = gtk_notebook_new();
    const gchar *names[] = {"RGB", "LVDS JEIDA", "LVDS VESA", "HDMI", "DVI"};
    for (guint i = 0; i < run->rasters->len; ++i)
    {
        RasterDisplay *display = g_ptr_array_index(run->rasters, i);
        if (final && !display->final_frame)
            continue;
        GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        GtkWidget *image = gtk_image_new_from_pixbuf(final ? display->final_frame : NULL);
        gtk_widget_set_halign(image, GTK_ALIGN_START);
        gtk_widget_set_valign(image, GTK_ALIGN_START);
        gtk_container_add(GTK_CONTAINER(scroll), image);
        gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
        if (!final)
        {
            display->image = image;
            display->status = gtk_label_new(insight_tr("Waiting for a complete display frame."));
            gtk_box_pack_start(GTK_BOX(page), display->status, FALSE, FALSE, 0);
        }
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
