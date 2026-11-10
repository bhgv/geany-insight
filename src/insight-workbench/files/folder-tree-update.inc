/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Folder tree refresh, filtering and selection.
 * RU: Обновление дерева каталогов, фильтрация и выбор. */


/* EN: Update the folder panel counter for shown and total rows.
 * RU: Обновляет счётчик видимых shown и всех total строк. */
static void folder_count(guint shown, guint total)
{
    g_object_set_data(G_OBJECT(wb.tree_status), "insight-folder-count", GUINT_TO_POINTER(shown));
    g_object_set_data(G_OBJECT(wb.tree_status), "insight-folder-total", GUINT_TO_POINTER(total));
    gchar *label = insight_i18n_printf("Folder files: %u / %u", shown, total);
    gtk_label_set_text(GTK_LABEL(wb.tree_status), label);
    g_free(label);
}

/* EN: Locate the current document in the folder tree. report enables feedback.
 * RU: Находит текущий документ в дереве. report включает уведомление. */
static gboolean folder_locate(gboolean report)
{
    GeanyDocument *doc = document_get_current();
    if (!doc || !doc->file_name)
    {
        if (report) ui_set_statusbar(TRUE, "Current file has no project path");
        return FALSE;
    }
    FolderVisit visit =
    {
        .wanted = doc->file_name
    };
    gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree), folder_visit, &visit);
    if (!visit.found)
    {
        if (report) ui_set_statusbar(TRUE, "Current file is outside the project tree or hidden by its filter");
        return FALSE;
    }
    wb.tree_setting = TRUE;
    gtk_tree_view_expand_to_path(GTK_TREE_VIEW(wb.tree_view), visit.found);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(wb.tree_view), visit.found, NULL, FALSE);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(wb.tree_view), visit.found, NULL, TRUE, 0.5, 0);
    wb.tree_setting = FALSE;
    gtk_tree_path_free(visit.found);
    return TRUE;
}

/* EN: Rebuild the project folder tree and preserve its view state.
 * RU: Обновляет дерево проекта и сохраняет его состояние. */
static void tree_refresh(void)
{
    if (!wb.project_tree || !gtk_widget_get_mapped(wb.tree_view)) return;
    GError *error = NULL;
    InsightLookupFilter *filter = insight_lookup_filter_new(gtk_entry_get_text(GTK_ENTRY(wb.tree_entry)), TRUE, &error);
    if (!filter)
    {
        gtk_widget_set_tooltip_text(wb.tree_entry, error ? error->message : "Invalid folder filter");
        gtk_style_context_add_class(gtk_widget_get_style_context(wb.tree_entry), "error");
        g_clear_error(&error);
        return;
    }
    gtk_style_context_remove_class(gtk_widget_get_style_context(wb.tree_entry), "error");
    gtk_widget_set_tooltip_text(wb.tree_entry, "Filter project paths: fragments, glob, ?regex or ~fuzzy; ancestors stay visible");
    /* Text edits do not change the folder model. Keep expanded rows and selection
  * intact unless membership, root or the path filter actually changed. */
    g_ptr_array_sort(wb.files, project_path_compare);
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(wb.tree_entry));
    g_checksum_update(sum, (const guchar *)query, strlen(query)+1);
    if (wb.root)
        g_checksum_update(sum, (const guchar *)wb.root, strlen(wb.root)+1);
    for (guint n = 0;n<wb.files->len;n++)
    {
        const gchar *path = g_ptr_array_index(wb.files, n);
        g_checksum_update(sum, (const guchar *)path, strlen(path)+1);
    }
    const gchar *key = g_checksum_get_string(sum);
    gboolean same = !g_strcmp0(key, g_object_get_data(G_OBJECT(wb.project_tree), "insight-request-key"));
    if (!same)
        g_object_set_data_full(G_OBJECT(wb.project_tree), "insight-request-key", g_strdup(key), g_free);
    g_checksum_free(sum);
    if (same)
    {
        insight_lookup_filter_free(filter);
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.tree_follow))) folder_locate(FALSE);
        return;
    }
    gboolean directory = FALSE;
    gchar *selected = folder_selected_path(&directory);
    GHashTable *expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    FolderVisit visit =
    {
        .expanded = expanded
    };
    gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree), folder_visit, &visit);
    wb.tree_setting = TRUE;
    gtk_tree_store_clear(wb.project_tree);
    GHashTable *nodes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gchar *root = wb.root ? utils_get_utf8_from_locale(wb.root) : g_strdup("");
    guint shown = 0;
    for (guint i = 0;i<wb.files->len && i<10000;i++)
    {
        const char *path = g_ptr_array_index(wb.files, i);
        if (!insight_lookup_filter_match(filter, NULL, NULL, path, NULL)) continue;
        gchar *parent = g_path_get_dirname(path), *name = g_path_get_basename(path);
        GtkTreeIter *above = tree_directory(nodes, parent, root, 0), iter;
        gtk_tree_store_append(wb.project_tree, &iter, above);
        gtk_tree_store_set(wb.project_tree, &iter, 0, name, 1, path, 2, FALSE, -1);
        shown++;
        g_free(parent);
        g_free(name);
    }
    gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree), folder_restore_expanded, expanded);
    if (*gtk_entry_get_text(GTK_ENTRY(wb.tree_entry))) gtk_tree_view_expand_all(GTK_TREE_VIEW(wb.tree_view));
    if (selected)
    {
        FolderVisit restore =
        {
            .wanted = selected
        };
        gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree), folder_visit, &restore);
        if (restore.found)
        {
            gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.tree_view)), restore.found);
            gtk_tree_path_free(restore.found);
        }
    }
    wb.tree_setting = FALSE;
    folder_count(shown, wb.files->len);
    g_free(selected);
    g_free(root);
    g_hash_table_destroy(nodes);
    g_hash_table_destroy(expanded);
    insight_lookup_filter_free(filter);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.tree_follow))) folder_locate(FALSE);
}

