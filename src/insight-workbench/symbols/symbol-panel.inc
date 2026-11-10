/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Keyboard routing and project symbol panel.
 * RU: Маршрутизация клавиш и панель символов проекта. */


/* EN: Route event to the focused panel. object and data are unused.
 * RU: Направляет event в активную панель. object и data не используются. */
static gboolean symbol_key_global(GObject *object, GdkEventKey *event, gpointer data)
{
    (void)object;
    (void)data;
    if (!wb.alive)
        return FALSE;
    if (insight_workbench_source_link_key(event))
        return TRUE;
    GtkWidget *relation_view = wb.usage_view && gtk_widget_has_focus(wb.usage_view) ? wb.usage_view : (wb.relation_tree && gtk_widget_has_focus(GTK_WIDGET(insight_relation_tree_view(wb.relation_tree))) ? GTK_WIDGET(insight_relation_tree_view(wb.relation_tree)) : NULL);
    if (relation_view && relation_node_key(relation_view, event, NULL))
        return TRUE;
    if (wb.marks_view && gtk_widget_has_focus(wb.marks_view))
        return bookmark_key(wb.marks_view, event, NULL);
    if (wb.marks_entry && gtk_widget_has_focus(wb.marks_entry))
        return bookmark_key(wb.marks_entry, event, NULL);
    if (wb.tree_view && gtk_widget_has_focus(wb.tree_view))
        return folder_key(wb.tree_view, event, NULL);
    if (wb.tree_entry && gtk_widget_has_focus(wb.tree_entry))
        return folder_key(wb.tree_entry, event, NULL);
    GroupedPanel *panels[] =
    {
        &wb.type_panel, &wb.category_panel
    };
    for (guint i = 0;i<G_N_ELEMENTS(panels);i++)
    {
        GroupedPanel *p = panels[i];
        if (p->view && gtk_widget_has_focus(p->view))
            return grouped_key_press(p->view, event, p);
        if (p->entry && gtk_widget_has_focus(p->entry))
            return grouped_key_press(p->entry, event, p);
    }
    GtkWidget *view = wb.local_view && gtk_widget_has_focus(wb.local_view) ? wb.local_view : (wb.symbol_view && gtk_widget_has_focus(wb.symbol_view) ? wb.symbol_view : (wb.file_view && gtk_widget_has_focus(wb.file_view) ? wb.file_view : NULL));
    return view ? browser_key(view, event, NULL) : FALSE;
}

/* EN: Connect context-menu and activation handlers to view.
 * RU: Подключает обработчики меню и активации к view. */
static void symbol_context_attach(GtkWidget *view)
{
    g_signal_connect_after(view, "button-release-event", G_CALLBACK(browser_symbol_clicked), NULL);
    g_signal_connect(view, "button-press-event", G_CALLBACK(symbol_popup_button), NULL);
    g_signal_connect(view, "popup-menu", G_CALLBACK(symbol_popup_key), NULL);
}

/* EN: Create the project symbol browser.
 * RU: Создаёт обозреватель символов проекта. */
static void create_browser(void)
{
    wb.browser = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    wb.symbol_entry = gtk_search_entry_new();
    gtk_widget_set_name(wb.symbol_entry, "insight-symbol-query");
    gtk_entry_set_placeholder_text(GTK_ENTRY(wb.symbol_entry), "Find project symbol…");
    wb.symbols = gtk_list_store_new(SYMBOL_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_ULONG, G_TYPE_STRING);
    wb.symbol_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.symbols));
    gtk_widget_set_name(wb.symbol_view, "insight-project-symbols");
    g_object_set_data(G_OBJECT(wb.symbol_view), "insight-bold-symbol-names", GINT_TO_POINTER(1));
    const char *titles[] =
    {
        "Symbol", "Scope", "Project-relative file", "Line", "Kind"
    };
    for (int i = 0;i<SYMBOL_COLUMNS;i++)
    {
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(titles[i], gtk_cell_renderer_text_new(), "text", i, NULL);
        gtk_tree_view_column_set_sort_column_id(column, i);
        gtk_tree_view_append_column(GTK_TREE_VIEW(wb.symbol_view), column);
    }
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), wb.symbol_view);
    scroll = insight_virtual_list_wrap(scroll, GTK_TREE_VIEW(wb.symbol_view), wb.symbols, virtual_reload, NULL);
    wb.outline = gtk_check_button_new_with_label("Current file outline");
    gtk_widget_set_no_show_all(wb.outline, TRUE);
    g_signal_connect(wb.outline, "toggled", G_CALLBACK(outline_changed), NULL);
    gtk_box_pack_start(GTK_BOX(wb.browser), wb.outline, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(wb.browser), symbol_query_controls(wb.symbol_entry, FALSE), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(wb.browser), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(wb.browser), sort_selector(wb.symbols), FALSE, FALSE, 0);
    GtkWidget *group = insight_symbol_view_attach(GTK_TREE_VIEW(wb.symbol_view), wb.symbols, NULL);
    gtk_widget_set_name(group, "insight-project-group-types");
    gtk_box_pack_start(GTK_BOX(wb.browser), group, FALSE, FALSE, 0);
    GtkWidget *scopes = insight_symbol_view_scopes_control(GTK_TREE_VIEW(wb.symbol_view));
    gtk_widget_set_name(scopes, "insight-project-scope-hierarchy");
    gtk_box_pack_start(GTK_BOX(wb.browser), scopes, FALSE, FALSE, 0);
    GtkWidget *types = insight_symbol_view_types_control(GTK_TREE_VIEW(wb.symbol_view));
    gtk_widget_set_name(types, "insight-project-type-filters");
    gtk_box_pack_start(GTK_BOX(wb.browser), types, FALSE, FALSE, 0);
    insight_symbol_view_more_loader(GTK_TREE_VIEW(wb.symbol_view), G_CALLBACK(symbols_load_more), 500);
    GtkWidget *info = gtk_button_new_with_label("Symbol Info…");
    g_signal_connect(info, "clicked", G_CALLBACK(browser_symbol_info), wb.symbol_view);
    gtk_box_pack_start(GTK_BOX(wb.browser), info, FALSE, FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook), wb.browser, gtk_label_new("Project Symbols"));
    g_signal_connect(wb.symbol_entry, "changed", G_CALLBACK(browser_filter), NULL);
    g_signal_connect(wb.symbol_entry, "activate", G_CALLBACK(browser_entry_activate), NULL);
    g_signal_connect(wb.symbol_entry, "key-press-event", G_CALLBACK(browser_key), NULL);
    g_signal_connect(wb.symbol_view, "key-press-event", G_CALLBACK(browser_key), NULL);
    symbol_context_attach(wb.symbol_view);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.symbol_view)), "changed", G_CALLBACK(browser_selected), NULL);
    g_signal_connect(wb.symbol_view, "row-activated", G_CALLBACK(browser_activated), NULL);
    gtk_widget_show_all(wb.browser);
    browser_filter(NULL, NULL);
}
