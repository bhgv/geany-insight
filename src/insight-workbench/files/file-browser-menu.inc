/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: File browser controls, menus and size display.
 * RU: Элементы обозревателя файлов, меню и отображение размера. */


/* EN: Dispatch the action attached to item. data is unused.
 * RU: Выполняет действие из item. data не используется. */
static void file_menu_action(GtkMenuItem *item, gpointer data)
{
    (void)data;
    file_action(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "insight-file-action")));
}

/* EN: Create the project file context menu.
 * RU: Создаёт контекстное меню файлов проекта. */
static GtkWidget *file_menu(void)
{
    GtkWidget *menu = g_object_get_data(G_OBJECT(wb.file_view), "insight-file-popup");
    if (!menu)
    {
        menu = gtk_menu_new();
        gtk_menu_attach_to_widget(GTK_MENU(menu), wb.file_view, NULL);
        g_object_set_data_full(G_OBJECT(wb.file_view), "insight-file-popup", g_object_ref_sink(menu), g_object_unref);
        const gchar *labels[] =
        {
            "Open file", "Copy file name", "Copy full file path", "Filter by this file", "Clear file query", "Reset file columns", "Visible file columns", "Help for this panel", "Show in file manager"
        }, *icons[] =
        {
            "go-jump", "edit-copy", "edit-copy", "edit-find", "edit-clear", "view-refresh", "view-list-symbolic", "panel-help", "folder-open"
        };
        for (guint i = 0;i<FP_COUNT;i++)
        {
            if (i == FP_FILTER || i == FP_RESET || i == FP_HELP) gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
            GtkWidget *item = symbol_history_item(labels[i], icons[i]);
            gchar *key = g_strdup_printf("insight-file-command-%u", i);
            gtk_widget_set_name(item, key);
            g_object_set_data(G_OBJECT(menu), key, item);
            g_free(key);
            g_object_set_data(G_OBJECT(item), "insight-file-action", GUINT_TO_POINTER(i));
            if (i == FP_COLUMNS) gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), insight_symbol_view_columns_menu(GTK_TREE_VIEW(wb.file_view)));
            else g_signal_connect(item, "activate", G_CALLBACK(file_menu_action), NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        }
    }
    insight_list_tools_menu(GTK_TREE_VIEW(wb.file_view), menu);
    gboolean ready = gtk_widget_get_sensitive(wb.file_view), selected = gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.file_view)))>0;
    for (guint i = 0;i<FP_COUNT;i++)
    {
        gchar *key = g_strdup_printf("insight-file-command-%u", i);
        GtkWidget *item = g_object_get_data(G_OBJECT(menu), key);
        g_free(key);
        gtk_widget_set_sensitive(item, i == FP_HELP || (ready && ((i>FP_FILTER && i != FP_REVEAL) || selected)));
    }
    insight_i18n_apply(menu);
    gtk_widget_show_all(menu);
    return menu;
}

/* EN: Show the file menu next to widget. data is unused.
 * RU: Открывает меню файлов рядом с widget. data не используется. */
static gboolean file_popup_key(GtkWidget *widget, gpointer data)
{
    (void)data;
    GdkEvent *event = gtk_get_current_event();
    gtk_menu_popup_at_widget(GTK_MENU(file_menu()), widget, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, event);
    if (event) gdk_event_free(event);
    return TRUE;
}

/* EN: Show the file menu on a secondary click event in widget. data is unused.
 * RU: Открывает меню по правому щелчку event в widget. data не используется. */
