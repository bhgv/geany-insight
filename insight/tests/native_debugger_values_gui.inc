/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Extend the editor/GDB GUI scenario with lazy trees, assignment and data watchpoints.
 * RU: Дополняет сценарий редактора/GDB деревьями значений, присваиванием и точками по данным. */

/* EN: Return widget's notebook page in tabs, or NULL if it is outside the notebook.
 * RU: Возвращает страницу widget в tabs либо NULL, если элемент вне блокнота. */
static GtkWidget *values_page(GtkWidget *widget, GtkWidget *tabs)
{
    while (widget && gtk_widget_get_parent(widget) != tabs)
        widget = gtk_widget_get_parent(widget);
    return widget;
}

/* EN: Expand path in window's real value tree after the command queue becomes idle.
 * RU: Раскрывает path в настоящем дереве window, когда очередь команд свободна. */
static gboolean expand_value(GtkWidget *window, const gchar *text)
{
    GtkWidget *tree = find(window, "insight-gdb-value-tree");
    if (!tree || !gtk_widget_is_sensitive(tree))
        return FALSE;
    GtkTreePath *path = gtk_tree_path_new_from_string(text);
    GtkTreeIter row;
    gboolean found = gtk_tree_model_get_iter(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)), &row, path);
    if (found)
        gtk_tree_view_expand_row(GTK_TREE_VIEW(tree), path, FALSE);
    gtk_tree_path_free(path);
    return found;
}

/* EN: Check and optionally select member/value under parent in window's value tree.
 * RU: Проверяет и при необходимости выбирает member/value под parent в дереве window. */
static gboolean value_member(GtkWidget *window, const gchar *parent, const gchar *member,
                              const gchar *expected, gboolean select)
{
    GtkWidget *tree = find(window, "insight-gdb-value-tree");
    if (!tree || !gtk_widget_is_sensitive(tree))
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    GtkTreePath *path = gtk_tree_path_new_from_string(parent);
    GtkTreeIter root, child;
    gboolean found = FALSE;
    if (gtk_tree_model_get_iter(model, &root, path))
        for (gboolean valid = gtk_tree_model_iter_children(model, &child, &root); valid;
             valid = gtk_tree_model_iter_next(model, &child))
        {
            gchar *name = NULL, *value = NULL;
            gtk_tree_model_get(model, &child, 0, &name, 2, &value, -1);
            found = !g_strcmp0(name, member) && (!expected || !g_strcmp0(value, expected));
            g_free(value);
            g_free(name);
            if (found)
            {
                if (select)
                    gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &child);
                break;
            }
        }
    gtk_tree_path_free(path);
    return found;
}

/* EN: Activate the pagination row at index under the array in window after checking current size.
 * RU: Активирует строку страницы index массива window после проверки текущего размера. */
static gboolean value_more(GtkWidget *window, guint index)
{
    GtkWidget *tree = find(window, "insight-gdb-value-tree");
    if (!tree || !gtk_widget_is_sensitive(tree))
        return FALSE;
    gchar *text = g_strdup_printf("0:2:%u", index);
    GtkTreePath *path = gtk_tree_path_new_from_string(text);
    GtkTreeIter row;
    gboolean found = gtk_tree_model_get_iter(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)), &row, path);
    if (found)
        gtk_tree_view_row_activated(GTK_TREE_VIEW(tree), path, NULL);
    gtk_tree_path_free(path);
    g_free(text);
    return found;
}

/* EN: Apply replacement data in the actual Change Value dialog, waiting until it appears.
 * RU: Применяет новое значение data в настоящем диалоге изменения, дожидаясь его появления. */
static gboolean set_value(gpointer data)
{
    GtkWidget *window = window_named("Change Value");
    GtkWidget *entry = window ? find(window, "insight-gdb-new-value") : NULL;
    if (!entry)
        return G_SOURCE_CONTINUE;
    gtk_entry_set_text(GTK_ENTRY(entry), data);
    gtk_dialog_response(GTK_DIALOG(window), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* EN: Select marker in window's variables and invoke label; replacement opens a modal editor if supplied.
 * RU: Выбирает marker в переменных window и вызывает label; replacement задаёт значение модального редактора. */
static gboolean local_command(GtkWidget *window, GtkWidget *tabs, const gchar *label, const gchar *replacement)
{
    GtkWidget *tree = find(window, "insight-gdb-frame-variables");
    if (!tree || !gtk_widget_is_sensitive(tree))
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *name = NULL;
        gtk_tree_model_get(model, &row, 0, &name, -1);
        gboolean selected = !g_strcmp0(name, "marker");
        g_free(name);
        if (selected)
        {
            gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), &row);
            GtkWidget *page = values_page(tree, tabs);
            GtkWidget *button = page ? find(page, label) : NULL;
            if (!button || !gtk_widget_is_sensitive(button))
                return FALSE;
            if (replacement)
                g_timeout_add(100, set_value, (gpointer)replacement);
            gtk_button_clicked(GTK_BUTTON(button));
            return TRUE;
        }
    }
    return FALSE;
}

