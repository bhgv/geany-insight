/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Waveform navigation and simulator entry points.
 * RU: Навигация по осциллограммам и запуск симулятора. */

/* Present a lookup only if the editor still has the same project open. */
/* Показывает результат поиска, если в редакторе ещё открыт тот же проект. */
static void wave_jump_done(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object;
    (void)data;
    WaveJump *jump = g_task_get_task_data(G_TASK(result));
    if (!wb.alive || jump->snapshot->epoch != workbench_epoch ||
        g_strcmp0(jump->snapshot->project, app->project ? app->project->file_name : NULL))
        return;

    if (jump->preview_context)
    {
        GtkTextView *view = g_weak_ref_get(&jump->viewer);
        gboolean found = g_task_propagate_boolean(G_TASK(result), NULL);
        if (view && !g_strcmp0(jump->name, g_object_get_data(G_OBJECT(view), "hdl-context-name")))
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
            gtk_text_buffer_set_text(buffer, insight_tr("HDL declaration not found."), -1);
            if (found)
            {
                gchar *path = utils_get_locale_from_utf8(jump->path);
                GeanyDocument *doc = document_find_by_filename(jump->path);
                gchar *text = doc ? sci_get_contents(doc->editor->sci, -1) : NULL;
                if (!text && path)
                    g_file_get_contents(path, &text, NULL, NULL);
                GString *excerpt = g_string_new(NULL);
                g_string_append_printf(excerpt, "%s:%u\n", jump->path, jump->line);
                if (text)
                {
                    gchar **lines = g_strsplit(text, "\n", -1);
                    guint length = g_strv_length(lines);
                    guint first = jump->line > 3 ? jump->line - 3 : 0;
                    for (guint i = first; i < length && i < jump->line + 2; i++)
                        g_string_append_printf(excerpt, "%s %u  %.400s\n",
                            i + 1 == jump->line ? ">" : " ", i + 1, lines[i]);
                    g_strfreev(lines);
                }
                gchar *valid = g_utf8_make_valid(excerpt->str, -1);
                gtk_text_buffer_set_text(buffer, valid, -1);
                g_free(valid);
                g_string_free(excerpt, TRUE);
                g_free(text);
                g_free(path);
            }
        }
        g_clear_object(&view);
        return;
    }

    if (!g_task_propagate_boolean(G_TASK(result), NULL))
        ui_set_statusbar(TRUE, insight_tr("HDL declaration not found: %s"), jump->name);
    else if (jump->show_uses)
    {
        if (jump->snapshot->result && jump->snapshot->result->index)
            waveform_show_hdl_uses(jump);
        else
            ui_set_statusbar(TRUE, insight_tr("HDL project index unavailable for %s"), jump->name);
    }
    else
    {
        gchar *path = utils_get_locale_from_utf8(jump->path);
        diff_source_open(path, jump->line);
        g_free(path);
    }
}

/* Start source navigation or a reference lookup for the selected waveform signal. */
/* Запускает переход к исходнику или поиск ссылок для выбранного сигнала диаграммы. */
static void waveform_source(const gchar *name, gboolean show_uses, GtkWindow *viewer)
{
    if (!wb.alive || !name || !*name || strlen(name) > 4096)
        return;
    WaveJump *jump = g_new0(WaveJump, 1);
    jump->snapshot = hdl_snapshot(NULL, FALSE);
    jump->name = g_strdup(name);
    jump->show_uses = show_uses;
    g_weak_ref_init(&jump->viewer, viewer);
    GTask *task = g_task_new(NULL, NULL, wave_jump_done, NULL);
    g_task_set_task_data(task, jump, wave_jump_free);
    g_task_run_in_thread(task, wave_jump_run);
    g_object_unref(task);
}
/* EN: Resolve hierarchy and display its declaration around the line in context.
 * RU: Находит hierarchy и показывает строки около объявления в context. */
