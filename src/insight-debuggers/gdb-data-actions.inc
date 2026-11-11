/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Native data watchpoints, value assignment and execution to the editor caret.
 * RU: Точки останова по данным, изменение значений и выполнение до курсора редактора. */

/* EN: Return a quoted MI watchpoint command for expression and action, or NULL for other actions.
 * RU: Возвращает экранированную команду точки по expression и action либо NULL для иных действий. */
static gchar *native_data_watch_command(const gchar *expression, NativeDataAction action)
{
    if (!expression || !*expression || action < NATIVE_DATA_WRITE || action > NATIVE_DATA_ACCESS)
        return NULL;
    gchar *escaped = g_strescape(expression, NULL);
    const gchar *option = action == NATIVE_DATA_READ ? " -r" : action == NATIVE_DATA_ACCESS ? " -a" : "";
    gchar *command = g_strdup_printf("-break-watch%s \"%s\"", option, escaped);
    g_free(escaped);
    return command;
}

/* EN: Refresh session's locals, watches and an existing inspection after a successful write.
 * RU: Обновляет переменные, наблюдения и открытое дерево session после успешной записи. */
static void native_data_refresh(NativeDebug *session)
{
    gboolean reload = session->value_root != NULL;
    native_values_reset(session);
    session->value_reload = reload;
    session->locals_needed = TRUE;
    session->watch_next = 0;
}

/* EN: Apply action to expression in session; queue watchpoints when called from an async path reply.
 * RU: Применяет action к expression в session; ставит точки по данным в очередь при асинхронном ответе пути. */
static void native_data_use_expression(NativeDebug *session, const gchar *expression, NativeDataAction action)
{
    if (!session->ready || !session->stopped || !expression || !*expression)
        return;
    if (action == NATIVE_DATA_INSPECT)
        native_values_inspect(session, expression);
    else if (action == NATIVE_DATA_WATCH)
    {
        if (g_utf8_strlen(expression, -1) > gtk_entry_get_max_length(GTK_ENTRY(session->watch_entry)))
        {
            ui_set_statusbar(FALSE, "%s", insight_tr("Watch expression exceeds 1024 characters."));
            return;
        }
        gtk_entry_set_text(GTK_ENTRY(session->watch_entry), expression);
        native_watch_edit(GTK_BUTTON(session->watch_add), session->window);
    }
    else
    {
        gchar *command = native_data_watch_command(expression, action);
        if (command && !session->data_command)
        {
            session->data_command = command;
            session->points_needed = TRUE;
            native_debug_resume(session->window, session);
        }
        else
            g_free(command);
    }
}

/* EN: Ask for a replacement C/C++ value in session and assign expression or its GDB object.
 * RU: Запрашивает новое значение C/C++ в session и присваивает expression либо его объекту GDB. */
static void native_assign_dialog(NativeDebug *session, const gchar *expression, const gchar *value,
                                 const gchar *object)
{
    if (!session->ready || !session->stopped || session->pending || session->writing)
        return;
    GtkWidget *window = g_object_ref(session->window);
    guint generation = session->value_generation;
    GtkWidget *dialog = gtk_dialog_new_with_buttons(insight_tr("Change Value"), GTK_WINDOW(window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(expression);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 8);
    GtkWidget *hint = gtk_label_new(insight_tr("Enter a C/C++ value expression. This changes the paused program."));
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 8);
    GtkWidget *entry = gtk_entry_new();
    gtk_widget_set_name(entry, "insight-gdb-new-value");
    gtk_entry_set_max_length(GTK_ENTRY(entry), 8192);
    gtk_entry_set_text(GTK_ENTRY(entry), value ? value : "");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT && !session->closed)
    {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
        if (*text && session->stopped && !session->pending && !session->writing &&
            session->value_generation == generation)
        {
            gchar *command;
            if (object)
            {
                gchar *escaped_name = g_strescape(object, NULL);
                gchar *escaped_value = g_strescape(text, NULL);
                command = g_strdup_printf("-var-assign \"%s\" \"%s\"", escaped_name, escaped_value);
                session->value_request = NATIVE_VALUE_ASSIGN;
                session->value_pending_generation = generation;
                g_free(escaped_value);
                g_free(escaped_name);
            }
            else
            {
                gchar *assignment = g_strdup_printf("(%s) = (%s)", expression, text);
                gchar *escaped = g_strescape(assignment, NULL);
                command = g_strdup_printf("-data-evaluate-expression \"%s\"", escaped);
                session->data_refresh_pending = TRUE;
                g_free(escaped);
                g_free(assignment);
            }
            native_debug_send(window, command);
            g_free(command);
        }
        else
            ui_set_statusbar(FALSE, "%s", insight_tr("Enter a value while the same frame is paused."));
    }
    gtk_widget_destroy(dialog);
    g_object_unref(window);
}

/* EN: Apply button's action to the selected local in session data, preserving its current text value.
 * RU: Применяет действие button к выбранной локальной переменной session data с её текущим значением. */
