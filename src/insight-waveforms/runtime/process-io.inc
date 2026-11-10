/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Compiler and simulator subprocess I/O.
 * RU: Ввод и вывод процессов компилятора и симулятора.
 */

/* EN: Find executable name in bin or PATH. Return an owned path or NULL.
 * RU: Ищет программу name в bin или PATH. Возвращает новый путь или NULL. */
static gchar *simulation_tool(const gchar *bin, const gchar *name)
{
    gchar *path = g_build_filename(bin, name, NULL);
    if (g_file_test(path, G_FILE_TEST_IS_EXECUTABLE))
        return path;
    g_free(path);
    return g_find_program_in_path(name);
}

/* EN: Force subprocess data to exit when cancel is triggered.
 * RU: Завершает процесс из data при срабатывании cancel. */
static void simulation_force_exit(GCancellable *cancel, gpointer data)
{
    (void)cancel;
    g_subprocess_force_exit(G_SUBPROCESS(data));
}

/* EN: Add stage, argv, directory and captured output to the existing process error.
 * output_note describes any clipping of the captured output.
 * RU: Добавляет к ошибке этап stage, argv, каталог directory и вывод output.
 * output_note сообщает об ограничении захваченного вывода. */
static void simulation_process_diagnostic(GError **error, const gchar *stage,
                                          const gchar *const *argv, const gchar *directory,
                                          const GString *output, const gchar *output_note)
{
    if (!error || !*error || g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

    GString *report = g_string_new(NULL);
    g_string_append_printf(report, "%s: %s\nWorking directory: %s\nArguments:", stage,
                           (*error)->message, directory);
    for (guint i = 0; argv[i]; ++i)
    {
        gchar *escaped = g_strescape(argv[i], NULL);
        g_string_append_printf(report, "\n  [%u] \"%s\"", i, escaped);
        g_free(escaped);
    }

    gchar *message = output ? g_utf8_make_valid(output->str, output->len) : NULL;
    if (message && *g_strstrip(message))
    {
        g_string_append(report, "\nProcess output (stdout and stderr):");
        gchar **lines = g_strsplit(message, "\n", -1);
        GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
        guint repeated = 0;
        for (guint i = 0; lines[i]; ++i)
        {
            gchar *line = g_strchomp(lines[i]);
            if (*line && !g_hash_table_add(seen, line))
                ++repeated;
            else
                g_string_append_printf(report, "\n%s", line);
        }
        if (repeated)
            g_string_append_printf(report, "\nRepeated output lines omitted: %u.", repeated);
        g_hash_table_unref(seen);
        g_strfreev(lines);
    }
    else
        g_string_append(report, "\nProcess output: none.");
    if (message && strstr(message, "Unknown module type:"))
        g_string_append(report,
            "\nAn HDL instance refers to an undefined module. Add its simulation source, "
            "or configure a replacement with the same module and port names. "
            "For a simple PLL, use PLL replacement in Port Signals.");
    if (output_note)
        g_string_append_printf(report, "\n%s", output_note);

    GError *original = *error;
    *error = g_error_new_literal(original->domain, original->code, report->str);
    g_error_free(original);
    g_free(message);
    g_string_free(report, TRUE);
}

/* EN: Run stage with argv in directory and bin on PATH. cancel stops the child. On failure,
 * return FALSE and capture up to 256 KiB of diagnostics in error.
 * RU: Запускает этап stage с argv в directory и bin в PATH. cancel останавливает процесс.
 * При ошибке возвращает FALSE и сохраняет до 256 КиБ диагностики в error. */
static gboolean simulation_process(const gchar *stage, const gchar *const *argv,
                                   const gchar *directory, const gchar *bin,
                                   GCancellable *cancel, GError **error)
{
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return FALSE;
    GSubprocessLauncher *launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, directory);
    const gchar *existing = g_getenv("PATH");
    gchar *path = g_strconcat(bin, G_SEARCHPATH_SEPARATOR_S, existing ? existing : "", NULL);
    g_subprocess_launcher_setenv(launcher, "PATH", path, TRUE);
    g_free(path);

#ifdef G_OS_WIN32
    gchar *ivl_diagnostics = NULL;
    gchar *program = g_path_get_basename(argv[0]);
    if (!g_ascii_strcasecmp(program, "ivl.exe"))
    {
        ivl_diagnostics = g_build_filename(directory, "geany-ivl.stderr", NULL);
        g_unlink(ivl_diagnostics);
        g_subprocess_launcher_setenv(launcher, "GEANY_IVL_DIAGNOSTICS", "geany-ivl.stderr", TRUE);
    }
    g_free(program);
#endif
    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, argv, error);
    g_object_unref(launcher);
    if (!process)
    {
#ifdef G_OS_WIN32
        g_free(ivl_diagnostics);
#endif
        simulation_process_diagnostic(error, stage, argv, directory, NULL, NULL);
        return FALSE;
    }

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
#ifdef G_OS_WIN32
    if (ivl_diagnostics)
    {
        FILE *file = !ok ? g_fopen(ivl_diagnostics, "rb") : NULL;
        if (file)
        {
            gsize available = limit - diagnostic->len;
            while (available)
            {
                gsize size = fread(bytes, 1, MIN(available, sizeof bytes), file);
                if (!size)
                    break;
                g_string_append_len(diagnostic, bytes, size);
                available -= size;
            }
            truncated |= fgetc(file) != EOF;
            fclose(file);
        }
        g_free(ivl_diagnostics);
    }
