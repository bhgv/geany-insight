/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise the real exception controls before the local debugger execution scenario.
 * RU: Проверяет настоящие элементы исключений перед сценарием выполнения локального отладчика. */

/* EN: Find and select window's exception row for caption/state, or return FALSE until its confirmed snapshot arrives.
 * RU: Ищет и выбирает точку исключения window по caption/state либо возвращает FALSE до подтверждённого снимка. */
static gboolean catch_gui_row(GtkWidget *window, const gchar *caption, const gchar *state)
{
    GtkWidget *view = find(window, "insight-gdb-breakpoints");
    if (!view)
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *kind = NULL, *name = NULL, *enabled = NULL;
        gtk_tree_model_get(model, &row, 8, &kind, 0, &name, 2, &enabled, -1);
        gboolean matches = !g_strcmp0(kind, "catchpoint") &&
            (!caption || !g_strcmp0(caption, name)) && (!state || !g_strcmp0(state, enabled));
        g_free(enabled);
        g_free(name);
        g_free(kind);
        if (matches)
        {
            gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), &row);
            return TRUE;
        }
    }
    return FALSE;
}

/* EN: Add, disable and delete all three C++ event kinds in window; return TRUE only after every confirmed deletion.
 * RU: Добавляет, выключает и удаляет три вида событий C++ в window; TRUE возвращает после подтверждения всех удалений. */
static gboolean native_catch_gui_step(GtkWidget *window)
{
    static guint event, stage;
    const gchar *captions[] = {"C++ throw [std::.*]", "C++ catch", "C++ rethrow"};
    if (event == G_N_ELEMENTS(captions))
        return TRUE;
    GtkWidget *add = find(window, "insight-gdb-exception-add");
    if (!add || !gtk_widget_is_sensitive(add))
        return FALSE;
    if (stage == 0)
    {
        GtkWidget *choice = find(window, "insight-gdb-exception-event");
        GtkWidget *filter = find(window, "insight-gdb-exception-filter");
        gtk_combo_box_set_active(GTK_COMBO_BOX(choice), event);
        gtk_entry_set_text(GTK_ENTRY(filter), event == 0 ? "std::.*" : "");
        if (event == 1)
            g_signal_emit_by_name(filter, "activate");
        else
            gtk_button_clicked(GTK_BUTTON(add));
        stage = 1;
    }
    else if (stage == 1 && catch_gui_row(window, captions[event], "y") && click(window, "Enable / Disable"))
        stage = 2;
    else if (stage == 2 && catch_gui_row(window, captions[event], "n") && click(window, "Delete Breakpoint"))
        stage = 3;
    else if (stage == 3 && !catch_gui_row(window, NULL, NULL))
    {
        event++;
        stage = 0;
        if (event == G_N_ELEMENTS(captions))
            g_print("C/C++ GTK: throw/catch/rethrow controls, regex labels, Enter, toggle and deletion passed.\n");
    }
    return FALSE;
}