static void native_local_action(GtkButton *button, gpointer data)
{
    NativeDebug *session = data;
    if (!session->stopped || session->pending || session->writing)
        return;
    GtkTreeIter row;
    GtkTreeModel *model = NULL;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(session->locals_view));
    if (!gtk_tree_selection_get_selected(selection, &model, &row))
        return;
    gchar *expression = NULL, *value = NULL;
    gtk_tree_model_get(model, &row, 0, &expression, 1, &value, -1);
    NativeDataAction action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "data-action"));
    if (expression && action == NATIVE_DATA_ASSIGN)
        native_assign_dialog(session, expression, value, NULL);
    else if (expression)
        native_data_use_expression(session, expression, action);
    g_free(value);
    g_free(expression);
}

/* EN: Apply button's action to the watch expression entered in session data.
 * RU: Применяет действие button к введённому выражению наблюдения session data. */
static void native_watch_data_action(GtkButton *button, gpointer data)
{
    NativeDebug *session = data;
    NativeDataAction action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "data-action"));
    native_data_use_expression(session, gtk_entry_get_text(GTK_ENTRY(session->watch_entry)), action);
}

/* EN: Build paused-only data actions for session, using either the locals selection or watch entry.
 * RU: Создаёт доступные на паузе действия session для выбранной переменной либо поля наблюдения. */
static GtkWidget *native_data_buttons(NativeDebug *session, gboolean locals)
{
    GtkWidget *bar = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(bar), GTK_SELECTION_NONE);
    const gchar *labels[] = {"Inspect selected value", "Watch selected value", "Change selected value",
                             "Break on Write", "Break on Read", "Break on Access"};
    const NativeDataAction actions[] = {NATIVE_DATA_INSPECT, NATIVE_DATA_WATCH, NATIVE_DATA_ASSIGN,
                                        NATIVE_DATA_WRITE, NATIVE_DATA_READ, NATIVE_DATA_ACCESS};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        if (!locals && (i == 1 || i == 2))
            continue;
        GtkWidget *button = gtk_button_new_with_label(insight_tr(!locals && i == 0 ? "Inspect watch expression" : labels[i]));
        g_object_set_data(G_OBJECT(button), "data-action", GUINT_TO_POINTER(actions[i]));
        g_signal_connect(button, "clicked", locals ? G_CALLBACK(native_local_action) :
                                                   G_CALLBACK(native_watch_data_action), session);
        gtk_container_add(GTK_CONTAINER(bar), button);
    }
    g_ptr_array_add(session->paused_controls, bar);
    return bar;
}

/* EN: Run session's selected thread until the saved editor caret, letting GDB validate the location.
 * RU: Выполняет выбранный поток session до курсора сохранённого исходника; позицию проверяет GDB. */
static void native_run_to_cursor(GtkWidget *dialog)
{
    NativeDebug *session = g_object_get_data(G_OBJECT(dialog), "native-debug");
    GeanyDocument *document = document_get_current();
    if (!session->stopped || session->pending || session->writing || !DOC_VALID(document) || !document->file_name)
        return;
    guint line = sci_get_line_from_position(document->editor->sci,
                                            sci_get_current_position(document->editor->sci)) + 1;
    gchar *location = g_strdup_printf("%s:%u", document->file_name, line);
    gchar *escaped = g_strescape(location, NULL);
    gchar *command = g_strdup_printf("-exec-until \"%s\"", escaped);
    native_debug_send(dialog, command);
    g_free(command);
    g_free(escaped);
    g_free(location);
}

/* EN: Summarize stop record, including watchpoint old/new values and signal names; caller frees the result.
 * RU: Кратко описывает record остановки, включая старое/новое значение и сигнал; результат освобождает вызывающий. */
static gchar *native_stop_detail(const gchar *record)
{
    gchar *reason = native_debug_field(record, "reason");
    gchar *result;
    if (reason && strstr(reason, "watchpoint"))
    {
        gchar *expression = native_debug_field(record, "exp");
        gchar *old = native_debug_field(record, "old");
        gchar *value = native_debug_field(record, "new");
        result = g_strdup_printf("%s: %.200s%s%.300s%s%.300s", insight_tr("Data watchpoint"),
            expression ? expression : "", old ? " — " : "", old ? old : "",
            value ? " → " : "", value ? value : "");
        g_free(value);
        g_free(old);
        g_free(expression);
    }
    else if (!g_strcmp0(reason, "signal-received"))
    {
        gchar *name = native_debug_field(record, "signal-name");
        result = g_strdup_printf("%s: %s", insight_tr("Paused by signal"), name ? name : "");
        g_free(name);
    }
    else
        result = g_strdup(insight_tr(!g_strcmp0(reason, "breakpoint-hit") ? "Breakpoint hit" :
            !g_strcmp0(reason, "end-stepping-range") ? "Step complete" :
            !g_strcmp0(reason, "function-finished") ? "Function returned" : "Stopped"));
    g_free(reason);
    gchar *valid = g_utf8_make_valid(result, -1);
    g_free(result);
    return valid;
}
