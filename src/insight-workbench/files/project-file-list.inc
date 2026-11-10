/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: File browser commands and project file list updates.
 * RU: Команды обозревателя и обновление списка файлов проекта. */


/* EN: Handle a folder browsing command in entry.
 * RU: Обрабатывает команду просмотра папки из entry. */
static gboolean file_browse_command(GtkEntry *entry)
{
    const gchar *query = gtk_entry_get_text(entry);
    if (!wb.disk_browser || (strcmp(query, ".") && strcmp(query, "..") && strcmp(query, "**")))
        return FALSE;
    gboolean project = !strcmp(query, "**"), up = !strcmp(query, "..");
    gtk_entry_set_text(entry, "");
    if (project)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode), 0);
        focus_widget_panel(wb.file_entry);
        return TRUE;
    }
    gchar *root = wb.root ? utils_get_utf8_from_locale(wb.root) : g_get_current_dir(), *path = up ? g_path_get_dirname(root) : g_strdup(root);
    gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode), 1);
    insight_disk_browser_set_root(wb.disk_browser, path);
    g_free(path);
    g_free(root);
    focus_widget_panel(wb.disk_browser);
    gtk_widget_child_focus(wb.disk_browser, GTK_DIR_TAB_FORWARD);
    return TRUE;
}

/* EN: Handle a wildcard query in entry.
 * RU: Обрабатывает шаблон поиска из entry. */
static gboolean file_wildcard_command(GtkEntry *entry)
{
    const gchar *query = gtk_entry_get_text(entry);
    gchar *trimmed = g_strdup(query);
    g_strstrip(trimmed);
    gboolean clear = !strcmp(trimmed, "*");
    g_free(trimmed);
    if (clear)
    {
        gtk_entry_set_text(entry, "");
        return TRUE;
    }
    InsightLookupFilter *filter = insight_lookup_filter_new(query, TRUE, NULL);
    gboolean wildcard = insight_lookup_filter_has_wildcards(filter);
    insight_lookup_filter_free(filter);
    if (wildcard) symbol_history_record(GTK_WIDGET(entry));
    return wildcard;
}

/* EN: Dispatch the command from entry. data selects the target.
 * RU: Выполняет команду из entry. data выбирает цель. */
static void browser_entry_activate(GtkEntry *entry, gpointer data)
{
    (void)data;
    if (GTK_WIDGET(entry) == wb.local_entry && wb.local_mode && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode)) == 0)
    {
        GtkWidget *tree = gtk_bin_get_child(GTK_BIN(ui_lookup_widget(main_widgets.window, "scrolledwindow2")));
        if (GTK_IS_TREE_VIEW(tree))
        {
            GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter_first(model, &iter))
            {
                GtkTreeIter child;
                while (gtk_tree_model_iter_children(model, &child, &iter)) iter = child;
                GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), path, NULL, FALSE);
                gtk_tree_path_free(path);
                gtk_widget_grab_focus(tree);
            }
        }
        return;
    }
    if (GTK_WIDGET(entry) == wb.file_entry && (file_browse_command(entry) || file_wildcard_command(entry))) return;
    if (GTK_WIDGET(entry) == wb.local_entry || GTK_WIDGET(entry) == wb.symbol_entry || GTK_WIDGET(entry) == wb.file_entry) symbol_history_record(GTK_WIDGET(entry));
    GtkTreeView *view = GTK_TREE_VIEW(GTK_WIDGET(entry) == wb.local_entry ? wb.local_view : (GTK_WIDGET(entry) == wb.symbol_entry ? wb.symbol_view : wb.file_view));
    if (!gtk_widget_get_sensitive(GTK_WIDGET(view))) return;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(model, &iter))
    {
        GtkTreeIter child;
        while (gtk_tree_model_iter_children(model, &child, &iter)) iter = child;
        GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
        gtk_tree_view_expand_to_path(view, path);
        browser_activated(view, path, NULL, NULL);
        gtk_tree_path_free(path);
    }
}

/* EN: Refresh the symbol browser. button and data are unused.
 * RU: Обновляет список символов. button и data не используются. */
static void outline_changed(GtkToggleButton *button, gpointer data)
{
    (void)button;
    (void)data;
    browser_filter(NULL, NULL);
}

/* EN: Compare the project paths pointed to by a and b.
 * RU: Сравнивает пути проекта, на которые указывают a и b. */
static gint project_path_compare(gconstpointer a, gconstpointer b)
{
    return g_strcmp0(*(gchar *const *)a, *(gchar *const *)b);
}

/* EN: Refresh the file query and reset the view position if entry triggered it. data is unused.
 * RU: Обновляет запрос файлов и сбрасывает позицию при вводе в entry. data не используется. */