static void waveform_context(const gchar *hierarchy, GtkTextView *context)
{
    if (!wb.alive || !hierarchy || !*hierarchy)
        return;
    WaveJump *jump = g_new0(WaveJump, 1);
    jump->snapshot = hdl_snapshot(NULL, FALSE);
    jump->name = g_strdup(hierarchy);
    jump->preview_context = TRUE;
    g_weak_ref_init(&jump->viewer, context);
    g_object_set_data_full(G_OBJECT(context), "hdl-context-name", g_strdup(hierarchy), g_free);
    GTask *task = g_task_new(NULL, NULL, wave_jump_done, NULL);
    g_task_set_task_data(task, jump, wave_jump_free);
    g_task_run_in_thread(task, wave_jump_run);
    g_object_unref(task);
}

static gboolean launch_waveforms(gpointer data) {
 (void)data;
 if(!wb.alive) return G_SOURCE_REMOVE;
 gchar *sessions=g_build_filename(app->configdir,"insight","waveforms",NULL);
 insight_waveforms_open(GTK_WINDOW(main_widgets.window),sessions,waveform_source);
 g_free(sessions);
 return G_SOURCE_REMOVE;
}
static int native_waveforms(void *context) {
 (void)context;
 g_idle_add(launch_waveforms,NULL);
 return 1;
}
static void waveforms_menu(GtkMenuItem *item,gpointer data) {
 (void)item;
 native_waveforms(data);
}
/* Select saved HDL source files and run them with the installed simulator. */
/* Выбирает сохранённые исходники HDL и запускает установленный симулятор. */
static void waveform_start_simulation(GSList *selected, const gchar *root, gboolean interactive,
                                      gboolean configure_ports)
{
    if (selected)
    {
        GPtrArray *sources = g_ptr_array_new();
        gboolean saved = TRUE;
        for (GSList *node = selected; node; node = node->next)
        {
            gchar *utf8 = utils_get_utf8_from_locale(node->data);
            GeanyDocument *document = utf8 ? document_find_by_filename(utf8) : NULL;
            if (DOC_VALID(document) && document->changed)
                saved = FALSE;
            g_free(utf8);
            g_ptr_array_add(sources, node->data);
        }
        g_ptr_array_add(sources, NULL);
        if (saved)
        {
            gchar *working_directory = root ? g_strdup(root) : g_path_get_dirname(selected->data);
            gchar *data_parent = g_path_get_dirname(app->datadir);
#ifdef G_OS_WIN32
            gchar *prefix = g_strdup(data_parent);
#else
            gchar *prefix = g_path_get_dirname(data_parent);
#endif
            gchar *bin = g_build_filename(prefix, "bin", NULL);
            gchar *settings = g_build_filename(app->configdir, "insight", "verilog-debug", NULL);
            insight_waveforms_simulate(GTK_WINDOW(main_widgets.window), bin, working_directory, (const gchar *const *)sources->pdata,
                                        interactive, waveform_source, waveform_context, diff_source_open, settings,
                                        app->project ? app->project->file_name : NULL, configure_ports);
            g_free(working_directory);
            g_free(settings);
            g_free(bin);
            g_free(prefix);
            g_free(data_parent);
        }
        else
            ui_set_statusbar(TRUE, "Save the selected HDL files before running simulation.");
        g_ptr_array_unref(sources);
    }
}

static gboolean waveform_testbench_path(const gchar *filename)
{
    const gchar *extension = filename ? strrchr(filename, '.') : NULL;
    return extension && (!g_ascii_strcasecmp(extension, ".v") ||
                         !g_ascii_strcasecmp(extension, ".sv"));
}

