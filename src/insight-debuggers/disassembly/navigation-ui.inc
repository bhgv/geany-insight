/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Navigate confirmed code history and explicit address selections without executing the inferior.
 * RU: Перемещается по истории кода и явно выделенным адресам без исполнения отлаживаемой программы. */

/* EN: Enable session's history controls only for available confirmed entries in an idle paused session.
 * RU: Включает кнопки истории session только для существующих подтверждённых записей на свободной паузе. */
static void native_navigation_controls(NativeDebug *session)
{
    NativeAssembly *assembly = session->assembly;
    if (!assembly || !assembly->history_back)
        return;
    gboolean idle = native_instruction_idle(session);
    guint count = assembly->history ? assembly->history->len : 0;
    gtk_widget_set_sensitive(assembly->history_back, idle && count && assembly->history_position > 0);
    gtk_widget_set_sensitive(assembly->history_forward, idle && count && assembly->history_position + 1 < count);
}

/* EN: Read the address requested by button's action for session data; history movement commits only after successful disassembly.
 * RU: Читает адрес по действию button для session data; переход истории подтверждается только после успешного дизассемблирования. */
static void native_navigation_action(GtkWidget *button, gpointer data)
{
    NativeDebug *session = data;
    NativeAssembly *assembly = session->assembly;
    if (!native_instruction_idle(session))
        return;
    guint action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "navigation-action"));
    guint position = assembly->history_position;
    gchar *address = NULL;
    if (action < 2)
    {
        guint count = assembly->history ? assembly->history->len : 0;
        if (!count || (!action && !position) || (action == 1 && position + 1 >= count))
            return;
        position = action ? position + 1 : position - 1;
        address = g_strdup(assembly->history->pdata[position]);
    }
    else if (action == 2)
        address = g_strdup("$pc");
    else
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(assembly->text));
        GtkTextIter start, end;
        if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end) &&
            gtk_text_iter_get_offset(&end) - gtk_text_iter_get_offset(&start) <= 512)
        {
            gchar *selected = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
            guint64 value = 0;
            g_strstrip(selected);
            if (g_str_has_prefix(selected, "0x") &&
                g_ascii_string_to_unsigned(selected + 2, 16, 0, G_MAXUINT64, &value, NULL))
                address = g_strdup_printf("0x%" G_GINT64_MODIFIER "x", value);
            g_free(selected);
        }
    }
    if (!address)
    {
        gtk_label_set_text(GTK_LABEL(assembly->status), insight_tr("Select one hexadecimal address to open as code."));
        return;
    }
    gchar *command = native_assembly_command(address);
    gtk_entry_set_text(GTK_ENTRY(assembly->expression), address);
    native_assembly_prepare(session, command, address);
    assembly->history_replaying = action < 2;
    assembly->history_requested = position;
    gtk_label_set_text(GTK_LABEL(assembly->status), insight_tr("Reading machine instructions…"));
    native_debug_resume(session->window, session);
    g_free(address);
}

/* EN: Handle Alt+Left/Right in view for session data and consume only these navigation keys; other keys propagate.
 * RU: Обрабатывает Alt+Left/Right в view для session data, поглощая только клавиши навигации; остальные передаются дальше. */
static gboolean native_navigation_key(GtkWidget *view, GdkEventKey *event, gpointer data)
{
    if ((event->state & gtk_accelerator_get_default_mod_mask()) != GDK_MOD1_MASK ||
        (event->keyval != GDK_KEY_Left && event->keyval != GDK_KEY_Right))
        return FALSE;
    g_object_set_data(G_OBJECT(view), "navigation-action", GUINT_TO_POINTER(event->keyval == GDK_KEY_Right));
    native_navigation_action(view, data);
    return TRUE;
}

/* EN: Build session's translated navigation row; GTK owns the returned container and its buttons.
 * RU: Создаёт переведённую строку навигации session; GTK владеет возвращённым контейнером и кнопками. */
static GtkWidget *native_navigation_widget(NativeDebug *session)
{
    GtkWidget *bar = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(bar), GTK_SELECTION_NONE);
    const gchar *labels[] = {"Code history back", "Code history forward", "Return to program counter", "Open selected address as code"};
    const gchar *names[] = {"insight-gdb-code-back", "insight-gdb-code-forward", "insight-gdb-code-pc", "insight-gdb-code-selected"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        gtk_widget_set_name(button, names[i]);
        g_object_set_data(G_OBJECT(button), "navigation-action", GUINT_TO_POINTER(i));
        g_signal_connect(button, "clicked", G_CALLBACK(native_navigation_action), session);
        if (i < 2)
        {
            gchar *shortcut = gtk_accelerator_get_label(i ? GDK_KEY_Right : GDK_KEY_Left, GDK_MOD1_MASK);
            gtk_widget_set_tooltip_text(button, shortcut);
            g_free(shortcut);
        }
        if (i == 0)
            session->assembly->history_back = button;
        else if (i == 1)
            session->assembly->history_forward = button;
        else
            g_ptr_array_add(session->paused_controls, button);
        gtk_flow_box_insert(GTK_FLOW_BOX(bar), button, -1);
    }
    return bar;
}
