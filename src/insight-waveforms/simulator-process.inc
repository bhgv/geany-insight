/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Icarus process launch, background compilation and capture collection.
 * RU: Запуск Icarus, фоновая компиляция и сбор диаграмм.
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
        g_string_append_printf(report, "\nProcess output (stdout and stderr):\n%s", message);
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

    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, argv, error);
    g_object_unref(launcher);
    if (!process)
    {
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

/* EN: Find the capture file in directory and report failure in error.
 * RU: Ищет файл записи в directory и сообщает об ошибке в error. */
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

/* EN: Write run's breakpoint rules into directory. Return false and set error on failure.
 * RU: Записывает точки останова run в directory. При ошибке возвращает false и заполняет error. */
static gboolean simulation_stage_rules(const WaveSimulation *run, const gchar *directory, GError **error)
{
    if (!g_hash_table_size(run->breakpoints))
        return TRUE;
    GPtrArray *records = simulation_rule_records(run);
    GString *contents = g_string_new(NULL);
    for (guint i = 0; i < records->len; i++)
        g_string_append_printf(contents, "%s\n", (gchar *)records->pdata[i]);
    gchar *path = g_build_filename(directory, ".geany-vvp-breakpoints", NULL);
    gboolean staged = g_file_set_contents(path, contents->str, (gssize)contents->len, error);
    g_free(path);
    g_string_free(contents, TRUE);
    g_ptr_array_unref(records);
    return staged;
}

#ifdef G_OS_WIN32
/* EN: Compile the staged sources using bundled ivlpp and ivl. Set handled when
 * the installed toolchain supports the direct path; cancel and error apply to both stages.
 * RU: Компилирует sources комплектными ivlpp и ivl. Устанавливает handled для
 * комплектной цепочки. cancel и error используются на обоих этапах. */
static gboolean simulation_compile_direct(WaveSimulation *run, const gchar *directory,
                                           const gchar *bin, GPtrArray *sources,
                                           GPtrArray *model_directories, GCancellable *cancel,
                                           gboolean *handled, GError **error)
{
    gchar *prefix = g_path_get_dirname(bin);
    gchar *base = g_build_filename(prefix, "lib", "ivl", NULL);
    gchar *ivlpp = g_build_filename(base, "ivlpp.exe", NULL);
    gchar *ivl = g_build_filename(base, "ivl.exe", NULL);
    gchar *target = g_build_filename(base, "vvp.conf", NULL);
    *handled = g_file_test(ivlpp, G_FILE_TEST_IS_REGULAR) &&
               g_file_test(ivl, G_FILE_TEST_IS_REGULAR) &&
               g_file_test(target, G_FILE_TEST_IS_REGULAR);
    gboolean ok = FALSE;
    if (*handled)
    {
        const gchar *flags = "geany-ivlpp.flags";
        const gchar *list = "geany-ivlpp.sources";
        const gchar *hdl = "geany-preprocessed.sv";
        const gchar *config = "geany-ivl.conf";
        gchar *flags_path = g_build_filename(directory, flags, NULL);
        gchar *list_path = g_build_filename(directory, list, NULL);
        gchar *config_path = g_build_filename(directory, config, NULL);
        gchar *standard = g_build_filename(base, "include", NULL);
        GString *defines = g_string_new("D:__ICARUS__=1\nrelative include:true\n");
        g_string_append_printf(defines, "I:%s\n", run->include_dir);
        for (guint i = 0; i < model_directories->len; ++i)
            g_string_append_printf(defines, "I:%s\n", (const gchar *)model_directories->pdata[i]);
        g_string_append_printf(defines, "I:%s\n", standard);

        GString *paths = g_string_new(NULL);
        for (guint i = 0; i < sources->len; ++i)
        {
            const gchar *source = sources->pdata[i];
            if (strpbrk(source, "\r\n"))
            {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                            "A Verilog source path contains a newline: %s", source);
                break;
            }
            g_string_append_printf(paths, "%s\n", source);
        }
        GString *settings = g_string_new(
            "generation:2012\ngeneration:no-specify\ngeneration:no-interconnect\n"
            "generation:assertions\ngeneration:xtypes\ngeneration:io-range-error\n"
            "generation:no-strict-ca-eval\ngeneration:no-strict-expr-width\n"
            "generation:shared-loop-index\ngeneration:no-verilog-ams\ngeneration:icarus-misc\n"
            "ignore_missing_modules:false\niwidth:32\nwidthcap:65536\nout:simulation.vvp\n");
        g_string_append_printf(settings, "basedir:%s\n", base);
        const gchar *modules[] = {"system", "vhdl_sys", "vhdl_textio", "v2005_math", "v2009"};
        for (guint i = 0; i < G_N_ELEMENTS(modules); ++i)
            g_string_append_printf(settings, "module:%s%c%s.vpi\n", base, G_DIR_SEPARATOR, modules[i]);
        if (run->interactive)
            g_string_append(settings, "flag:fileline=1\n");
        if (run->port_harness)
            g_string_append(settings, "root:geany_port_testbench\n");

        const gchar *preprocess[] = {ivlpp, "-L", "-F", flags, "-f", list, "-o", hdl, NULL};
        const gchar *compile[] = {ivl, "-C", config, "-C", target, "--", hdl, NULL};
        ok = (!error || !*error) &&
             g_file_set_contents(flags_path, defines->str, defines->len, error) &&
             g_file_set_contents(list_path, paths->str, paths->len, error) &&
             g_file_set_contents(config_path, settings->str, settings->len, error) &&
             simulation_process("Preprocessing (ivlpp)", preprocess, directory, bin, cancel, error) &&
             simulation_process("Compilation and elaboration (ivl)", compile, directory, bin, cancel, error);

        g_string_free(settings, TRUE);
        g_string_free(paths, TRUE);
        g_string_free(defines, TRUE);
        g_free(standard);
        g_free(config_path);
        g_free(list_path);
        g_free(flags_path);
    }
    g_free(target);
    g_free(ivl);
    g_free(ivlpp);
    g_free(base);
    g_free(prefix);
    return ok;
}
#endif

