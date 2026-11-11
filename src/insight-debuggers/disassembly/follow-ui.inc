/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Follow the selected frame's program counter after a confirmed stop without interrupting execution.
 * RU: Следует за счётчиком команд выбранного кадра после подтверждённой остановки без прерывания выполнения. */

/* EN: Start session's optional PC snapshot only after frame, locals and watches settle; never delay queued execution.
 * RU: Запускает необязательный снимок PC session после кадра, переменных и наблюдений, не задерживая очередь исполнения. */
static void native_assembly_follow(NativeDebug *session)
{
    NativeAssembly *assembly = session->assembly;
    if (!assembly || !assembly->follow_needed || !assembly->visible || !assembly->follow ||
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(assembly->follow)) ||
        !session->ready || !session->stopped || session->pending || session->writing ||
        session->resume || session->stop_requested || !g_queue_is_empty(&session->launch_commands) ||
        session->threads_needed || session->stack_needed || session->frame_select_needed ||
        session->locals_needed || session->locals_pending || session->watch_pending ||
        session->watch_next < session->expressions->len || session->context_pending != NATIVE_CONTEXT_NONE ||
        !native_context_id(session->selected_thread) || !native_context_id(session->selected_frame) ||
        (session->remote && session->remote->busy))
        return;
    gtk_entry_set_text(GTK_ENTRY(assembly->expression), "$pc");
    gtk_label_set_text(GTK_LABEL(assembly->status), insight_tr("Following the selected frame's program counter…"));
    native_assembly_prepare(session, native_assembly_command("$pc"), "$pc");
}

/* EN: Request or cancel future automatic snapshots for session data when button changes; an in-flight read finishes normally.
 * RU: Запрашивает или отменяет будущие автоматические снимки session data по button; начатое чтение завершается штатно. */
static void native_assembly_follow_changed(GtkToggleButton *button, gpointer data)
{
    NativeDebug *session = data;
    session->assembly->follow_enabled = gtk_toggle_button_get_active(button);
    session->assembly->follow_needed = session->assembly->follow_enabled;
    native_debug_resume(session->window, session);
}

/* EN: Track whether page is the instruction page of session data; notebook and number are unused.
 * RU: Учитывает видимость страницы инструкций page в session data; notebook и number не используются. */
static void native_assembly_follow_page(GtkNotebook *notebook, GtkWidget *page, guint number, gpointer data)
{
    (void)notebook;
    (void)number;
    NativeDebug *session = data;
    NativeAssembly *assembly = session->assembly;
    assembly->visible = page == assembly->page;
    if (assembly->visible)
    {
        assembly->follow_needed = TRUE;
        native_debug_resume(session->window, session);
    }
}
