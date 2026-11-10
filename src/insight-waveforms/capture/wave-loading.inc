/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Asynchronous loading of VCD recordings.
 * RU: Асинхронная загрузка записей VCD.
 */

typedef struct
{
    GWeakRef parent;
    gchar *path, *session_path;
    InsightWaveSource navigate;
} Load;
/* EN: Release the VCD Load request held in data.
 * RU: Освобождает запрос загрузки VCD из data. */
static void load_free(gpointer data)
{
    Load *load = data;
    g_weak_ref_clear(&load->parent);
    g_free(load->path);
    g_free(load->session_path);
    g_free(load);
}
/* EN: Read a bounded VCD at path and return an owned capture. cancel stops I/O and error receives failures.
 * RU: Читает ограниченный VCD из path и возвращает новую запись. cancel отменяет чтение, error получает ошибки. */
static Capture *read_capture_file(const gchar *path, GCancellable *cancel, GError **error)
{
    GFile *file = g_file_new_for_path(path);
    GFileInputStream *stream = g_file_read(file, cancel, error);
    g_object_unref(file);
    Capture *capture = NULL;
    if (stream)
    {
        GByteArray *bytes = g_byte_array_new();
        guint8 buffer[16384];
        gssize size;
        while ((size = g_input_stream_read(G_INPUT_STREAM(stream), buffer, sizeof buffer, cancel,
                                           error)) > 0)
        {
            if (bytes->len + (gsize)size > VCD_BYTES)
                break;
            g_byte_array_append(bytes, buffer, (guint)size);
        }
        if (size == 0 && bytes->len && !memchr(bytes->data, 0, bytes->len))
            capture = parse_capture((const gchar *)bytes->data, bytes->len, error);
        else if (!*error)
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "VCD must be text, 1 byte to 32 MiB");
        g_byte_array_unref(bytes);
        g_object_unref(stream);
    }
    return capture;
}

/* EN: Read the Load request in data and return its capture or error to task. object is unused, cancel stops I/O.
 * RU: Читает запрос Load из data и возвращает запись или ошибку в task. object не используется, cancel отменяет чтение. */
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

/* EN: Handle result on the GTK thread and show the capture or report failure. object and data are unused.
 * RU: Обрабатывает result в потоке GTK и показывает запись либо ошибку. object и data не используются. */
static void load_done(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object;
    (void)data;
    GTask *task = G_TASK(result);
    Load *load = g_task_get_task_data(task);
    GtkWindow *parent = g_weak_ref_get(&load->parent);
    GError *error = NULL;
    Capture *capture = g_task_propagate_pointer(task, &error);
    if (parent && capture)
        show_capture(parent, capture, load->session_path, load->navigate);
    else
    {
        if (capture)
            capture_free(capture);
        if (parent && error)
        {
            GtkWidget *dialog =
                gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_CLOSE, "%s", error->message);
            g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
            gtk_widget_show(dialog);
        }
    }
    g_clear_error(&error);
    if (parent)
        g_object_unref(parent);
}

/* EN: Choose a VCD under parent and load it asynchronously. session_dir stores view settings, navigate opens HDL source.
 * RU: Выбирает VCD в окне parent и загружает его асинхронно. session_dir хранит настройки, navigate открывает исходник HDL. */
void insight_waveforms_open(GtkWindow *parent, const gchar *session_dir, InsightWaveSource navigate)
{
    GtkWidget *dialog =
        gtk_file_chooser_dialog_new("Open VCD waveform", parent, GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
                                    GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "VCD waveforms");
    gtk_file_filter_add_pattern(filter, "*.[vV][cC][dD]");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    gchar *path = insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT
                      ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog))
                      : NULL;
    gtk_widget_destroy(dialog);
    if (!path)
        return;
    Load *load = g_new0(Load, 1);
    g_weak_ref_init(&load->parent, parent);
    load->path = path;
    if (session_dir)
    {
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