static void files_filter(GtkEditable *entry, gpointer data)
{
    (void)data;
    if (!wb.file_store) return;
    if (entry)
        insight_virtual_list_position(GTK_TREE_VIEW(wb.file_view), 0);
    g_ptr_array_sort(wb.files, project_path_compare);
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(wb.file_entry));
    g_checksum_update(sum, (const guchar *)query, strlen(query)+1);
    if (wb.root)
        g_checksum_update(sum, (const guchar *)wb.root, strlen(wb.root)+1);
    for (guint n = 0;n<wb.files->len;n++)
    {
        const gchar *path = g_ptr_array_index(wb.files, n);
        g_checksum_update(sum, (const guchar *)path, strlen(path)+1);
    }
    GeanyDocument *active = document_get_current();
    gchar *profile = active ? insight_languages_profile_name(active->editor->sci, active->file_name, active->file_type):g_strdup("");
    g_checksum_update(sum, (const guchar *)profile, strlen(profile)+1);
    g_free(profile);
    if (active && active->file_type)
        g_checksum_update(sum, (const guchar *)active->file_type->name, strlen(active->file_type->name)+1);
    if (!wb.workspace_from_project) for (guint n = 0;n<documents_array->len;n++)
    {
        GeanyDocument *d = g_ptr_array_index(documents_array, n);
        if (d->is_valid && d->file_name)
            g_checksum_update(sum, (const guchar *)d->file_name, strlen(d->file_name)+1);
    }
    const gchar *key = g_checksum_get_string(sum);
    gboolean same = !g_strcmp0(key, g_object_get_data(G_OBJECT(wb.file_store), "insight-request-key"));
    if (!same)
        g_object_set_data_full(G_OBJECT(wb.file_store), "insight-request-key", g_strdup(key), g_free);
    g_checksum_free(sum);
    if (same) return;
    SymbolListJob *j = symbol_list_begin(wb.file_store, wb.file_entry, 10000, TRUE);
    if (!j) return;
    j->paths = g_ptr_array_new_with_free_func(g_free);
    for (guint n = 0;n<wb.files->len;n++)
        g_ptr_array_add(j->paths, g_strdup(g_ptr_array_index(wb.files, n)));
    if (!wb.workspace_from_project) for (guint n = 0;n<documents_array->len;n++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, n);
        if (doc->is_valid && doc->file_name)
            g_ptr_array_add(j->paths, g_strdup(doc->file_name));
    }
    symbol_list_launch(wb.file_store, j);
}

/* EN: Update the preview for selection. data is unused.
 * RU: Обновляет предпросмотр для selection. data не используется. */
static void file_selected(GtkTreeSelection *selection, gpointer data)
{
    (void)data;
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!wb.preview || !gtk_widget_get_sensitive(GTK_WIDGET(gtk_tree_selection_get_tree_view(selection))) || !gtk_tree_selection_get_selected(selection, &model, &iter)) return;
    gchar *path;
    gtk_tree_model_get(model, &iter, 1, &path, -1);
    reveal_context();
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow), FALSE);
    request_file_preview(path);
    g_free(path);
}

/* EN: Run the file command identified by action.
 * RU: Выполняет команду над файлом по action. */
static void file_action(guint action)
{
    if (action == FP_HELP)
    {
        insight_help_show(NULL, "panel-project-files.md");
        return;
    }
    if (!gtk_widget_get_sensitive(wb.file_view)) return;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(wb.file_view));
    GtkTreeIter iter;
    gboolean selected = gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.file_view)), NULL, &iter);
    gchar *name = NULL, *path = NULL;
    if (selected) gtk_tree_model_get(model, &iter, 0, &name, 1, &path, -1);
    switch(action)
    {
        case FP_REVEAL: if (path) ui_reveal_file(path);
        break;
        case FP_OPEN: if (selected)
        {
            GtkTreePath *row = gtk_tree_model_get_path(model, &iter);
            browser_activated(GTK_TREE_VIEW(wb.file_view), row, NULL, NULL);
            gtk_tree_path_free(row);
        }
        break;
        case FP_NAME: case FP_PATH: if (selected) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), action == FP_NAME ? name : path, -1);
        break;
        case FP_FILTER: if (path)
        {
            gchar *query = g_strdup_printf("file:\"%s\"", path);
            if (strlen(query) <= 1024)
            {
                gtk_entry_set_text(GTK_ENTRY(wb.file_entry), query);
                gtk_widget_grab_focus(wb.file_entry);
            }
            else ui_set_statusbar(TRUE, "Selected value is too long for a lookup filter");
            g_free(query);
        }
        break;
        case FP_CLEAR: gtk_entry_set_text(GTK_ENTRY(wb.file_entry), "");
        gtk_widget_grab_focus(wb.file_entry);
        break;
        case FP_RESET: insight_symbol_view_columns_reset(GTK_TREE_VIEW(wb.file_view));
        break;
        default:break;
    }
    g_free(name);
    g_free(path);
}
