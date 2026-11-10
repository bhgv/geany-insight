/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Locating and paging symbol list rows.
 * RU: Поиск и постраничная загрузка строк символов. */


/* EN: Match iter in model against the target in data and remember path.
 * RU: Сравнивает iter из model с целью из data и запоминает path. */
static gboolean symbol_locate_row(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
    SymbolLocate *target = data;
    gchar *name = NULL;
    gulong line = 0;
    gtk_tree_model_get(model, iter, SYMBOL_NAME, &name, SYMBOL_LINE, &line, -1);
    gboolean match = line == target->tag->line && !g_strcmp0(name, target->tag->name);
    g_free(name);
    if (match) target->path = gtk_tree_path_copy(path);
    return match;
}

/* EN: Locate the active source symbol in the list selected by data.
 * RU: Находит символ активного исходника в списке из data. */
static void symbols_locate(GtkWidget *button, gpointer data)
{
    (void)button;
    (void)data;
    GeanyDocument *doc = document_get_current();
    if (!doc || !wb.local_view || !gtk_widget_get_sensitive(wb.local_view) || doc->id != wb.local_doc_id)
    {
        ui_set_statusbar(TRUE, "File symbols are still updating");
        return;
    }
    TMTag *tag = relation_enclosing_function(doc);
    if (!tag && doc->tm_file && doc->tm_file->tags_array)
    {
        gulong line = (gulong)sci_get_current_line(doc->editor->sci)+1;
        for (guint i = 0;i<doc->tm_file->tags_array->len;i++)
        {
            TMTag *candidate = g_ptr_array_index(doc->tm_file->tags_array, i);
            if (candidate && candidate->line && candidate->line <= line && (!tag || candidate->line>tag->line)) tag = candidate;
        }
    }
    if (!tag)
    {
        ui_set_statusbar(TRUE, "No indexed declaration at or before the caret");
        return;
    }
    SymbolLocate target =
    {
        .tag = tag
    };
    gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(wb.local_view)), symbol_locate_row, &target);
    if (!target.path)
    {
        ui_set_statusbar(TRUE, "The caret symbol is outside the loaded outline or hidden by a filter");
        return;
    }
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view));
    guint signal = g_signal_lookup("changed", GTK_TYPE_TREE_SELECTION);
    g_signal_handlers_block_matched(selection, G_SIGNAL_MATCH_ID, signal, 0, NULL, NULL, NULL);
    gtk_tree_view_expand_to_path(GTK_TREE_VIEW(wb.local_view), target.path);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(wb.local_view), target.path, NULL, FALSE);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(wb.local_view), target.path, NULL, TRUE, 0.5, 0);
    g_signal_handlers_unblock_matched(selection, G_SIGNAL_MATCH_ID, signal, 0, NULL, NULL, NULL);
    gtk_tree_path_free(target.path);
}

/* EN: Load more rows into the list selected by data.
 * RU: Загружает следующие строки в список из data. */
static void symbols_load_more(GtkWidget *button, gpointer data)
{
    (void)button;
    GtkWidget *view = data;
    if (!gtk_widget_get_sensitive(view)) return;
    if (view == wb.local_view)
    {
        wb.local_symbol_limit = MIN(250000u, wb.local_symbol_limit+2000u);
        local_filter(NULL, NULL);
    }
    else if (view == wb.symbol_view)
    {
        wb.project_symbol_limit = MIN(250000u, wb.project_symbol_limit+500u);
        browser_filter(NULL, NULL);
    }
}
