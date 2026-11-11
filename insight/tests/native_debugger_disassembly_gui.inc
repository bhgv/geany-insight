/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise instruction reading and real manual-name dialogs without moving the source execution point.
 * RU: Проверяет чтение инструкций и настоящие диалоги имён без смены позиции исполнения исходного сценария. */

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

/* EN: Return whether window's real instruction text includes expected, without treating console text as disassembly.
 * RU: Проверяет наличие expected в настоящем тексте инструкций window, не подменяя его текстом консоли. */
static gboolean native_assembly_gui_text(GtkWidget *window, const gchar *expected)
{
    GtkWidget *view = find(window, "insight-gdb-instructions");
    GtkTextIter start, end;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gboolean valid = expected ? strstr(text, expected) != NULL : !*text;
    g_free(text);
    return valid;
}

/* EN: Advance phases 70..76 through add/rename/save/error recovery in window, then resume value tests in tabs.
 * RU: Продвигает фазы 70..76 добавления/переименования/сохранения/ошибки window, затем возвращает тесты значений в tabs. */
static gboolean native_assembly_gui_step(GtkWidget *window, GtkWidget *tabs)
{
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
        g_print("C/C++ GTK: disassembly, automatic name refresh, rename, profile save and error recovery passed.\n");
        phase = 16;
    }
    return G_SOURCE_CONTINUE;
}