/* EN: Apply the deferred folder filter. data is unused.
 * RU: Применяет отложенный фильтр каталогов. data не используется. */
static gboolean folder_filter_idle(gpointer data)
{
    (void)data;
    wb.tree_filter_idle = 0;
    if (wb.alive) tree_refresh();
    return G_SOURCE_REMOVE;
}

/* EN: Schedule a refresh after the folder filter changes.
 * RU: Планирует обновление после изменения фильтра папок. */
static void folder_filter_changed(GtkEditable *entry, gpointer data)
{
    (void)entry;
    (void)data;
    if (wb.tree_filter_idle)
        g_source_remove(wb.tree_filter_idle);
    wb.tree_filter_idle = g_timeout_add(120, folder_filter_idle, NULL);
}

/* EN: Update the source preview for selection.
 * RU: Обновляет предпросмотр по selection. */
static void tree_selected(GtkTreeSelection *selection, gpointer data)
{
    (void)data;
    if (wb.tree_setting && gtk_tree_selection_get_tree_view(selection) == GTK_TREE_VIEW(wb.tree_view)) return;
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    gchar *path;
    gboolean directory;
    gtk_tree_model_get(model, &iter, 1, &path, 2, &directory, -1);
    if (!directory)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow), FALSE);
        show_location(path, 0, 1, TRUE);
    }
    g_free(path);
}

/* EN: Open or expand the row at path in view.
 * RU: Открывает или раскрывает строку по path в view. */
static void tree_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data)
{
    (void)column;
    (void)data;
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    if (!gtk_tree_model_get_iter(model, &iter, path)) return;
    gboolean directory;
    gtk_tree_model_get(model, &iter, 2, &directory, -1);
    if (directory)
    {
        if (gtk_tree_view_row_expanded(view, path)) gtk_tree_view_collapse_row(view, path);
        else gtk_tree_view_expand_row(view, path, FALSE);
        return;
    }
    GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
    gboolean was_setting = wb.tree_setting;
    GroupedPanel *grouped = g_object_get_data(G_OBJECT(view), "insight-grouped-panel");
    gboolean grouped_setting = grouped ? grouped->setting : FALSE;
    if (grouped) grouped->setting = TRUE;
    if (GTK_WIDGET(view) == wb.tree_view)
        wb.tree_setting = TRUE;
    gtk_tree_selection_select_path(selection, path);
    wb.tree_setting = was_setting;
    if (grouped) grouped->setting = grouped_setting;
    gchar *source = NULL;
    gtk_tree_model_get(model, &iter, 1, &source, -1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow), FALSE);
    if (source && show_location(source, 0, 1, TRUE)) open_context(NULL, NULL);
    g_free(source);
}
enum
{
    FOLDER_OPEN, FOLDER_PATH, FOLDER_RELATIVE, FOLDER_LOCATE, FOLDER_EXPAND, FOLDER_COLLAPSE, FOLDER_EXPAND_ALL, FOLDER_COLLAPSE_ALL, FOLDER_CLEAR, FOLDER_HELP
};
