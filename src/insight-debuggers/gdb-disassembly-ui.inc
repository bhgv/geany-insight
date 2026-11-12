/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Show bounded instructions, register snapshots and paused-only machine steps.
 * RU: Показывает ограниченный машинный код, снимки регистров и машинные шаги на паузе. */

/* EN: Execute button's refresh/step action for session data only while stopped and idle.
 * RU: Выполняет обновление/шаг кнопки button для session data только на паузе без ожидающих команд. */
static void native_assembly_action(GtkWidget *button, gpointer data)
{
    NativeDebug *session = data;
    NativeAssembly *assembly = session->assembly;
    if (assembly->active || !session->ready || !session->stopped || session->pending || session->writing ||
        (session->remote && (session->remote->busy || session->remote->release_pending)))
        return;
    const gchar *step = g_object_get_data(G_OBJECT(button), "instruction-step");
    gchar *command = step ? g_strdup(step) : native_assembly_command(
        gtk_entry_get_text(GTK_ENTRY(assembly->expression)));
    if (!command)
    {
        gtk_label_set_text(GTK_LABEL(assembly->status), insight_tr("Enter a single-line address expression (up to 512 characters)."));
        return;
    }
    if (!step)
    {
        native_assembly_prepare(session, command, gtk_entry_get_text(GTK_ENTRY(assembly->expression)));
        gtk_label_set_text(GTK_LABEL(assembly->status), insight_tr("Reading machine instructions…"));
    }
    if (step)
    {
        native_debug_send(session->window, command);
        g_free(command);
    }
    else
        native_debug_resume(session->window, session);
}

/* EN: Create the instruction page for session; expression defaults to the selected frame's program counter.
 * RU: Создаёт страницу инструкций session; выражение по умолчанию — счётчик команд выбранного кадра. */
static GtkWidget *native_assembly_widget(NativeDebug *session)
{
    NativeAssembly *assembly = g_new0(NativeAssembly, 1);
    session->assembly = assembly;
    assembly->instruction_points = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    assembly->instruction_marks = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    assembly->rules = g_ptr_array_new_with_free_func(native_name_rule_free);
    assembly->ranges = g_ptr_array_new_with_free_func(native_name_range_free);
    assembly->objects = g_ptr_array_new_with_free_func(g_free);
    assembly->registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    assembly->page = page;
    GtkWidget *label = gtk_label_new(insight_tr("Address expression ($pc, function or 0x…):"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(page), label, FALSE, FALSE, 0);
    assembly->expression = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(assembly->expression), 512);
    gtk_entry_set_text(GTK_ENTRY(assembly->expression), "$pc");
    gtk_widget_set_name(assembly->expression, "insight-gdb-instruction-address");
    g_signal_connect(assembly->expression, "activate", G_CALLBACK(native_assembly_action), session);
    g_ptr_array_add(session->paused_controls, assembly->expression);
    gtk_box_pack_start(GTK_BOX(page), assembly->expression, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), native_navigation_widget(session), FALSE, FALSE, 0);
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
    const gchar *actions[] = {"Toggle instruction breakpoint (F2)", "View instruction address in memory", "Run to instruction"};
    for (guint i = 0; i < G_N_ELEMENTS(actions); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(actions[i]));
        g_object_set_data(G_OBJECT(button), "instruction-action", GUINT_TO_POINTER(i));
        g_signal_connect(button, "clicked", G_CALLBACK(native_instruction_action), session);
        g_ptr_array_add(session->paused_controls, button);
        gtk_flow_box_insert(GTK_FLOW_BOX(bar), button, -1);
    }
    gtk_box_pack_start(GTK_BOX(page), bar, FALSE, FALSE, 0);
    assembly->follow = gtk_check_button_new_with_label(insight_tr("Follow the selected frame at each stop"));
    gtk_widget_set_name(assembly->follow, "insight-gdb-follow-instructions");
    gtk_widget_set_tooltip_text(assembly->follow, insight_tr("While this page is visible, refresh $pc and names after stops and frame changes. Manual address reads remain available."));
    g_signal_connect(assembly->follow, "toggled", G_CALLBACK(native_assembly_follow_changed), session);
    g_signal_connect(session->tabs, "switch-page", G_CALLBACK(native_assembly_follow_page), session);
    gtk_box_pack_start(GTK_BOX(page), assembly->follow, FALSE, FALSE, 0);
    assembly->bytes_toggle = gtk_check_button_new_with_label(insight_tr("Show instruction bytes"));
    gtk_widget_set_name(assembly->bytes_toggle, "insight-gdb-show-bytes");
    g_signal_connect(assembly->bytes_toggle, "toggled", G_CALLBACK(native_opcode_toggled), session);
    gtk_box_pack_start(GTK_BOX(page), assembly->bytes_toggle, FALSE, FALSE, 0);
    assembly->status = gtk_label_new(insight_tr("Pause the program, then read instructions. This is a snapshot, not a live view."));
    gtk_widget_set_name(assembly->status, "insight-gdb-instruction-status");
    gtk_label_set_line_wrap(GTK_LABEL(assembly->status), TRUE);
    gtk_box_pack_start(GTK_BOX(page), assembly->status, FALSE, FALSE, 0);
    GtkWidget *view = gtk_text_view_new();
    assembly->text = view;
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), TRUE);
    g_signal_connect(view, "key-press-event", G_CALLBACK(native_instruction_key), session);
    g_signal_connect(view, "key-press-event", G_CALLBACK(native_navigation_key), session);
    g_signal_connect(view, "button-press-event", G_CALLBACK(native_instruction_mouse), session);
    g_signal_connect(view, "populate-popup", G_CALLBACK(native_instruction_popup), session);
    gtk_widget_set_name(view, "insight-gdb-instructions");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    GtkWidget *registers = gtk_expander_new(insight_tr("Registers at the last instruction read"));
    gtk_widget_set_name(registers, "insight-gdb-registers-panel");
    gtk_container_add(GTK_CONTAINER(registers), native_assembly_register_widget(session));
    gtk_box_pack_start(GTK_BOX(page), registers, FALSE, FALSE, 0);
    GtkWidget *names = gtk_expander_new(insight_tr("Names, frame offsets and data types"));
    gtk_widget_set_name(names, "insight-gdb-name-rules");
    gtk_container_add(GTK_CONTAINER(names), native_name_widget(session));
    gtk_box_pack_start(GTK_BOX(page), names, FALSE, FALSE, 0);
    return page;
}
