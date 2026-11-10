/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Grouped panel settings, filters and file type view.
 * RU: Настройки и фильтры сгруппированных панелей, список типов файлов. */


/* EN: Write the settings for p to settings.
 * RU: Записывает настройки панели p в settings. */
static void grouped_write(GroupedPanel *p, GKeyFile *settings)
{
    insight_symbol_view_columns_write(GTK_TREE_VIEW(p->view), settings, grouped_prefix(p));
    gint column;
    GtkSortType order;
    if (gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(p->store), &column, &order))
    {
        gchar *key = g_strconcat(grouped_prefix(p), "-sort-column", NULL);
        g_key_file_set_integer(settings, "workbench", key, column);
        g_free(key);
        key = g_strconcat(grouped_prefix(p), "-sort-descending", NULL);
        g_key_file_set_boolean(settings, "workbench", key, order == GTK_SORT_DESCENDING);
        g_free(key);
    }
}

/* EN: Load panel p settings from settings.
 * RU: Загружает настройки панели p из settings. */
static void grouped_read(GroupedPanel *p, GKeyFile *settings)
{
    insight_symbol_view_columns_read(GTK_TREE_VIEW(p->view), settings, grouped_prefix(p));
    gchar *key = g_strconcat(grouped_prefix(p), "-sort-column", NULL);
    GError *error = NULL;
    gint column = g_key_file_get_integer(settings, "workbench", key, &error);
    g_free(key);
    if (!error && column >= 0 && column<(p->symbols ? SYMBOL_COLUMNS : 2))
    {
        key = g_strconcat(grouped_prefix(p), "-sort-descending", NULL);
        gboolean reverse = g_key_file_get_boolean(settings, "workbench", key, NULL);
        g_free(key);
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(p->store), column, reverse ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
    }
    g_clear_error(&error);
}

/* EN: Build a filter for panel p and report invalid input in its entry.
 * RU: Создаёт фильтр панели p и показывает ошибку в поле ввода. */
static InsightLookupFilter *grouped_filter(GroupedPanel *p)
{
    InsightLookupFilter *filter = entry_filter(p->entry, !p->symbols);
    if (filter) gtk_style_context_remove_class(gtk_widget_get_style_context(p->entry), "error");
    else gtk_style_context_add_class(gtk_widget_get_style_context(p->entry), "error");
    return filter;
}

/* EN: Refresh the file type groups for the current project.
 * RU: Обновляет группы типов файлов текущего проекта. */
static void file_types_refresh(void)
{
    GroupedPanel *p = &wb.type_panel;
    if (!p->store || !gtk_widget_get_mapped(p->view)) return;
    InsightLookupFilter *filter = grouped_filter(p);
    if (!filter) return;
    GHashTable *expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gchar *selected = grouped_capture(p, expanded);
    gtk_tree_store_clear(p->store);
    GHashTable *groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    guint shown = 0;
    for (guint i = 0;i<wb.files->len && i<10000;i++)
    {
        const char *path = g_ptr_array_index(wb.files, i);
        if (!insight_lookup_filter_match(filter, NULL, NULL, path, NULL)) continue;
        GeanyDocument *doc = document_find_by_filename(path);
        GeanyFiletype *ft = doc ? doc->file_type : insight_languages_detect(path);
        if (!ft) ft = filetypes_detect_from_extension(path);
        gchar *name = insight_languages_profile_name(doc ? doc->editor->sci : NULL, path, ft);
        if (!*name)
        {
            g_free(name);
            name = g_strdup("Other");
        }
        GtkTreeIter *group = g_hash_table_lookup(groups, name);
        if (!group)
        {
            group = g_new(GtkTreeIter, 1);
            gtk_tree_store_append(p->store, group, NULL);
            gtk_tree_store_set(p->store, group, 0, name, 2, TRUE, -1);
            g_hash_table_insert(groups, g_strdup(name), group);
        }
        gchar *base = g_path_get_basename(path);
        GtkTreeIter row;
        gtk_tree_store_append(p->store, &row, group);
        gtk_tree_store_set(p->store, &row, 0, base, 1, path, 2, FALSE, -1);
        shown++;
        g_free(base);
        g_free(name);
    }
    g_hash_table_destroy(groups);
    insight_lookup_filter_free(filter);
    grouped_count(p, shown, wb.files->len);
    grouped_restore(p, expanded, selected);
}

/* EN: Create the file type panel and connect its controls.
 * RU: Создаёт панель типов файлов и подключает её элементы управления. */
static void create_file_types(void)
{
    GroupedPanel *p = &wb.type_panel;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    p->entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(p->entry), "Filter files by path…");
    gtk_widget_set_tooltip_text(p->entry, "Use path fragments, glob, ?regex or ~fuzzy; empty groups are hidden");
    gtk_box_pack_start(GTK_BOX(box), p->entry, FALSE, FALSE, 0);
    wb.file_types = p->store = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    wb.types_view = p->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(p->store));
    const gchar *labels[] =
    {
        "File / type", "Path"
    };
    for (guint i = 0;i<G_N_ELEMENTS(labels);i++)
    {
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(labels[i], gtk_cell_renderer_text_new(), "text", i, NULL);
        gtk_tree_view_column_set_sort_column_id(c, i);
        gtk_tree_view_append_column(GTK_TREE_VIEW(p->view), c);
    }
    const gint widths[] =
    {
        220, 300
    };
    insight_symbol_view_columns_configure(GTK_TREE_VIEW(p->view), widths, G_N_ELEMENTS(widths));
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(p->view), 1);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(p->store), 0, GTK_SORT_ASCENDING);
    grouped_attach(p, box);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), p->view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook), box, gtk_label_new("File Types"));
    g_signal_connect(p->view, "row-activated", G_CALLBACK(tree_activated), NULL);
    gtk_widget_show_all(box);
    file_types_refresh();
}
