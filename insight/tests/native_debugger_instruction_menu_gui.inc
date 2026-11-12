/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise production instruction menus, hardware breakpoint edits and address naming with real GDB.
 * RU: Проверяет настоящие меню инструкций, аппаратные точки, их правила и имена адресов с реальным GDB. */

/* EN: Build window's real instruction popup and activate label, optionally moving the cursor before activation to test captured addresses.
 * RU: Создаёт настоящее меню инструкций window и выбирает label; при move_cursor меняет курсор до выбора для проверки сохранённого адреса. */
static gboolean instruction_menu_choose(GtkWidget *window, const gchar *label, gboolean move_cursor)
{
    GtkWidget *view = find(window, "insight-gdb-instructions");
    GtkWidget *menu = gtk_menu_new();
    g_object_ref_sink(menu);
    g_signal_emit_by_name(view, "populate-popup", menu);
    GtkWidget *item = find(menu, label);
    gboolean found = item && GTK_IS_MENU_ITEM(item) && gtk_widget_is_sensitive(item);
    if (move_cursor)
    {
        GtkTextIter row;
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        gtk_text_buffer_get_iter_at_line(buffer, &row, 1);
        gtk_text_buffer_place_cursor(buffer, &row);
    }
    if (found)
        gtk_menu_item_activate(GTK_MENU_ITEM(item));
    gtk_widget_destroy(menu);
    g_object_unref(menu);
    return found;
}

/* EN: Return whether window's confirmed hardware point has column equal to expected; no source point is selected or modified.
 * RU: Проверяет column подтверждённой аппаратной точки window на expected; точки исходников не выбирает и не изменяет. */
static gboolean instruction_hardware_column(GtkWidget *window, gint column, const gchar *expected)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(find(window, "insight-gdb-breakpoints")));
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *kind = NULL, *value = NULL;
        gtk_tree_model_get(model, &row, 8, &kind, column, &value, -1);
        gboolean match = !g_strcmp0(kind, "hw breakpoint") && !g_strcmp0(value, expected);
        g_free(kind);
        g_free(value);
        if (match)
            return TRUE;
    }
    return FALSE;
}

/* EN: Fill the shared rule dialog named by data with a nondefault value; wait until it opens.
 * RU: Заполняет общий диалог правила с именем data нестандартным значением; ждёт его открытия. */
