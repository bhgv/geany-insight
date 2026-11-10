/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Symbol browser filtering, selection and navigation.
 * RU: Фильтрация, выбор и навигация в списке символов. */


/* EN: Refresh the browser query and reset paging if entry triggered it. data is unused.
 * RU: Обновляет запрос и сбрасывает страницу при вводе в entry. data не используется. */
static void browser_filter(GtkEditable *entry, gpointer data)
{
    (void)data;
    if (!wb.symbols) return;
    if (entry)
        insight_virtual_list_position(GTK_TREE_VIEW(wb.symbol_view), 0);
    g_object_set_data_full(G_OBJECT(wb.symbol_view), "insight-project-root", wb.root ? utils_get_utf8_from_locale(wb.root) : NULL, g_free);
    GeanyDocument *doc = document_get_current();
    gchar *profile = doc ? insight_languages_profile_name(doc->editor->sci, doc->file_name, doc->file_type) : g_strdup("");
    if (entry || !wb.project_symbol_limit || g_strcmp0(wb.project_page_root, wb.root) || g_strcmp0(wb.project_page_profile, profile))
        wb.project_symbol_limit = 500;
    g_free(wb.project_page_root);
    wb.project_page_root = g_strdup(wb.root);
    g_free(wb.project_page_profile);
    wb.project_page_profile = profile;
    fill_symbol_list(wb.symbols, wb.symbol_entry, NULL, wb.project_symbol_limit);
}

/* EN: Update the source preview for selection. data is unused.
 * RU: Обновляет предпросмотр для selection. data не используется. */
static void browser_selected(GtkTreeSelection *selection, gpointer data)
{
    (void)data;
    if (!wb.preview || !gtk_widget_get_sensitive(GTK_WIDGET(gtk_tree_selection_get_tree_view(selection)))) return;
    GtkWidget *view = GTK_WIDGET(gtk_tree_selection_get_tree_view(selection));
    if ((view == wb.local_view || view == wb.symbol_view) && !gtk_widget_has_focus(view)) return;
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    gchar *path, *name;
    gulong line;
    gtk_tree_model_get(model, &iter, SYMBOL_PATH, &path, SYMBOL_NAME, &name, SYMBOL_LINE, &line, -1);
    if (!line)
    {
        g_free(path);
        g_free(name);
        return;
    }
    reveal_context();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow), FALSE);
    if (show_location(path, gtk_tree_selection_get_tree_view(selection) == GTK_TREE_VIEW(wb.local_view) ? wb.local_doc_id : 0, line, TRUE)) preview_mark_symbol(name);
    g_free(path);
    g_free(name);
}

/* EN: Handle event on widget using the supplied data.
 * RU: Обрабатывает event в widget с учётом data. */
static gboolean browser_symbol_clicked(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)data;
    if (event->button != 1 || !gtk_widget_has_focus(widget))
        return FALSE;
    GtkTreePath *row = NULL;
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x, (gint)event->y, &row, NULL, NULL, NULL))
    {
        gtk_tree_path_free(row);
        browser_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)), NULL);
    }
    return FALSE;
}

/* EN: Open the symbol at path in view. column and data are unused.
 * RU: Открывает символ по path из view. column и data не используются. */
static void browser_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data)
{
    (void)column;
    (void)data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
    gtk_tree_selection_select_path(selection, path);
    if (GTK_WIDGET(view) == wb.symbol_view || GTK_WIDGET(view) == wb.local_view)
    {
        GtkTreeModel *model = gtk_tree_view_get_model(view);
        GtkTreeIter iter;
        gulong line = 0;
        if (!gtk_tree_model_get_iter(model, &iter, path)) return;
        gtk_tree_model_get(model, &iter, SYMBOL_LINE, &line, -1);
        if (!line)
        {
            if (gtk_tree_view_row_expanded(view, path)) gtk_tree_view_collapse_row(view, path);
            else gtk_tree_view_expand_row(view, path, FALSE);
            return;
        }
    }
    if (GTK_WIDGET(view) == wb.local_view || GTK_WIDGET(view) == wb.symbol_view)
    {
        GtkTreeModel *model = gtk_tree_view_get_model(view);
        GtkTreeIter iter;
        gchar *file = NULL, *name = NULL;
        gulong line = 0;
        if (gtk_tree_model_get_iter(model, &iter, path))
        {
            gtk_tree_model_get(model, &iter, SYMBOL_PATH, &file, SYMBOL_NAME, &name, SYMBOL_LINE, &line, -1);
            GeanyDocument *doc = GTK_WIDGET(view) == wb.local_view ? document_find_by_id(wb.local_doc_id) : NULL;
            if (!doc && file && line)
            {
                gchar *native = utils_get_locale_from_utf8(file);
                doc = document_open_file(native, FALSE, NULL, NULL);
                g_free(native);
            }
            if (doc && line)
            {
                editor_mark_symbol(doc, line, name);
                document_grab_focus(doc);
            }
        }
        g_free(file);
        g_free(name);
        return;
    }
    if (GTK_WIDGET(view) == wb.symbol_view || GTK_WIDGET(view) == wb.local_view || GTK_WIDGET(view) == wb.category_view) browser_selected(selection, NULL);
    else if (GTK_WIDGET(view) == wb.file_view)
    {
        GtkTreeIter iter;
        GtkTreeModel *model = gtk_tree_view_get_model(view);
        if (!gtk_tree_model_get_iter(model, &iter, path)) return;
        gchar *filename;
        gtk_tree_model_get(model, &iter, 1, &filename, -1);
        gchar *native = utils_get_locale_from_utf8(filename);
        GeanyDocument *doc = document_open_file(native, FALSE, NULL, NULL);
        g_free(native);
        g_free(filename);
        if (doc)
        {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow), TRUE);
            document_grab_focus(doc);
            insight_workbench_refresh();
        }
        return;
    }
    else return;
    open_context(NULL, NULL);
}