#endif
    if (!ok)
        simulation_process_diagnostic(error, stage, argv, directory, diagnostic,
                                      truncated ? "Additional process output omitted." : NULL);
    g_string_free(diagnostic, TRUE);
    g_cancellable_disconnect(cancel, cancellation);
    g_object_unref(process);
    return ok;
}

/* EN: Cancel the active subprocess of run stored in data when cancel fires.
 * RU: Останавливает активный процесс сеанса из data при срабатывании cancel. */
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

/* EN: Run the interactive session run in directory with tool folder bin. cancel stops it and error receives failures.
 * RU: Ведёт сеанс run в directory с программами из bin. cancel прерывает сеанс, error получает ошибки. */
static gboolean simulation_interactive(WaveSimulation *run, const gchar *directory, const gchar *bin,
                                       GCancellable *cancel, GError **error)
{
    if (g_cancellable_set_error_if_cancelled(cancel, error))
        return FALSE;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, directory);
    g_subprocess_launcher_setenv(launcher, "GEANY_VVP_DEBUG", "1", TRUE);
    g_subprocess_launcher_setenv(launcher, "GEANY_LUA_TERMINAL", "1", TRUE);
    if (run->port_harness && (strstr(run->port_harness, "$geany_sram(") ||
                              strstr(run->port_harness, "$geany_parallel_psram(") ||
                              strstr(run->port_harness, "$geany_sdram(") ||
                              strstr(run->port_harness, "$geany_ddr") ||
                              strstr(run->port_harness, "$geany_spi_") ||
                              strstr(run->port_harness, "$geany_i2c_") ||
                              strstr(run->port_harness, "$geany_hdmi_ddc(")))
        g_subprocess_launcher_setenv(launcher, "GEANY_MEMORY_INSPECT", "1", TRUE);
    const gchar *existing = g_getenv("PATH");
    gchar *path = g_strconcat(bin, G_SEARCHPATH_SEPARATOR_S, existing ? existing : "", NULL);
    g_subprocess_launcher_setenv(launcher, "PATH", path, TRUE);
    g_free(path);
    gchar *runtime_bin = g_path_get_dirname(run->runtime);
    gchar *runtime_prefix = g_path_get_dirname(runtime_bin);
    gchar *modules = g_build_filename(runtime_prefix, "lib", "ivl", NULL);
    const gchar *argv[] = {run->runtime, "-i", "-s", "simulation.vvp", NULL};
    const gchar *device_argv[] = {run->runtime, "-M", modules,          "-m", "geany_devices",
                                  "-i",         "-s", "simulation.vvp", NULL};
    gboolean devices =
        run->port_harness &&
        (strstr(run->port_harness, "$geany_parallel_psram(") ||
         strstr(run->port_harness, "$geany_sram(") || strstr(run->port_harness, "$geany_sdram(") ||
         strstr(run->port_harness, "$geany_ddr1(") || strstr(run->port_harness, "$geany_ddr2(") || strstr(run->port_harness, "$geany_ddr3(") ||
         strstr(run->port_harness, "$geany_ddr4(") || strstr(run->port_harness, "$geany_ddr5(") || strstr(run->port_harness, "$geany_spi_") ||
         strstr(run->port_harness, "$geany_i2c_") || strstr(run->port_harness, "$geany_usart(") ||
         strstr(run->port_harness, "$geany_raster(") || strstr(run->port_harness, "$geany_raster_dual(") || strstr(run->port_harness, "$geany_hdmi_ddc("));
    const gchar *const *command = devices ? device_argv : argv;
    const gchar *stage = "Interactive simulation (vvp)";
    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, command, error);
    g_free(runtime_prefix);
    g_free(runtime_bin);
    g_object_unref(launcher);
    if (!process)
    {
        simulation_process_diagnostic(error, stage, command, directory, NULL, NULL);
        g_free(modules);
        return FALSE;
    }

    g_mutex_lock(&run->lock);
    run->live_process = g_object_ref(process);
    run->live_directory = g_strdup(directory);
    g_mutex_unlock(&run->lock);
    gulong cancellation = g_cancellable_connect(cancel, G_CALLBACK(simulation_cancel_live), run, NULL);

    GInputStream *output = g_subprocess_get_stdout_pipe(process);
    guint8 bytes[8192];
    GString *diagnostic = g_string_new(NULL);
    const gsize diagnostic_limit = 64 * 1024;
    gboolean truncated = FALSE;
    gssize count;
    while ((count = g_input_stream_read(output, bytes, sizeof bytes, cancel, error)) > 0)
    {
        if (diagnostic->len + (gsize)count > diagnostic_limit)
        {
            g_string_erase(diagnostic, 0, diagnostic->len + (gsize)count - diagnostic_limit);
            truncated = TRUE;
        }
        g_string_append_len(diagnostic, (const gchar *)bytes, count);
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
    if (!ok)
        simulation_process_diagnostic(error, stage, command, directory, diagnostic,
                                      truncated ? "Earlier process output omitted." : NULL);
    g_free(modules);
    g_string_free(diagnostic, TRUE);
    g_cancellable_disconnect(cancel, cancellation);
    g_mutex_lock(&run->lock);
    g_clear_object(&run->live_process);
    g_mutex_unlock(&run->lock);
    g_object_unref(process);
    return ok;
}
