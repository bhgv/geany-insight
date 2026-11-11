/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Run, stop and restart a native target with explicit arguments and working directory.
 * RU: Запускает, останавливает и перезапускает программу с аргументами и рабочим каталогом. */

/* EN: Clear session's stopped-context values and source highlight after target exit.
 * RU: Удаляет значения контекста и подсветку session после завершения программы. */
static void native_target_clear(NativeDebug *session)
{
    session->running = session->stopped = session->resume = FALSE;
    session->stop_requested = FALSE;
    session->threads_needed = session->stack_needed = session->locals_needed = FALSE;
    session->all_stacks_needed = session->frame_select_needed = FALSE;
    g_clear_pointer(&session->selected_thread, g_free);
    g_clear_pointer(&session->selected_frame, g_free);
    g_clear_pointer(&session->requested_frame, g_free);
    g_clear_pointer(&session->stop_file, g_free);
    session->stop_line = 0;
    gtk_list_store_clear(session->local_rows);
    gtk_list_store_clear(session->stack_rows);
    gtk_tree_store_clear(session->all_stack_rows);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(session->threads));
    g_ptr_array_set_size(session->locals, 0);
    g_ptr_array_set_size(session->stop_markers, 0);
    session->watch_next = session->expressions->len;
    for (guint i = 0; i < session->expressions->len; i++)
    {
        NativeWatch *watch = session->expressions->pdata[i];
        g_free(watch->value);
        watch->value = g_strdup(insight_tr("Unavailable"));
    }
    native_watch_refresh(session, FALSE);
}

/* EN: Build MI argument command from text; return NULL and set error for invalid quoting.
 * RU: Создаёт команду аргументов MI из text; при неверных кавычках возвращает NULL и error. */
static gchar *native_launch_arguments(const gchar *text, GError **error)
{
    gint count = 0;
    gchar **arguments = NULL;
    gchar *trimmed = g_strdup(text);
    g_strstrip(trimmed);
    gboolean valid = !*trimmed || g_shell_parse_argv(trimmed, &count, &arguments, error);
    g_free(trimmed);
    if (!valid)
        return NULL;
    GString *command = g_string_new("-exec-arguments");
    for (gint i = 0; i < count; i++)
    {
        gchar *escaped = g_strescape(arguments[i], NULL);
        g_string_append_printf(command, " \"%s\"", escaped);
        g_free(escaped);
    }
    g_strfreev(arguments);
    return g_string_free(command, FALSE);
}

/* EN: Validate dialog's launch form and queue settings plus run; report errors without launching.
 * RU: Проверяет форму dialog и ставит настройки и запуск в очередь; ошибки показываются без запуска. */
static gboolean native_launch_prepare(GtkWidget *dialog)
{
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    GError *error = NULL;
    gchar *arguments = native_launch_arguments(
        gtk_entry_get_text(GTK_ENTRY(session->launch_args)), &error);
    const gchar *directory = gtk_entry_get_text(GTK_ENTRY(session->launch_directory));
    gboolean valid = arguments && g_path_is_absolute(directory) &&
                     g_file_test(directory, G_FILE_TEST_IS_DIR);
    if (!valid)
    {
        gtk_label_set_text(GTK_LABEL(session->launch_status), error ? error->message :
            insight_tr("Choose an existing absolute working directory."));
        GtkWidget *page = gtk_widget_get_parent(session->launch_status);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(session->tabs),
            gtk_notebook_page_num(GTK_NOTEBOOK(session->tabs), page));
        g_clear_error(&error);
        g_free(arguments);
        return FALSE;
    }
    insight_queue_clear_full(&session->launch_commands, g_free);
    gchar *escaped = g_strescape(directory, NULL);
    g_queue_push_tail(&session->launch_commands, g_strdup_printf("-environment-cd \"%s\"", escaped));
    g_queue_push_tail(&session->launch_commands, arguments);
    g_queue_push_tail(&session->launch_commands, g_strdup("-exec-run"));
    g_free(escaped);
    gtk_label_set_text(GTK_LABEL(session->launch_status), insight_tr("Launch settings accepted."));
    return TRUE;
}

