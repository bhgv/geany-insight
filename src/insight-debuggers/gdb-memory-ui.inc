/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Paused-only memory requests, navigation and snapshot lifetime for the GDB panel.
 * RU: Запросы памяти на паузе, навигация и срок жизни снимков панели GDB. */

/* EN: Invalidate session's memory snapshot when its value context changes; optionally queue a fresh read.
 * RU: Сбрасывает снимок памяти session при смене контекста; при необходимости запрашивает новый. */
static void native_memory_sync(NativeDebug *session)
{
    if (!session->memory_text || session->memory_generation == session->value_generation)
        return;
    session->memory_generation = session->value_generation;
    session->memory_needed = session->stopped &&
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(session->memory_auto)) &&
        *gtk_entry_get_text(GTK_ENTRY(session->memory_expression));
    g_object_set_data(G_OBJECT(session->memory_text), "native-memory-raw", NULL);
    g_clear_pointer(&session->memory_address, g_free);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(session->memory_text)), "", -1);
    gtk_label_set_text(GTK_LABEL(session->memory_status),
        insight_tr("Snapshot cleared. Pause and read memory in the selected frame."));
}

/* EN: Return session's queued memory read command, owned by the caller, or NULL if none is pending.
 * RU: Возвращает ожидающую команду чтения session, принадлежащую вызывающему, либо NULL. */
static gchar *native_memory_next(NativeDebug *session)
{
    if (!session->memory_needed || !session->stopped)
        return NULL;
    session->memory_needed = FALSE;
    const gchar *expression = gtk_entry_get_text(GTK_ENTRY(session->memory_expression));
    if (!*expression)
        return NULL;
    session->memory_count = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(session->memory_size));
    gchar *escaped = g_strescape(expression, NULL);
    gchar *command = g_strdup_printf("-data-read-memory-bytes \"%s\" %u", escaped, session->memory_count);
    g_free(escaped);
    session->memory_pending = TRUE;
    session->memory_pending_generation = session->value_generation;
    gtk_label_set_text(GTK_LABEL(session->memory_status), insight_tr("Reading memory…"));
    return command;
}

/* EN: Consume record for session's memory read; discard obsolete frame replies and clear invalid snapshots.
 * Navigation uses the validated snapshot's first displayed address, regardless of MI fragment order.
 * Return TRUE when the memory request consumed the reply, otherwise FALSE.
 * RU: Принимает record чтения памяти session; отбрасывает устаревшие кадры и очищает неверные снимки.
 * Навигация использует первый адрес проверенного снимка независимо от порядка фрагментов MI.
 * Возвращает TRUE, если ответ принадлежал запросу памяти, иначе FALSE. */
static gboolean native_memory_reply(NativeDebug *session, const gchar *record)
{
    if (!session->memory_pending)
        return FALSE;
    session->memory_pending = FALSE;
    if (!session->stopped || session->memory_pending_generation != session->value_generation)
        return TRUE;
    gchar *text = native_memory_format(record, session->memory_count);
    gchar *message = text ? NULL : native_debug_field(record, "msg");
    gchar *valid = message ? g_utf8_make_valid(message, -1) : NULL;
    g_object_set_data_full(G_OBJECT(session->memory_text), "native-memory-raw", g_strdup(text), g_free);
    gchar *named = native_name_line_addresses(session, text);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(session->memory_text)), named, -1);
    g_free(named);
    gtk_label_set_text(GTK_LABEL(session->memory_status), text ?
        insight_tr("Memory at the current stop; byte order is unchanged.") : valid ? valid :
        insight_tr("Memory unavailable or incomplete. Try a smaller block or another address."));
    g_clear_pointer(&session->memory_address, g_free);
    if (text)
        session->memory_address = g_strndup(text, strcspn(text, " \n"));
    g_free(valid);
    g_free(message);
    g_free(text);
    return TRUE;
}

/* EN: Queue a read in session data from button; navigate only from the last confirmed numeric address.
 * RU: Запрашивает чтение сеанса data по button; перемещается только от подтверждённого числового адреса. */