static gboolean file_popup_button(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)data;
    if (event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;
    GtkTreeView *view = GTK_TREE_VIEW(widget);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
    GtkTreePath *path = NULL;
    guint signal = g_signal_lookup("changed", GTK_TYPE_TREE_SELECTION);
    g_signal_handlers_block_matched(selection, G_SIGNAL_MATCH_ID, signal, 0, NULL, NULL, NULL);
    if (gtk_tree_view_get_path_at_pos(view, (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL))
    {
        gtk_tree_view_set_cursor(view, path, NULL, FALSE);
        gtk_tree_path_free(path);
    }
    else gtk_tree_selection_unselect_all(selection);
    g_signal_handlers_unblock_matched(selection, G_SIGNAL_MATCH_ID, signal, 0, NULL, NULL, NULL);
    gtk_widget_grab_focus(widget);
    gtk_menu_popup_at_pointer(GTK_MENU(file_menu()), (GdkEvent *)event);
    return TRUE;
}

/* EN: Render the file size at iter from model into renderer. column and data are unused.
 * RU: Показывает размер файла для iter в model через renderer. column и data не используются. */
static void project_file_size(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    (void)column;
    (void)data;
    guint64 size = 0;
    gboolean valid = FALSE;
    gtk_tree_model_get(model, iter, 3, &size, 4, &valid, -1);
    gchar *text = valid ? g_strdup_printf("%" G_GUINT64_FORMAT, size) : g_strdup("");
    g_object_set(renderer, "text", text, "xalign", 1.0f, NULL);
    g_free(text);
}

/* EN: Build the file browser panel and connect its controls.
 * RU: Создаёт панель файлов и подключает элементы управления. */
static void create_files_browser(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    wb.file_entry = gtk_search_entry_new();
    gtk_widget_set_name(wb.file_entry, "insight-file-query");
    gtk_entry_set_placeholder_text(GTK_ENTRY(wb.file_entry), "Find workspace file…");
    gtk_widget_set_tooltip_text(wb.file_entry, "Enter applies filename masks without opening; * clears the filter");
    wb.file_store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_BOOLEAN);
    wb.file_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.file_store));
    gtk_widget_set_name(wb.file_view, "insight-project-files");
    const gchar *file_labels[] =
    {
        "File", "Directory", "Size"
    };
    const gint file_fields[] =
    {
        0, 2, 3
    };
    for (guint i = 0;i<3;i++)
    {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        if (i == 0)
            g_object_set(renderer, "weight", PANGO_WEIGHT_BOLD, NULL);
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(file_labels[i], renderer, NULL);
        if (i == 2) gtk_tree_view_column_set_cell_data_func(column, renderer, project_file_size, NULL, NULL);
        else gtk_tree_view_column_add_attribute(column, renderer, "text", file_fields[i]);
        gtk_tree_view_column_set_sort_column_id(column, file_fields[i]);
        gtk_tree_view_append_column(GTK_TREE_VIEW(wb.file_view), column);
    }
    GtkTreeViewColumn *full_path = gtk_tree_view_column_new_with_attributes("Path", gtk_cell_renderer_text_new(), "text", 1, NULL);
    gtk_tree_view_column_set_sort_column_id(full_path, 1);
    gtk_tree_view_append_column(GTK_TREE_VIEW(wb.file_view), full_path);
    const gint widths[] =
    {
        220, 300, 260, 90
    };
    insight_symbol_view_columns_configure(GTK_TREE_VIEW(wb.file_view), widths, G_N_ELEMENTS(widths));
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.file_view), 1);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.file_store), 0, GTK_SORT_ASCENDING);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), wb.file_view);
    scroll = insight_virtual_list_wrap(scroll, GTK_TREE_VIEW(wb.file_view), wb.file_store, virtual_reload, NULL);
    gtk_box_pack_start(GTK_BOX(box), symbol_query_controls(wb.file_entry, FALSE), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), sort_selector(wb.file_store), FALSE, FALSE, 0);
    wb.file_status = gtk_label_new("");
    gtk_widget_set_name(wb.file_status, "insight-files-count");
    gtk_label_set_xalign(GTK_LABEL(wb.file_status), 0);
    gtk_widget_set_tooltip_text(wb.file_status, "Project Files");
    gtk_box_pack_start(GTK_BOX(box), wb.file_status, FALSE, FALSE, 0);
    g_signal_connect(wb.file_view, "button-press-event", G_CALLBACK(file_popup_button), NULL);
    g_signal_connect(wb.file_view, "popup-menu", G_CALLBACK(file_popup_key), NULL);
    GtkWidget *manage = gtk_button_new_with_label("Add and Remove Project Files…");
    g_signal_connect(manage, "clicked", G_CALLBACK(manage_project_files), NULL);
    gtk_box_pack_start(GTK_BOX(box), manage, FALSE, FALSE, 0);
    GtkTargetEntry targets[] =
    {
        {
            "text/uri-list", 0, 0
        }
    };
    gtk_drag_dest_set(wb.file_view, GTK_DEST_DEFAULT_ALL, targets, G_N_ELEMENTS(targets), GDK_ACTION_COPY);
    g_signal_connect(wb.file_view, "drag-data-received", G_CALLBACK(project_files_drop), NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook), box, gtk_label_new("Project Files"));
    gtk_notebook_reorder_child(GTK_NOTEBOOK(wb.project_notebook), box, 0);
    g_signal_connect(wb.file_entry, "changed", G_CALLBACK(files_filter), NULL);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.file_view)), "changed", G_CALLBACK(file_selected), NULL);
    g_signal_connect(wb.file_view, "row-activated", G_CALLBACK(browser_activated), NULL);
    g_signal_connect(wb.file_entry, "activate", G_CALLBACK(browser_entry_activate), NULL);
    g_signal_connect(wb.file_entry, "key-press-event", G_CALLBACK(browser_key), NULL);
    g_signal_connect(wb.file_view, "key-press-event", G_CALLBACK(browser_key), NULL);
    gtk_widget_show_all(box);
    files_filter(NULL, NULL);
}
enum
{
    USAGE_DISPLAY, USAGE_PATH, USAGE_LINE, USAGE_START, USAGE_END, USAGE_ID, USAGE_DIGEST, USAGE_SYMBOL, USAGE_SNIPPET, USAGE_COLUMN, USAGE_COLUMNS
};
