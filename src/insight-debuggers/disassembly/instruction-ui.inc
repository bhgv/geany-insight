/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Bind instruction cursor actions and visible breakpoint/selected-frame markers to confirmed GDB state.
 * RU: Связывает действия строки инструкций и маркеры точек/выбранного кадра с подтверждённым состоянием GDB. */

/* EN: Repaint session's last instruction snapshot, preserving cursor row; confirmed points are red, disabled ones gray.
 * RU: Перерисовывает снимок инструкций session с сохранением строки курсора; подтверждённые точки красные, отключённые серые. */
static void native_instruction_refresh(NativeDebug *session)
{
    NativeAssembly *assembly = session->assembly;
    if (!assembly || !assembly->text || !assembly->record)
        return;
    gchar *raw = native_assembly_render(session, assembly->record);
    if (!raw)
        return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(assembly->text));
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    gint row = gtk_text_iter_get_line(&cursor);
    gchar **lines = g_strsplit(raw, "\n", -1);
    GString *text = g_string_new(NULL);
    for (guint i = 0; lines[i]; i++)
    {
        gchar *address = native_instruction_address(lines[i]);
        guint point = address && assembly->instruction_marks ?
            GPOINTER_TO_UINT(g_hash_table_lookup(assembly->instruction_marks, address)) : 0;
        guint64 numeric = 0;
        gboolean current = assembly->instruction_pc_valid && address &&
            native_assembly_number(address, &numeric) && numeric == assembly->instruction_pc;
        g_string_append(text, point == 2 ? "● " : point == 1 ? "○ " : "");
        if (current)
            g_string_append(text, "▶ ");
        g_string_append(text, lines[i]);
        if (lines[i + 1])
            g_string_append_c(text, '\n');
        g_free(address);
    }
    gtk_text_buffer_set_text(buffer, text->str, -1);
    GtkTextTagTable *tags = gtk_text_buffer_get_tag_table(buffer);
    if (!gtk_text_tag_table_lookup(tags, "instruction-point"))
    {
        gtk_text_buffer_create_tag(buffer, "instruction-point", "foreground", "#c62828", NULL);
        gtk_text_buffer_create_tag(buffer, "instruction-disabled", "foreground", "#777777", NULL);
        gtk_text_buffer_create_tag(buffer, "instruction-current", "background", "#fff2ad", "foreground", "#202020", NULL);
    }
    gtk_text_buffer_get_start_iter(buffer, &cursor);
    do
    {
        GtkTextIter end = cursor;
        gtk_text_iter_forward_to_line_end(&end);
        gchar *line = gtk_text_buffer_get_text(buffer, &cursor, &end, FALSE);
        if (g_str_has_prefix(line, "● ") || g_str_has_prefix(line, "○ "))
        {
            GtkTextIter dot_end = cursor;
            gtk_text_iter_forward_char(&dot_end);
            gtk_text_buffer_apply_tag_by_name(buffer, g_str_has_prefix(line, "● ") ? "instruction-point" : "instruction-disabled", &cursor, &dot_end);
        }
        if (g_str_has_prefix(line, "▶ ") || g_str_has_prefix(line, "● ▶ ") || g_str_has_prefix(line, "○ ▶ "))
            gtk_text_buffer_apply_tag_by_name(buffer, "instruction-current", &cursor, &end);
        g_free(line);
    } while (gtk_text_iter_forward_line(&cursor));
    gtk_text_buffer_get_iter_at_line(buffer, &cursor, MIN(row, gtk_text_buffer_get_line_count(buffer) - 1));
    gtk_text_buffer_place_cursor(buffer, &cursor);
    g_string_free(text, TRUE);
    g_strfreev(lines);
    g_free(raw);
}

/* EN: Use session data's instruction cursor for button action: 0 toggle address breakpoint, 1 memory, 2 run until address.
 * RU: Использует строку курсора session data для действия button: 0 точка адреса, 1 память, 2 выполнение до адреса. */
static void native_instruction_action(GtkWidget *button, gpointer data)
{
    NativeDebug *session = data;
    NativeAssembly *assembly = session->assembly;
    if (!session->ready || !session->stopped || session->pending || session->writing || assembly->active ||
        session->points_needed || session->points_pending || session->stop_requested ||
        !g_queue_is_empty(&session->launch_commands) ||
        (session->remote && (session->remote->busy || session->remote->release_pending)))
        return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(assembly->text));
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));
    gtk_text_iter_set_line_offset(&start, 0);
    end = start;
    gtk_text_iter_forward_to_line_end(&end);
    gchar *line = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gchar *address = native_instruction_address(line);
    g_free(line);
    if (!address)
    {
        gtk_label_set_text(GTK_LABEL(assembly->status), insight_tr("Place the cursor on an instruction row first."));
        return;
    }
    guint action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "instruction-action"));
    gchar *command = NULL;
    if (action == 1)
        native_memory_inspect(session, address);
    else if (action == 2)
    {
        guint64 numeric = 0;
        native_assembly_number(address, &numeric);
        if (assembly->instruction_pc_valid && numeric == assembly->instruction_pc)
            gtk_label_set_text(GTK_LABEL(assembly->status), insight_tr("The selected frame is already at this instruction."));
        else
            command = g_strdup_printf("-exec-until \"*%s\"", address);
    }
    else
    {
        const gchar *number = g_hash_table_lookup(assembly->instruction_points, address);
        command = number ? g_strdup_printf("-break-delete %s", number) :
            g_strdup_printf("-break-insert -- \"*%s\"", address);
        session->points_needed = TRUE;
    }
    if (command)
        native_debug_send(session->window, command);
    g_free(command);
    g_free(address);
}

/* EN: Toggle the instruction under the cursor on unmodified F2; view is the action owner, data is session. Other keys propagate.
 * RU: Переключает инструкцию под курсором по F2 без модификаторов; view владеет действием, data — сеанс. Прочие клавиши передаются дальше. */
static gboolean native_instruction_key(GtkWidget *view, GdkEventKey *event, gpointer data)
{
    if (event->keyval != GDK_KEY_F2 || (event->state & gtk_accelerator_get_default_mod_mask()))
        return FALSE;
    native_instruction_action(view, data);
    return TRUE;
}