static void waveform_debug_current(GtkMenuItem *item, gpointer data)
{
    (void)item;
    (void)data;
    GeanyDocument *document = document_get_current();
    if (!DOC_VALID(document) || !waveform_testbench_path(document->file_name) ||
        !g_path_is_absolute(document->file_name))
    {
        ui_set_statusbar(TRUE, "%s", insight_tr("Open a saved Verilog or SystemVerilog testbench first."));
        return;
    }
    if (document->changed)
    {
        ui_set_statusbar(TRUE, "%s", insight_tr("Save the current HDL file before starting the debugger."));
        return;
    }
    gchar *path = utils_get_locale_from_utf8(document->file_name);
    if (!path)
        return;
    gchar *root = app->project ? project_get_base_path() : NULL;
    GSList *dependencies = NULL;
    if (root && wb.workspace_from_project && wb.files)
    {
        gchar *root_utf8 = utils_get_utf8_from_locale(root);
        if (g_strcmp0(root_utf8, wb.root) == 0)
        {
            if (wb.scan)
            {
                ui_set_statusbar(TRUE, "Wait for the project file scan before starting Verilog debugging.");
                g_free(root_utf8);
                g_free(root);
                g_free(path);
                return;
            }
            for (guint i = 0; i < wb.files->len; ++i)
            {
                const gchar *candidate = wb.files->pdata[i];
                if (waveform_testbench_path(candidate) && strcmp(candidate, document->file_name))
                {
                    gchar *native = utils_get_locale_from_utf8(candidate);
                    if (native)
                        dependencies = g_slist_prepend(dependencies, native);
                }
            }
        }
        g_free(root_utf8);
    }
    GSList selected = {path, g_slist_sort(dependencies, (GCompareFunc)g_strcmp0)};
    waveform_start_simulation(&selected, root, TRUE, TRUE);
    g_slist_free_full(selected.next, g_free);
    g_free(root);
    g_free(path);
}

/* EN: Open the Verilog source picker from item and use data to select debug
 * or run mode. Cancellation leaves the current session alone.
 * RU: Открывает выбор исходников Verilog по item. data выбирает отладку
 * или запуск. При отмене текущий сеанс сохраняется. */
static void waveform_run_simulation(GtkMenuItem *item, gpointer data)
{
    (void)item;
    gint mode = GPOINTER_TO_INT(data);
    gboolean interactive = mode != 0;
    gboolean configure_ports = mode == 2;
    static GtkWidget *active_picker;
    if (active_picker)
    {
        gtk_window_present(GTK_WINDOW(active_picker));
        return;
    }
    GtkWindow *parent = GTK_WINDOW(main_widgets.window);
    const gchar *title = configure_ports ? "Debug Verilog Sources with Test Devices"
                       : interactive ? "Debug Verilog Testbench" : "Run Verilog Testbench";
    GtkWidget *chooser = gtk_file_chooser_dialog_new(title, parent,
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Run", GTK_RESPONSE_ACCEPT, NULL);
    g_object_ref_sink(chooser);
    active_picker = chooser;
    gtk_window_set_destroy_with_parent(GTK_WINDOW(chooser), TRUE);
    GtkFileChooser *files = GTK_FILE_CHOOSER(chooser);
    gtk_file_chooser_set_select_multiple(files, TRUE);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Verilog and SystemVerilog sources");
    gtk_file_filter_add_pattern(filter, "*.[vV]");
    gtk_file_filter_add_pattern(filter, "*.[sS][vV]");
    gtk_file_chooser_add_filter(files, filter);

    gchar *root = app->project ? project_get_base_path() : NULL;
    if (root)
        gtk_file_chooser_set_current_folder(files, root);
    GeanyDocument *current = document_get_current();
    gchar *active_source = DOC_VALID(current) && waveform_testbench_path(current->file_name)
                               ? utils_get_locale_from_utf8(current->file_name) : NULL;
    if (active_source)
        gtk_file_chooser_select_filename(files, active_source);

    GSList *selected = insight_waveforms_run_setup_dialog(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT ?
        gtk_file_chooser_get_filenames(files) : NULL;
    active_picker = NULL;
    gtk_widget_destroy(chooser);
    g_object_unref(chooser);
    if (configure_ports && active_source)
    {
        GSList *active = g_slist_find_custom(selected, active_source, (GCompareFunc)g_strcmp0);
        if (active && active != selected)
        {
            selected = g_slist_remove_link(selected, active);
            selected = g_slist_concat(active, selected);
        }
    }
    g_free(active_source);
    waveform_start_simulation(selected, root, interactive, configure_ports);
    g_slist_free_full(selected, g_free);
    g_free(root);
}