/* EN: Handle Stop or Restart for dialog, interrupting a running inferior before killing it.
 * RU: Обрабатывает Stop или Restart для dialog, прерывая выполнение перед завершением программы. */
static void native_lifecycle_request(GtkWidget *dialog, gboolean restart)
{
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    if (session->pending || session->writing || !session->ready)
        return;
    if (restart && !native_launch_prepare(dialog))
        return;
    session->stop_requested = session->running || session->stopped;
    if (session->running)
        native_debug_send(dialog, "-exec-interrupt --all");
    else
        native_debug_resume(dialog, session);
}

/* EN: Return the next lifecycle command owned by the caller; session records a pending kill.
 * RU: Возвращает следующую команду, принадлежащую вызывающему; session запоминает ожидающую остановку. */
static gchar *native_launch_next(NativeDebug *session)
{
    if (session->stop_requested && session->stopped)
    {
        session->kill_pending = TRUE;
        return g_strdup("-interpreter-exec console \"kill\"");
    }
    return g_queue_pop_head(&session->launch_commands);
}

/* EN: Complete session's kill or launch command from record; abandon queued launches on error.
 * RU: Завершает команду остановки/запуска session по record; при ошибке отменяет очередь запуска. */
static void native_lifecycle_reply(NativeDebug *session, const gchar *record)
{
    if (session->kill_pending)
    {
        session->kill_pending = FALSE;
        session->stop_requested = FALSE;
        if (g_str_has_prefix(record, "^done"))
            native_target_clear(session);
    }
    if (g_str_has_prefix(record, "^error"))
    {
        insight_queue_clear_full(&session->launch_commands, g_free);
        session->stop_requested = FALSE;
        gchar *message = native_debug_field(record, "msg");
        if (message)
            gtk_label_set_text(GTK_LABEL(session->launch_status), message);
        g_free(message);
    }
}

/* EN: Create the launch form for session; target supplies the default working directory.
 * RU: Создаёт форму запуска session; target задаёт рабочий каталог по умолчанию. */
static GtkWidget *native_launch_widget(NativeDebug *session, const gchar *target)
{
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    const gchar *labels[] = {"Executable", "Program arguments", "Working directory"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *label = gtk_label_new(insight_tr(labels[i]));
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
    }
    GtkWidget *executable = gtk_label_new(target);
    gtk_label_set_selectable(GTK_LABEL(executable), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(executable), PANGO_ELLIPSIZE_MIDDLE);
    gtk_grid_attach(GTK_GRID(grid), executable, 1, 0, 1, 1);
    session->launch_args = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(session->launch_args), 8192);
    gtk_widget_set_name(session->launch_args, "insight-gdb-launch-arguments");
    gtk_entry_set_placeholder_text(GTK_ENTRY(session->launch_args), insight_tr("Arguments; quote values containing spaces"));
    session->launch_directory = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(session->launch_directory), 4096);
    gtk_widget_set_name(session->launch_directory, "insight-gdb-working-directory");
    gchar *directory = g_path_get_dirname(target);
    gtk_entry_set_text(GTK_ENTRY(session->launch_directory), directory);
    g_free(directory);
    gtk_widget_set_hexpand(session->launch_args, TRUE);
    gtk_widget_set_hexpand(session->launch_directory, TRUE);
    gtk_grid_attach(GTK_GRID(grid), session->launch_args, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), session->launch_directory, 1, 2, 1, 1);
    session->launch_status = gtk_label_new(insight_tr("Settings apply on Run or Restart. Breakpoints and watches are kept."));
    gtk_label_set_line_wrap(GTK_LABEL(session->launch_status), TRUE);
    gtk_grid_attach(GTK_GRID(grid), session->launch_status, 0, 3, 2, 1);
    return grid;
}
