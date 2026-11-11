/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise instruction reading, names and automatic PC refresh with two controlled machine steps.
 * RU: Проверяет чтение инструкций, имена и слежение за PC двумя контролируемыми машинными шагами. */

/* EN: Fill the real name dialog with main's address and name in data, then apply the production validation.
 * RU: Заполняет настоящий диалог адресом main и именем data, затем применяет производственную проверку. */
static gboolean native_assembly_gui_name(gpointer data)
{
    GtkWidget *dialog = window_named("Name or type binding");
    GtkWidget *anchor = dialog ? find(dialog, "insight-gdb-name-anchor") : NULL;
    GtkWidget *label = dialog ? find(dialog, "insight-gdb-name-label") : NULL;
    if (!anchor || !label)
        return G_SOURCE_CONTINUE;
    gtk_entry_set_text(GTK_ENTRY(anchor), "&main");
    gtk_entry_set_text(GTK_ENTRY(label), data);
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* EN: Return an owned copy of window's production instruction snapshot for step/follow comparisons.
 * RU: Возвращает собственную копию настоящего снимка инструкций window для сравнения шага/слежения. */
static gchar *native_assembly_gui_snapshot(GtkWidget *window)
{
    GtkWidget *view = find(window, "insight-gdb-instructions");
    GtkTextIter start, end;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

/* EN: Return whether window's real instruction text includes expected, without treating console text as disassembly.
 * RU: Проверяет наличие expected в настоящем тексте инструкций window, не подменяя его текстом консоли. */
static gboolean native_assembly_gui_text(GtkWidget *window, const gchar *expected)
{
    gchar *text = native_assembly_gui_snapshot(window);
    gboolean valid = expected ? strstr(text, expected) != NULL : !*text;
    g_free(text);
    return valid;
}

/* EN: Advance phases 70..78 through names, profiles, errors and instruction following in window, then resume value tests in tabs.
 * RU: Продвигает фазы 70..78 имён, профилей, ошибок и слежения window, затем возвращает тесты значений в tabs. */
static gboolean native_assembly_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    static gchar *previous_instruction;
    GtkWidget *entry = find(window, "insight-gdb-instruction-address");
    if (!entry || !gtk_widget_is_sensitive(entry))
        return G_SOURCE_CONTINUE;
    if (phase == 70)
    {
        GtkWidget *watch = find(window, "GDB watch expression");
        if (!watch || !gtk_widget_is_sensitive(watch))
            return G_SOURCE_CONTINUE;
        gtk_entry_set_text(GTK_ENTRY(watch), "&local_child.code");
        if (click(window, "Add Watch"))
            phase = 75;
    }
    else if (phase == 75)
    {
        GtkWidget *page = values_page(entry, tabs);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs), gtk_notebook_page_num(GTK_NOTEBOOK(tabs), page));
        gtk_entry_set_text(GTK_ENTRY(entry), "main");
        GtkWidget *expander = find(window, "insight-gdb-name-rules");
        gtk_expander_set_expanded(GTK_EXPANDER(expander), TRUE);
        phase = 71;
        g_timeout_add(100, native_assembly_gui_name, "entry_alias");
        click(window, "Address name…");
    }
    else if (phase == 71 && native_assembly_gui_text(window, "entry_alias") &&
             find(window, "local: local_child.code"))
    {
        GtkWidget *view = find(window, "insight-gdb-address-names");
        GtkTreePath *path = gtk_tree_path_new_first();
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), path);
        gtk_tree_path_free(path);
        phase = 73;
        g_timeout_add(100, native_assembly_gui_name, "program_entry");
        click(window, "Edit name…");
    }
    else if (phase == 73 && native_assembly_gui_text(window, "program_entry") &&
             !native_assembly_gui_text(window, "entry_alias"))
    {
        if (click(window, "Save names in debugger profile"))
            phase = 74;
    }
    else if (phase == 74 && !g_strcmp0(gtk_label_get_text(GTK_LABEL(find(window, "insight-gdb-instruction-status"))),
        "Debugger profile saved for this project and executable."))
    {
        gtk_entry_set_text(GTK_ENTRY(entry), "missing_instruction_symbol");
        if (click(window, "Read instructions"))
            phase = 72;
    }
    else if (phase == 72 && native_assembly_gui_text(window, NULL))
    {
        gtk_entry_set_text(GTK_ENTRY(entry), "$pc");
        phase = 76;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(find(window, "insight-gdb-follow-instructions")), TRUE);
    }
    else if (phase == 76 && native_assembly_gui_text(window, "main") &&
             find(window, "local: local_child.code"))
    {
        previous_instruction = native_assembly_gui_snapshot(window);
        if (click(window, "Step instruction"))
            phase = 77;
    }
    else if (phase == 77 || phase == 78)
    {
        gchar *current = native_assembly_gui_snapshot(window);
        gboolean moved = g_strcmp0(current, previous_instruction) != 0;
        if (moved && phase == 77)
        {
            g_free(previous_instruction);
            previous_instruction = g_strdup(current);
            if (click(window, "Next instruction"))
                phase = 78;
        }
        else if (moved)
        {
            g_clear_pointer(&previous_instruction, g_free);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(find(window, "insight-gdb-follow-instructions")), FALSE);
            g_print("C/C++ GTK: automatic local fields, manual names, profiles, instruction steps and PC following passed.\n");
            phase = 16;
        }
        g_free(current);
    }
    return G_SOURCE_CONTINUE;
}