static void native_memory_read(GtkButton *button, gpointer data)
{
    NativeDebug *session = data;
    if (!session->ready || !session->stopped || session->pending || session->writing)
        return;
    gint direction = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "memory-direction"));
    if (direction)
    {
        guint64 address = 0;
        guint count = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(session->memory_size));
        if (!session->memory_address || !native_memory_address(session->memory_address, &address) ||
            (direction < 0 ? address < count : address > G_MAXUINT64 - count))
        {
            gtk_label_set_text(GTK_LABEL(session->memory_status),
                insight_tr("Read a valid address before navigating; address overflow is not allowed."));
            return;
        }
        address = direction < 0 ? address - count : address + count;
        gchar *expression = g_strdup_printf("0x%" G_GINT64_MODIFIER "x", address);
        gtk_entry_set_text(GTK_ENTRY(session->memory_expression), expression);
        g_free(expression);
    }
    session->memory_needed = TRUE;
    native_debug_resume(session->window, session);
}

/* EN: Open session's memory tab using expression as the address; read it when the command queue is idle.
 * RU: Открывает вкладку памяти session с адресом expression; читает после освобождения очереди команд. */
static void native_memory_inspect(NativeDebug *session, const gchar *expression)
{
    gtk_entry_set_text(GTK_ENTRY(session->memory_expression), expression);
    GtkWidget *page = gtk_widget_get_parent(session->memory_status);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(session->tabs),
        gtk_notebook_page_num(GTK_NOTEBOOK(session->tabs), page));
    session->memory_needed = TRUE;
    native_debug_resume(session->window, session);
}

/* EN: Build session's memory tab with byte count, refresh policy and a noneditable hex/ASCII snapshot.
 * RU: Создаёт вкладку памяти session с числом байтов, обновлением и неизменяемым снимком hex/ASCII. */
static GtkWidget *native_memory_widget(NativeDebug *session)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *bar = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(bar), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(bar), gtk_label_new(insight_tr("Address expression")));
    session->memory_expression = gtk_entry_new();
    gtk_widget_set_name(session->memory_expression, "insight-gdb-memory-address");
    gtk_entry_set_max_length(GTK_ENTRY(session->memory_expression), 1024);
    gtk_entry_set_placeholder_text(GTK_ENTRY(session->memory_expression), "&variable / pointer / 0x1000");
    gtk_entry_set_width_chars(GTK_ENTRY(session->memory_expression), 30);
    gtk_container_add(GTK_CONTAINER(bar), session->memory_expression);
    gtk_container_add(GTK_CONTAINER(bar), gtk_label_new(insight_tr("Bytes to read")));
    session->memory_size = gtk_spin_button_new_with_range(1, 4096, 16);
    gtk_widget_set_name(session->memory_size, "insight-gdb-memory-size");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(session->memory_size), 256);
    gtk_container_add(GTK_CONTAINER(bar), session->memory_size);
    const gchar *labels[] = {"Read Memory", "Previous block", "Next block"};
    const gint directions[] = {0, -1, 1};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        g_object_set_data(G_OBJECT(button), "memory-direction", GINT_TO_POINTER(directions[i]));
        g_signal_connect(button, "clicked", G_CALLBACK(native_memory_read), session);
        gtk_container_add(GTK_CONTAINER(bar), button);
        if (!i)
            g_signal_connect_swapped(session->memory_expression, "activate", G_CALLBACK(gtk_button_clicked), button);
    }
    session->memory_auto = gtk_check_button_new_with_label(insight_tr("Refresh memory at each stop"));
    gtk_container_add(GTK_CONTAINER(bar), session->memory_auto);
    gtk_box_pack_start(GTK_BOX(page), bar, FALSE, FALSE, 0);
    g_ptr_array_add(session->paused_controls, bar);
    GtkWidget *hint = gtk_label_new(insight_tr(
        "Use &variable for storage, a pointer for its target. Expressions may execute code in the program."));
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_pack_start(GTK_BOX(page), hint, FALSE, FALSE, 0);
    session->memory_text = gtk_text_view_new();
    gtk_widget_set_name(session->memory_text, "insight-gdb-memory-bytes");
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(session->memory_text), TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(session->memory_text), FALSE);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), session->memory_text);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    session->memory_status = gtk_label_new(insight_tr("Pause and read up to 4096 bytes from an address expression."));
    gtk_widget_set_name(session->memory_status, "insight-gdb-memory-status");
    gtk_label_set_line_wrap(GTK_LABEL(session->memory_status), TRUE);
    gtk_box_pack_start(GTK_BOX(page), session->memory_status, FALSE, FALSE, 0);
    return page;
}