static gboolean instruction_menu_rule(gpointer data)
{
    GtkWidget *dialog = window_named(data);
    GtkWidget *entry = dialog ? find(dialog, "insight-gdb-breakpoint-rule") : NULL;
    if (!entry)
        return G_SOURCE_CONTINUE;
    gtk_entry_set_text(GTK_ENTRY(entry), !strcmp(data, "Breakpoint condition") ? "1" : "7");
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* EN: Check the numeric address prefilled in the naming dialog, name it using data, and apply the normal validation.
 * RU: Проверяет числовой адрес, подставленный в диалог имени, задаёт имя data и применяет обычную проверку. */
static gboolean instruction_menu_name(gpointer data)
{
    GtkWidget *dialog = window_named("Name or type binding");
    GtkWidget *anchor = dialog ? find(dialog, "insight-gdb-name-anchor") : NULL;
    GtkWidget *label = dialog ? find(dialog, "insight-gdb-name-label") : NULL;
    if (!anchor || !label)
        return G_SOURCE_CONTINUE;
    g_assert_true(g_str_has_prefix(gtk_entry_get_text(GTK_ENTRY(anchor)), "0x"));
    gtk_entry_set_text(GTK_ENTRY(label), data);
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* EN: Advance window through phases 84..92 of real menu edits and double-click deletion, then restore the legacy value tests in tabs.
 * RU: Продвигает window по фазам 84..92 настоящих меню и удаления двойным щелчком, затем возвращает проверки значений в tabs. */
static gboolean instruction_menu_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    GtkWidget *view = find(window, "insight-gdb-instructions");
    GtkWidget *entry = find(window, "insight-gdb-instruction-address");
    if (!entry || !gtk_widget_is_sensitive(entry))
        return G_SOURCE_CONTINUE;
    static gint points_before;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(buffer, &start);
    GtkTreeModel *points = gtk_tree_view_get_model(GTK_TREE_VIEW(find(window, "insight-gdb-breakpoints")));
    if (phase == 84)
    {
        GtkWidget *page = values_page(entry, tabs);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), gtk_notebook_page_num(GTK_NOTEBOOK(tabs), page));
        gtk_text_buffer_place_cursor(buffer, &start);
        points_before = gtk_tree_model_iter_n_children(points, NULL);
        gchar *text = native_assembly_gui_snapshot(window);
        const gchar *address = strstr(text, "0x");
        g_assert_nonnull(address);
        const gchar *end = address + 2;
        while (g_ascii_isxdigit(*end))
            end++;
        gchar *digits = g_strndup(address, end - address);
        guint64 numeric = g_ascii_strtoull(digits, NULL, 16);
        gchar *expected = g_strdup_printf("0x%" G_GINT64_MODIFIER "x", numeric);
        g_free(digits);
        if (instruction_menu_choose(window, "Copy instruction address", TRUE))
        {
            gchar *copied = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
            g_assert_cmpstr(copied, ==, expected);
            g_free(copied);
            gtk_text_buffer_place_cursor(buffer, &start);
            if (instruction_menu_choose(window, "Add hardware instruction breakpoint", FALSE))
                phase = 85;
        }
        g_free(expected);
        g_free(text);
    }
    else if (phase == 85 && instruction_hardware_column(window, 2, "y"))
    {
        if (instruction_menu_choose(window, "Enable / Disable", FALSE))
            phase = 86;
    }
    else if (phase == 86 && instruction_hardware_column(window, 2, "n"))
    {
        phase = 87;
        g_timeout_add(100, instruction_menu_rule, "Breakpoint condition");
        g_assert_true(instruction_menu_choose(window, "Breakpoint condition", FALSE));
    }
    else if (phase == 87 && instruction_hardware_column(window, 3, "1"))
    {
        phase = 88;
        g_timeout_add(100, instruction_menu_rule, "Ignore first hits");
        g_assert_true(instruction_menu_choose(window, "Ignore first hits", FALSE));
    }
    else if (phase == 88 && instruction_hardware_column(window, 5, "7"))
    {
        GdkRectangle location;
        gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view), &start, &location);
        gint x, y;
        gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(view), GTK_TEXT_WINDOW_TEXT,
            location.x + 2, location.y + location.height / 2, &x, &y);
        GdkEventButton event = {0};
        event.type = GDK_2BUTTON_PRESS;
        event.button = 1;
        event.x = x;
        event.y = y;
        gboolean handled = FALSE;
        g_signal_emit_by_name(view, "button-press-event", &event, &handled);
        if (handled)
            phase = 89;
    }
    else if (phase == 89 && gtk_tree_model_iter_n_children(points, NULL) == points_before)
    {
        phase = 90;
        g_timeout_add(100, instruction_menu_name, "cursor_entry_name");
        g_assert_true(instruction_menu_choose(window, "Name this instruction address…", FALSE));
    }
    else if (phase == 90 && native_assembly_gui_text(window, "cursor_entry_name"))
    {
        GtkTextIter cursor;
        gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
        g_assert_cmpint(gtk_text_iter_get_line(&cursor), ==, 0);
        g_assert_true(instruction_menu_choose(window, "Run to instruction", FALSE));
        phase = 91;
    }
    else if (phase == 91 && !g_strcmp0(gtk_label_get_text(GTK_LABEL(find(window, "insight-gdb-instruction-status"))),
        "The selected frame is already at this instruction."))
    {
        g_print("C/C++ GTK: captured-address popup, hardware point, enable/condition/ignore, double-click and name passed.\n");
        g_assert_true(instruction_menu_choose(window, "Open source for instruction", FALSE));
        phase = 92;
    }
    else if (phase == 92)
    {
        const gchar *status = gtk_label_get_text(GTK_LABEL(find(window, "insight-gdb-instruction-status")));
        g_print("C/C++ GTK source status: %s\n", status);
        g_assert_cmpstr(status, ==, "Instruction source opened. The execution stop has not changed.");
        GeanyDocument *document = document_get_current();
        g_assert_true(DOC_VALID(document));
        guint line = sci_get_line_from_position(document->editor->sci, sci_get_current_position(document->editor->sci)) + 1;
        GtkTreeModel *frames = gtk_tree_view_get_model(GTK_TREE_VIEW(find(window, "insight-gdb-stack")));
        GtkTreeIter frame;
        g_assert_true(gtk_tree_model_get_iter_first(frames, &frame));
        gchar *expected = NULL;
        gtk_tree_model_get(frames, &frame, 3, &expected, -1);
        g_assert_cmpuint(line, ==, (guint)g_ascii_strtoull(expected, NULL, 10));
        g_free(expected);
        g_assert_true(source_markers());
        g_print("C/C++ GTK: exact instruction-to-source navigation preserved execution markers.\n");
        phase = 16;
    }
    return G_SOURCE_CONTINUE;
}
