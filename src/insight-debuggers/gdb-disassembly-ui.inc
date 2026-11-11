/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Show bounded machine instructions and provide paused-only instruction steps.
 * RU: Показывает ограниченный фрагмент машинного кода и пошаговое выполнение инструкций на паузе. */

/* EN: Execute button's refresh/step action for session data only while stopped and idle.
 * RU: Выполняет обновление/шаг кнопки button для session data только на паузе без ожидающих команд. */
static void native_assembly_action(GtkWidget *button, gpointer data)
{
    NativeDebug *session = data;
    if (!session->ready || !session->stopped || session->pending || session->writing ||
        (session->remote && (session->remote->busy || session->remote->release_pending)))
        return;
    const gchar *step = g_object_get_data(G_OBJECT(button), "instruction-step");
    gchar *command = step ? g_strdup(step) : native_assembly_command(
        gtk_entry_get_text(GTK_ENTRY(session->assembly_expression)));
    if (!command)
    {
        gtk_label_set_text(GTK_LABEL(session->assembly_status), insight_tr("Enter a single-line address expression (up to 512 characters)."));
        return;
    }
    if (!step)
    {
        session->assembly_pending = TRUE;
        gtk_label_set_text(GTK_LABEL(session->assembly_status), insight_tr("Reading machine instructions…"));
    }
    native_debug_send(session->window, command);
    g_free(command);
}

/* EN: Consume only session's pending disassembly reply record; errors remain visible in the page and console.
 * RU: Обрабатывает record только ожидающего запроса session; ошибки видны на странице и в консоли. */
static gboolean native_assembly_reply(NativeDebug *session, const gchar *record)
{
    if (!session->assembly_pending)
        return FALSE;
    session->assembly_pending = FALSE;
    gchar *text = native_assembly_format(record);
    gchar *error = text ? NULL : native_debug_field(record, "msg");
    debug_set_text(session->assembly_text, text ? text : "");
    gtk_label_set_text(GTK_LABEL(session->assembly_status), text ?
        insight_tr("Instruction snapshot: 128 bytes from the requested address. Refresh after stepping or changing frames.") :
        error ? error : insight_tr("No readable machine instructions at this address."));
    g_free(error);
    g_free(text);
    return FALSE;
}

/* EN: Create the instruction page for session; expression defaults to the selected frame's program counter.
 * RU: Создаёт страницу инструкций session; выражение по умолчанию — счётчик команд выбранного кадра. */
static GtkWidget *native_assembly_widget(NativeDebug *session)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *label = gtk_label_new(insight_tr("Address expression ($pc, function or 0x…):"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(page), label, FALSE, FALSE, 0);
    session->assembly_expression = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(session->assembly_expression), 512);
    gtk_entry_set_text(GTK_ENTRY(session->assembly_expression), "$pc");
    gtk_widget_set_name(session->assembly_expression, "insight-gdb-instruction-address");
    g_signal_connect(session->assembly_expression, "activate", G_CALLBACK(native_assembly_action), session);
    g_ptr_array_add(session->paused_controls, session->assembly_expression);
    gtk_box_pack_start(GTK_BOX(page), session->assembly_expression, FALSE, FALSE, 0);
    GtkWidget *bar = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(bar), GTK_SELECTION_NONE);
    const gchar *labels[] = {"Read instructions", "Step instruction", "Next instruction"};
    const gchar *commands[] = {NULL, "-exec-step-instruction", "-exec-next-instruction"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        g_object_set_data(G_OBJECT(button), "instruction-step", (gpointer)commands[i]);
        g_signal_connect(button, "clicked", G_CALLBACK(native_assembly_action), session);
        g_ptr_array_add(session->paused_controls, button);
        gtk_flow_box_insert(GTK_FLOW_BOX(bar), button, -1);
    }
    gtk_box_pack_start(GTK_BOX(page), bar, FALSE, FALSE, 0);
    session->assembly_status = gtk_label_new(insight_tr("Pause the program, then read instructions. This is a snapshot, not a live view."));
    gtk_label_set_line_wrap(GTK_LABEL(session->assembly_status), TRUE);
    gtk_box_pack_start(GTK_BOX(page), session->assembly_status, FALSE, FALSE, 0);
    GtkWidget *view = gtk_text_view_new();
    session->assembly_text = GTK_TEXT_VIEW(view);
    gtk_widget_set_name(view, "insight-gdb-instructions");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    return page;
}
