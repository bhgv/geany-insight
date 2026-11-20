/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Drain both subprocess pipes concurrently with bounded memory, including native Windows OpenSSH
 * diagnostics. RU: Читает оба канала процесса параллельно с ограничением памяти, включая диагностику OpenSSH
 * Windows. */

/* EN: Release the task in data and its owned process/input; input is never a
 * password. RU: Освобождает задание data и принадлежащие ему процесс/ввод;
 * input не содержит пароль. */
static void setup_task_free(gpointer data)
{
    SetupTask *task = data;
    g_object_unref(task->process);
    g_free(task->input);
    g_free(task);
}

/* EN: Return bounded UTF-8 diagnostics from output, removing only the wizard's protocol lines.
 * RU: Возвращает ограниченную диагностику UTF-8 из output, удаляя только протокольные строки мастера. */
static gchar *setup_failure_output(const gchar *output)
{
    gchar **lines = g_strsplit(output, "\n", -1);
    GString *message = g_string_new(NULL);
    for (guint i = 0; lines[i]; i++)
        if (!g_str_has_prefix(lines[i], "GEANY-SSH-CHECK:") &&
            strcmp(g_strchomp(lines[i]), "GEANY-SSH-READY"))
            g_string_append_printf(message, "%s\n", lines[i]);
    g_strfreev(lines);
    gchar *valid = g_utf8_make_valid(message->str, MIN(message->len, (gsize)4096));
    g_string_free(message, TRUE);
    return g_strstrip(valid);
}

typedef struct
{
    GInputStream *input;
    GSubprocess *process;
    GCancellable *cancel;
    GString *output;
    GError *error;
} SetupStream;

/* EN: Drain one SetupStream in data; bound each pipe and terminate its owned process on read/size failures.
 * RU: Читает SetupStream из data; ограничивает каждый канал и завершает собственный процесс при ошибке
 * чтения/размера. */
static gpointer setup_stream_read(gpointer data)
{
    SetupStream *stream = data;
    gchar bytes[4096];
    for (;;)
    {
        gssize count =
            g_input_stream_read(stream->input, bytes, sizeof bytes, stream->cancel, &stream->error);
        if (count <= 0)
            break;
        if (stream->output->len + count > 65536)
        {
            g_set_error_literal(&stream->error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                                "SSH setup output exceeds 64 KiB.");
            break;
        }
        g_string_append_len(stream->output, bytes, count);
    }
    if (stream->error)
        g_subprocess_force_exit(stream->process);
    return NULL;
}

/* EN: Run job data for task; source is unused. cancel stops both independently drained pipes and process
 * waiting. RU: Выполняет задание data для task; source не используется. cancel прерывает оба канала и
 * ожидание процесса. */
static void setup_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    SetupTask *job = data;
    SetupStream output = {g_subprocess_get_stdout_pipe(job->process), job->process, cancel,
                          g_string_new(NULL), NULL};
    SetupStream errors = {g_subprocess_get_stderr_pipe(job->process), job->process, cancel,
                          g_string_new(NULL), NULL};
    GThread *stdout_reader = g_thread_new("ssh-setup-stdout", setup_stream_read, &output);
    GThread *reader = g_thread_new("ssh-setup-stderr", setup_stream_read, &errors);
    GError *error = NULL;
    GOutputStream *input = g_subprocess_get_stdin_pipe(job->process);
    gboolean ok =
        !job->input || g_output_stream_write_all(input, job->input, strlen(job->input), NULL, cancel, &error);
    if (ok)
        ok = g_output_stream_close(input, cancel, &error);
    if (!ok)
        g_subprocess_force_exit(job->process);
    g_thread_join(stdout_reader);
    g_thread_join(reader);
    if (!error && output.error)
        error = g_steal_pointer(&output.error);
    if (!error && errors.error)
        error = g_steal_pointer(&errors.error);
    g_clear_error(&output.error);
    g_clear_error(&errors.error);
    if (!error && output.output->len + errors.output->len > 65536)
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "SSH setup output exceeds 64 KiB.");
    if (!error)
        g_subprocess_wait(job->process, cancel, &error);
    if (!error)
    {
        if (output.output->len && errors.output->len)
            g_string_append_c(output.output, '\n');
        g_string_append_len(output.output, errors.output->str, errors.output->len);
    }
    if (!error && !g_subprocess_get_successful(job->process))
    {
        gchar *diagnostic = setup_failure_output(output.output->str);
        g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                    *diagnostic ? diagnostic : "SSH setup command failed.");
        g_free(diagnostic);
    }
    g_string_free(errors.output, TRUE);
    if (error)
    {
        g_subprocess_force_exit(job->process);
        g_subprocess_wait(job->process, NULL, NULL);
        g_string_free(output.output, TRUE);
        g_task_return_error(task, error);
    }
    else
        g_task_return_pointer(task, g_string_free(output.output, FALSE), g_free);
}