/* EN: Compile and run WaveSimulation data off the GTK thread. task receives the capture or failure, cancel stops execution and object is unused.
 * RU: Компилирует и запускает WaveSimulation из data вне потока GTK. task получает запись или ошибку, cancel останавливает работу, object не используется. */
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
        const gchar *options[] = {run->compiler,    "-g2012", "-grelative-include", "-I",
                                  run->include_dir, "-o",     "simulation.vvp"};
        for (guint i = 0; i < G_N_ELEMENTS(options); i++)
            g_ptr_array_add(command, (gpointer)options[i]);
        if (run->interactive)
            g_ptr_array_add(command, "-pfileline=1");
        GPtrArray *sources = g_ptr_array_new();
        for (guint i = 0; run->sources[i]; ++i)
            g_ptr_array_add(sources, run->sources[i]);
        GPtrArray *model_directories = g_ptr_array_new_with_free_func(g_free);
        if (run->port_model_sources)
            for (guint i = 0; i < run->port_model_sources->len; i++)
            {
                const gchar *source = run->port_model_sources->pdata[i];
                gchar *model_directory = g_path_get_dirname(source);
                g_ptr_array_add(model_directories, model_directory);
                g_ptr_array_add(command, "-I");
                g_ptr_array_add(command, model_directory);
                if (!g_strv_contains((const gchar *const *)run->sources, source))
                    g_ptr_array_add(sources, (gpointer)source);
            }
        if (run->port_images)
        {
            GHashTableIter iter;
            gpointer anchor, image;
            g_hash_table_iter_init(&iter, run->port_images);
            while (!error && g_hash_table_iter_next(&iter, &anchor, &image))
            {
                gchar *name = simulation_memory_filename(anchor);
                gchar *path = g_build_filename(directory, name, NULL);
                gsize size;
                const gchar *bytes = g_bytes_get_data(image, &size);
                g_file_set_contents(path, size ? bytes : "", size, &error);
                g_free(path);
                g_free(name);
            }
        }
        gchar *harness_path = NULL;
        if (!error && run->port_harness)
        {
            harness_path = g_build_filename(directory, "geany-port-testbench.sv", NULL);
            if (g_file_set_contents(harness_path, run->port_harness, -1, &error))
            {
                g_ptr_array_add(command, "-s");
                g_ptr_array_add(command, "geany_port_testbench");
                g_ptr_array_add(sources, harness_path);
            }
        }
        gboolean compiled = FALSE, handled = FALSE;
#ifdef G_OS_WIN32
        if (!error)
            compiled = simulation_compile_direct(run, directory, bin, sources, model_directories,
                                                 cancel, &handled, &error);
#endif
        if (!error && !handled)
        {
            gchar *list_path = g_build_filename(directory, "sources.list", NULL);
            if (simulation_source_arguments(command, sources, "-c", list_path, &error))
            {
                g_ptr_array_add(command, NULL);
                compiled = simulation_process("Compilation (iverilog)",
                    (const gchar *const *)command->pdata, directory, bin, cancel, &error);
            }
            g_free(list_path);
        }
        const gchar *execute[] = {run->runtime, "simulation.vvp", NULL};
        if (compiled &&
            (run->interactive ? (simulation_stage_rules(run, directory, &error) &&
                                 simulation_interactive(run, directory, bin, cancel, &error))
                              : simulation_process("Simulation (vvp)", execute, directory, bin, cancel, &error)))
        {
            gchar *vcd = simulation_find_vcd(directory, &error);
            if (!vcd && run->port_harness && strstr(run->port_harness, "// GEANY-CAPTURE-TRIGGER") &&
                g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            {
                g_clear_error(&error);
                g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                    "The capture trigger did not fire before the simulation ended.");
            }
            if (vcd)
                capture = read_capture_file(vcd, cancel, &error);
            g_free(vcd);
        }
        g_ptr_array_unref(command);
        g_ptr_array_unref(sources);
        g_ptr_array_unref(model_directories);
        g_free(harness_path);
        g_free(bin);

        if (!g_cancellable_is_cancelled(cancel))
        {
            simulation_rasters_finish(run, directory);
            simulation_memory_finish(run, directory);
        }
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
