/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Configure exception catchpoints in the shared breakpoint page and identify exception stops.
 * RU: Настраивает точки на исключениях в общей вкладке точек и распознаёт такие остановки. */

/* EN: Add the exception event/filter stored on button to the session in data; report validation failures without sending MI.
 * RU: Добавляет событие/фильтр из button в сеанс data; при ошибке проверки не отправляет MI. */
static void native_catch_clicked(GtkButton *button, gpointer data)
{
    NativeDebug *session = data;
    if (session->closed || !session->ready || session->pending || session->writing || session->running)
        return;
    GtkWidget *event = g_object_get_data(G_OBJECT(button), "catch-event");
    GtkWidget *filter = g_object_get_data(G_OBJECT(button), "catch-filter");
    gchar *command = native_catch_command((guint)gtk_combo_box_get_active(GTK_COMBO_BOX(event)),
        gtk_entry_get_text(GTK_ENTRY(filter)));
    if (!command)
    {
        ui_set_statusbar(TRUE, "%s", insight_tr("Choose an exception event and a single-line type filter of at most 1024 characters."));
        return;
    }
    session->points_needed = TRUE;
    native_debug_send(session->window, command);
    g_free(command);
}

/* EN: Activate the Add button in data after Enter in entry; reuse the same validation and busy guards.
 * RU: Нажимает кнопку добавления data по Enter в entry; использует общую проверку и ограничения состояния. */
static void native_catch_activate(GtkEntry *entry, gpointer data)
{
    (void)entry;
    gtk_button_clicked(GTK_BUTTON(data));
}

/* EN: Build session's C++ exception controls; their parent breakpoint controls own idle/running sensitivity.
 * RU: Создаёт элементы исключений C++ для session; доступностью управляет родительская группа точек. */
static GtkWidget *native_catch_widget(NativeDebug *session)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *event = gtk_combo_box_text_new();
    gtk_widget_set_name(event, "insight-gdb-exception-event");
    const gchar *labels[] = {"C++ throw", "C++ catch", "C++ rethrow"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(event), insight_tr(labels[i]));
    gtk_combo_box_set_active(GTK_COMBO_BOX(event), 0);
    GtkWidget *filter = gtk_entry_new();
    gtk_widget_set_name(filter, "insight-gdb-exception-filter");
    gtk_entry_set_max_length(GTK_ENTRY(filter), 4096);
    gtk_entry_set_placeholder_text(GTK_ENTRY(filter), insight_tr("Exception type regex (optional)"));
    GtkWidget *add = gtk_button_new_with_label(insight_tr("Add Exception Catchpoint"));
    gtk_widget_set_name(add, "insight-gdb-exception-add");
    g_object_set_data(G_OBJECT(add), "catch-event", event);
    g_object_set_data(G_OBJECT(add), "catch-filter", filter);
    g_signal_connect(add, "clicked", G_CALLBACK(native_catch_clicked), session);
    g_signal_connect(filter, "activate", G_CALLBACK(native_catch_activate), add);
    gtk_box_pack_start(GTK_BOX(row), event, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), filter, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), add, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    GtkWidget *hint = gtk_label_new(insight_tr(
        "Empty filter: all exception types. Catchpoints appear in this list and last for this debugger session."));
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 90);
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);
    return box;
}

/* EN: Return the confirmed catchpoint description for session's stop record, or NULL for another kind of stop.
 * RU: Возвращает подтверждённое описание точки исключения session для record либо NULL для другого вида остановки. */
static gchar *native_catch_stop_detail(NativeDebug *session, const gchar *record)
{
    gchar *number = native_debug_field(record, "bkptno");
    if (!native_context_id(number))
    {
        g_free(number);
        return NULL;
    }
    GtkTreeModel *model = GTK_TREE_MODEL(session->point_rows);
    GtkTreeIter row;
    gchar *result = NULL;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid && !result;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *id = NULL, *kind = NULL, *caption = NULL;
        gtk_tree_model_get(model, &row, 1, &id, 8, &kind, 0, &caption, -1);
        if (!g_strcmp0(number, id) && !g_strcmp0(kind, "catchpoint"))
            result = g_strdup_printf("%s (#%s)", caption ? caption : insight_tr("Exception catchpoint"), number);
        g_free(caption);
        g_free(kind);
        g_free(id);
    }
    g_free(number);
    return result;
}
