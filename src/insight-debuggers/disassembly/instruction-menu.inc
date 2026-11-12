/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Provide mouse and keyboard menus for instruction addresses, reusing shared breakpoint and naming editors.
 * RU: Предоставляет меню адресов инструкций для мыши и клавиатуры с общими редакторами точек и имён. */

/* EN: Edit item’s confirmed address breakpoint in session data through the shared point editor; other source rules stay intact.
 * RU: Редактирует подтверждённую адресную точку item в session data общим редактором; правила исходников сохраняются. */
static void native_instruction_edit(GtkWidget *item, gpointer data)
{
    NativeDebug *session = data;
    if (!native_instruction_idle(session))
        return;
    const gchar *address = g_object_get_data(G_OBJECT(item), "instruction-address");
    const gchar *number = g_hash_table_lookup(session->assembly->instruction_points, address);
    if (!number)
        return;
    GtkTreeModel *model = GTK_TREE_MODEL(session->point_rows);
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *candidate = NULL;
        gtk_tree_model_get(model, &row, 1, &candidate, -1);
        gboolean matches = !g_strcmp0(candidate, number);
        g_free(candidate);
        if (matches)
        {
            gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(session->points_view)), &row);
            native_point_edit(item, session->window);
            return;
        }
    }
}

/* EN: Copy item's captured instruction address to the clipboard; data is unused and the inferior is never queried.
 * RU: Копирует сохранённый адрес инструкции item в буфер обмена; data не используется, запросов к программе нет. */
static void native_instruction_copy(GtkWidget *item, gpointer data)
{
    (void)data;
    const gchar *address = g_object_get_data(G_OBJECT(item), "instruction-address");
    if (address)
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), address, -1);
}

/* EN: Name item's captured address in session data using the normal validated, profile-backed name editor.
 * RU: Задаёт имя сохранённого адреса item в session data обычным проверяемым редактором имён с сохранением в профиль. */
static void native_instruction_name(GtkWidget *item, gpointer data)
{
    NativeDebug *session = data;
    if (native_instruction_idle(session))
        native_name_edit(item, session);
}

/* EN: Append a translated item to menu, copying address and connecting callback/session; return the menu-owned item.
 * RU: Добавляет переведённый пункт в menu, копирует address и подключает callback/session; возвращает пункт, принадлежащий меню. */
static GtkWidget *native_instruction_menu_item(GtkWidget *menu, const gchar *label, const gchar *address,
                                               GCallback callback, NativeDebug *session, gboolean sensitive)
{
    GtkWidget *item = gtk_menu_item_new_with_label(insight_tr(label));
    g_object_set_data_full(G_OBJECT(item), "instruction-address", g_strdup(address), g_free);
    g_signal_connect(item, "activate", callback, session);
    gtk_widget_set_sensitive(item, sensitive);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
    return item;
}

/* EN: Append cursor actions to the standard text menu of view for session data; capture the address before the cursor changes.
 * RU: Добавляет действия курсора в стандартное текстовое меню view для session data; сохраняет адрес до смены курсора. */
static void native_instruction_popup(GtkTextView *view, GtkWidget *menu, gpointer data)
{
    (void)view;
    NativeDebug *session = data;
    if (!GTK_IS_MENU_SHELL(menu))
        return;
    gchar *address = native_instruction_cursor(session);
    if (!address)
        return;
    gboolean idle = native_instruction_idle(session);
    gboolean point = g_hash_table_contains(session->assembly->instruction_points, address);
    GtkWidget *separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    gtk_widget_show(separator);
    const gchar *actions[] = {"Toggle instruction breakpoint (F2)", "View instruction address in memory",
        "Run to instruction", "Add hardware instruction breakpoint"};
    for (guint i = 0; i < G_N_ELEMENTS(actions); i++)
    {
        GtkWidget *item = native_instruction_menu_item(menu, actions[i], address,
            G_CALLBACK(native_instruction_action), session, idle && (i != 3 || !point));
        g_object_set_data(G_OBJECT(item), "instruction-action", GUINT_TO_POINTER(i));
    }
    native_instruction_menu_item(menu, "Open source for instruction", address,
        G_CALLBACK(native_instruction_source), session, idle);
    const gchar *labels[] = {"Enable / Disable", "Breakpoint condition", "Ignore first hits"};
    const gchar *edits[] = {"toggle", "condition", "ignore"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *item = native_instruction_menu_item(menu, labels[i], address,
            G_CALLBACK(native_instruction_edit), session, idle && point);
        g_object_set_data(G_OBJECT(item), "point-action", (gpointer)edits[i]);
    }
    GtkWidget *name = native_instruction_menu_item(menu, "Name this instruction address…", address,
        G_CALLBACK(native_instruction_name), session, idle);
    g_object_set_data_full(G_OBJECT(name), "name-anchor", g_strdup(address), g_free);
    native_instruction_menu_item(menu, "Copy instruction address", address,
        G_CALLBACK(native_instruction_copy), session, TRUE);
    gchar *bytes = native_opcode_at(session->assembly, address);
    GtkWidget *copy_bytes = native_instruction_menu_item(menu, "Copy instruction bytes", address,
        G_CALLBACK(native_opcode_copy), session, bytes != NULL);
    g_object_set_data_full(G_OBJECT(copy_bytes), "instruction-copy-text", bytes, g_free);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(session->assembly->text));
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));
    gtk_text_iter_set_line_offset(&start, 0);
    end = start;
    gtk_text_iter_forward_to_line_end(&end);
    gchar *line = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    const gchar *plain = line;
    if (g_str_has_prefix(plain, "● ") || g_str_has_prefix(plain, "○ "))
        plain += strlen("● ");
    if (g_str_has_prefix(plain, "▶ "))
        plain += strlen("▶ ");
    GtkWidget *copy_line = native_instruction_menu_item(menu, "Copy instruction line", address,
        G_CALLBACK(native_opcode_copy), session, TRUE);
    g_object_set_data_full(G_OBJECT(copy_line), "instruction-copy-text", g_strdup(plain), g_free);
    g_free(line);
    g_free(address);
}

/* EN: Position view's cursor at right-click or address-column double-click event for session data; leave operand selection unchanged.
 * RU: Ставит курсор view на правом щелчке или двойном щелчке по адресу event для session data; выделение операндов сохраняется. */
static gboolean native_instruction_mouse(GtkWidget *view, GdkEventButton *event, gpointer data)
{
    if (event->button != 3 && !(event->button == 1 && event->type == GDK_2BUTTON_PRESS))
        return FALSE;
    if (event->button == 1 && (event->state & gtk_accelerator_get_default_mod_mask()))
        return FALSE;
    gint x, y;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT,
        (gint)event->x, (gint)event->y, &x, &y);
    GtkTextIter at, start, end;
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(view), &at, x, y);
    start = at;
    gtk_text_iter_set_line_offset(&start, 0);
    end = start;
    gtk_text_iter_forward_to_line_end(&end);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gchar *line = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gchar *address = native_instruction_address(line);
    const gchar *digits = address ? strstr(line, "0x") : NULL;
    const gchar *last = digits ? digits + 2 : NULL;
    while (last && g_ascii_isxdigit(*last))
        last++;
    gboolean address_column = last && gtk_text_iter_get_line_offset(&at) < g_utf8_pointer_to_offset(line, last);
    g_free(address);
    g_free(line);
    if (event->button == 3 || address_column)
        gtk_text_buffer_place_cursor(buffer, &at);
    if (event->button == 1 && address_column)
    {
        native_instruction_action(view, data);
        return TRUE;
    }
    return FALSE;
}