/* EN: Detect a data watchpoint in window's synchronized GDB breakpoint model.
 * RU: Находит точку по данным в синхронизированной модели точек GDB окна window. */
static gboolean has_data_point(GtkWidget *window)
{
    GtkWidget *tree = find(window, "insight-gdb-breakpoints");
    if (!tree)
        return FALSE;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *kind = NULL;
        gtk_tree_model_get(model, &row, 8, &kind, -1);
        gboolean found = kind && strstr(kind, "watchpoint");
        g_free(kind);
        if (found)
            return TRUE;
    }
    return FALSE;
}

/* EN: Advance the data-inspection part of the real editor/GDB scenario for window and tabs.
 * RU: Продвигает часть исследования данных настоящего редактора/GDB для window и tabs. */
static gboolean native_values_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    if (phase == 16)
    {
        GtkWidget *entry = find(window, "insight-gdb-inspect-expression");
        if (entry && gtk_widget_is_sensitive(entry))
        {
            gtk_entry_set_text(GTK_ENTRY(entry), "debug_sample");
            if (click(window, "Inspect Expression"))
                phase++;
        }
    }
    else if (phase == 17 && expand_value(window, "0"))
        phase++;
    else if (phase == 18 && value_member(window, "0", "count", "7", TRUE))
    {
        GtkWidget *page = values_page(find(window, "insight-gdb-value-tree"), tabs);
        GtkWidget *button = page ? find(page, "Change Value") : NULL;
        if (button && gtk_widget_is_sensitive(button))
        {
            phase++;
            g_timeout_add(100, set_value, "13");
            gtk_button_clicked(GTK_BUTTON(button));
        }
    }
    else if (phase == 19 && expand_value(window, "0"))
        phase++;
    else if (phase == 20 && value_member(window, "0", "count", "13", FALSE) && expand_value(window, "0:2"))
        phase++;
    else if (phase == 21 && value_more(window, 128))
        phase++;
    else if (phase == 22 && value_more(window, 256))
        phase++;
    else if (phase == 23 && value_member(window, "0:2", "259", "0", FALSE) &&
             value_member(window, "0", "count", "13", TRUE) && click(window, "Add selected watch"))
        phase++;
    else if (phase == 24 && find(window, "(debug_sample).count = 13") &&
             local_command(window, tabs, "Break on Write", NULL))
        phase++;
    else if (phase == 25 && has_data_point(window) && click(window, "Continue"))
        phase++;
    else if (phase == 26 && has_local(window, "marker", "42"))
    {
        GtkWidget *reason = find(window, "insight-gdb-stop-reason");
        const gchar *text = reason ? gtk_label_get_text(GTK_LABEL(reason)) : "";
        if (strstr(text, "Data watchpoint") && strstr(text, "41") && strstr(text, "42") &&
            local_command(window, tabs, "Change selected value", "100"))
            phase++;
    }
    else if (phase == 27 && has_local(window, "marker", "100") && find(window, "marker = 100"))
    {
        GeanyDocument *document = document_get_current();
        if (!DOC_VALID(document))
            return G_SOURCE_CONTINUE;
        for (gint line = 0; line < sci_get_line_count(document->editor->sci); line++)
        {
            gchar *text = sci_get_line(document->editor->sci, line);
            gboolean target_line = strstr(text, "return marker") != NULL;
            g_free(text);
            if (target_line)
            {
                sci_set_current_position(document->editor->sci,
                    sci_get_position_from_line(document->editor->sci, line), FALSE);
                if (click(window, "Run to Cursor"))
                    phase++;
                break;
            }
        }
    }
    else if (phase == 28 && has_local(window, "marker", "102") && click(window, "Stop"))
        phase++;
    else if (phase == 29)
    {
        GtkWidget *run = find(window, "Run");
        if (run && gtk_widget_is_sensitive(run) && !has_local(window, "marker", "102"))
        {
            g_print("C/C++ GTK: paged value trees, assignment, data watchpoint and run-to-cursor passed.\n");
            passed = TRUE;
            gtk_widget_destroy(window);
            gtk_main_quit();
            return G_SOURCE_REMOVE;
        }
    }
    return G_SOURCE_CONTINUE;
}
