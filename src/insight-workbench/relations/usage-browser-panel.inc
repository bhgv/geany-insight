/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Usage browser panel and symbol information entry point.
 * RU: Панель использований и открытие сведений о символе. */


/* EN: Build the usage browser and connect its controls.
 * RU: Создаёт панель использований и подключает элементы управления. */
static void create_usage_browser(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4), *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    wb.usage_entry = gtk_search_entry_new();
    gtk_widget_set_name(wb.usage_entry, "insight-relation-query");
    gtk_entry_set_placeholder_text(GTK_ENTRY(wb.usage_entry), "Symbol or qualified owner…");
    wb.usage_status = gtk_label_new("Comments and strings included; no semantic scope resolution");
    gtk_widget_set_name(wb.usage_status, "insight-relation-status");
    gtk_label_set_line_wrap(GTK_LABEL(wb.usage_status), TRUE);
    wb.usage_buffer = gtk_check_button_new_with_label("Current buffer only");
    wb.usage_code = gtk_check_button_new_with_label("References: code only");
    gtk_widget_set_name(wb.usage_code, "insight-references-code-only");
    wb.usage_comments = gtk_combo_box_text_new();
    gtk_widget_set_name(wb.usage_comments, "insight-references-comments");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.usage_comments), "References: all text");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.usage_comments), "References: skip comments");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.usage_comments), "References: comments only");
    gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments), 0);
    gtk_widget_set_tooltip_text(wb.usage_comments, "Lexical comment regions; skip comments retains strings, code only excludes both. Unknown languages are skipped in filtered modes.");
    wb.relation_follow = gtk_check_button_new_with_label("Follow symbol in editor");
    gtk_widget_set_name(wb.relation_follow, "insight-relations-follow");
    wb.relation_enclosing = gtk_check_button_new_with_label("Track enclosing function");
    wb.relation_transitive = gtk_check_button_new_with_label("Include indirect inheritance (tags)");
    gtk_widget_set_name(wb.relation_transitive, "insight-relation-transitive");
    gtk_widget_set_sensitive(wb.relation_transitive, FALSE);
    wb.relation_mode = gtk_combo_box_text_new();
    gtk_widget_set_name(wb.relation_mode, "insight-relation-mode");
    const char *modes[] =
    {
        "References (lexical)", "Definitions / declarations", "Direct members (tags)", "Text search (project)", "Base types (tags)", "Derived types (tags)", "Calls (current function, lexical)", "Call sites (project, lexical)", "References (Clang C/C++)", "Callees (Clang tree)", "Callers (Clang tree)", "References (slang HDL, by name)", "Diagnostics (slang HDL)", "Module instances (slang HDL)"
    };
    for (guint i = 0;i<G_N_ELEMENTS(modes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.relation_mode), modes[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode), 0);
    GtkWidget *search = gtk_button_new_with_label("Search"), *cancel = gtk_button_new_with_label("Stop");
    gtk_box_pack_start(GTK_BOX(controls), search, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(controls), cancel, FALSE, FALSE, 0);
    GtkWidget *instances = gtk_button_new_from_icon_name("view-list-tree-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name(instances, "insight-hdl-module-tree");
    gtk_widget_set_tooltip_text(instances, insight_tr("Module instance tree from the current module"));
    g_signal_connect(instances, "clicked", G_CALLBACK(hdl_instance_search), NULL);
    gtk_box_pack_start(GTK_BOX(controls), instances, FALSE, FALSE, 0);
    const gchar *report_icons[] =
    {
        "view-list-symbolic", "bookmark-new-symbolic"
    }, *report_tips[] =
    {
        "View Relation Outline…", "Create Bookmarks from Relation Items…"
    };
    for (guint i = 0;i<2;i++)
    {
        GtkWidget *button = gtk_button_new_from_icon_name(report_icons[i], GTK_ICON_SIZE_MENU);
        gtk_widget_set_name(button, i ? "insight-relation-bookmarks":"insight-relation-outline");
        gtk_widget_set_tooltip_text(button, report_tips[i]);
        gtk_box_pack_start(GTK_BOX(controls), button, FALSE, FALSE, 0);
        g_signal_connect(button, "clicked", G_CALLBACK(relation_report_show), GUINT_TO_POINTER(i));
    }
    wb.matches = gtk_list_store_new(USAGE_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_ULONG, G_TYPE_ULONG, G_TYPE_ULONG, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_ULONG);
    wb.relation_filter_entry = gtk_search_entry_new();
    gtk_widget_set_tooltip_text(wb.relation_filter_entry, "Prefix with ~ for fuzzy subsequence matching");
    gtk_widget_set_name(wb.relation_filter_entry, "insight-relation-filter");
    gtk_entry_set_placeholder_text(GTK_ENTRY(wb.relation_filter_entry), "Filter results: symbol, file or source…");
    GtkTreeModel *filtered = gtk_tree_model_filter_new(GTK_TREE_MODEL(wb.matches), NULL);
    wb.relation_filter = GTK_TREE_MODEL_FILTER(filtered);
    gtk_tree_model_filter_set_visible_func(wb.relation_filter, relation_row_visible, NULL, NULL);
    GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(filtered);
    wb.usage_view = gtk_tree_view_new_with_model(sorted);
    g_object_unref(sorted);
    g_object_unref(filtered);
    gtk_widget_set_name(wb.usage_view, "insight-relations");
    GtkCellRenderer *relation_symbol_renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *relation_symbol_column = gtk_tree_view_column_new_with_attributes("Symbol", relation_symbol_renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(relation_symbol_column, relation_symbol_renderer, hdl_text_cell, GINT_TO_POINTER(USAGE_SYMBOL), NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view), relation_symbol_column);
    GtkCellRenderer *relation_file_renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *relation_file_column = gtk_tree_view_column_new_with_attributes("File", relation_file_renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(relation_file_column, relation_file_renderer, hdl_text_cell, GINT_TO_POINTER(USAGE_DISPLAY), NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view), relation_file_column);
    GtkCellRenderer *relation_line_renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *relation_line_column = gtk_tree_view_column_new_with_attributes("Line", relation_line_renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(relation_line_column, relation_line_renderer, hdl_position_cell, GINT_TO_POINTER(USAGE_LINE), NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view), relation_line_column);
    gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view), gtk_tree_view_column_new_with_attributes("Source / kind • scope", gtk_cell_renderer_text_new(), "text", USAGE_SNIPPET, NULL));
    GtkCellRenderer *hdl_column_renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *hdl_column = gtk_tree_view_column_new_with_attributes(insight_tr("Column"), hdl_column_renderer, NULL);
    gtk_tree_view_column_set_cell_data_func(hdl_column, hdl_column_renderer, hdl_position_cell, GINT_TO_POINTER(USAGE_COLUMN), NULL);
    insight_i18n_key(G_OBJECT(hdl_column), "title", "Column");
    gtk_tree_view_column_set_visible(hdl_column, FALSE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view), hdl_column);
    const gint columns[] =
    {
        USAGE_SYMBOL, USAGE_DISPLAY, USAGE_LINE, USAGE_SNIPPET
    };
    const gint widths[] =
    {
        100, 150, 55, 260
    };
    for (guint i = 0;i<G_N_ELEMENTS(columns);i++)
    {
        GtkTreeViewColumn *column = gtk_tree_view_get_column(GTK_TREE_VIEW(wb.usage_view), (gint)i);
        gtk_tree_view_column_set_sort_column_id(column, columns[i]);
        gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_column_set_fixed_width(column, widths[i]);
        gtk_tree_view_column_set_resizable(column, TRUE);
        GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
        for (GList *cell = cells;cell;cell = cell->next)
            g_object_set(cell->data, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
        g_list_free(cells);
    }
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.usage_view), USAGE_SNIPPET);
    GtkWidget *drill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2), *definitions = gtk_button_new_with_label("Definitions"), *sites = gtk_button_new_with_label("Call sites");
    gtk_widget_set_name(definitions, "insight-relation-definitions");
    gtk_widget_set_tooltip_text(definitions, "Show definitions for the selected result; choose among overloads");
    gtk_widget_set_tooltip_text(sites, "Search call sites for the selected result");
    wb.relation_history = g_ptr_array_new_with_free_func(relation_visit_free);
    wb.relation_future = g_ptr_array_new_with_free_func(relation_visit_free);
    wb.relation_back = gtk_button_new_from_icon_name("go-previous", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_name(wb.relation_back, "insight-relation-back");
    gtk_widget_set_tooltip_text(wb.relation_back, "Back to the previous relation query");
    gtk_widget_set_sensitive(wb.relation_back, FALSE);
    g_signal_connect(wb.relation_back, "clicked", G_CALLBACK(relation_back), NULL);
    gtk_box_pack_start(GTK_BOX(drill), wb.relation_back, FALSE, FALSE, 0);
    wb.relation_forward = gtk_button_new_from_icon_name("go-next", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_name(wb.relation_forward, "insight-relation-forward");
    gtk_widget_set_tooltip_text(wb.relation_forward, "Forward to the next relation query");
    gtk_widget_set_sensitive(wb.relation_forward, FALSE);
    g_signal_connect(wb.relation_forward, "clicked", G_CALLBACK(relation_forward), NULL);
    gtk_box_pack_start(GTK_BOX(drill), wb.relation_forward, FALSE, FALSE, 0);
    GtkWidget *history = gtk_menu_button_new(), *history_menu = gtk_menu_new();
    gtk_widget_set_name(history, "insight-relation-history");
    gtk_button_set_image(GTK_BUTTON(history), gtk_image_new_from_icon_name("document-open-recent", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(history, "Relation query history");
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(history), history_menu);
    g_signal_connect(history_menu, "show", G_CALLBACK(relation_history_show), NULL);
    gtk_box_pack_start(GTK_BOX(drill), history, FALSE, FALSE, 0);
    const gchar *node_icons[] =
    {
        "list-remove", "view-refresh"
    }, *node_tips[] =
    {
        "Remove selected node from view (Delete)", "Restore nodes by refreshing the query (F5)"
    }, *node_names[] =
    {
        "insight-relation-node-remove", "insight-relation-node-restore"
    };
    for (guint i = 0;i<2;i++)
    {
        GtkWidget *button = gtk_button_new_from_icon_name(node_icons[i], GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_name(button, node_names[i]);
        gtk_widget_set_tooltip_text(button, node_tips[i]);
        gtk_box_pack_start(GTK_BOX(drill), button, FALSE, FALSE, 0);
        g_signal_connect(button, "clicked", G_CALLBACK(relation_node_button), GUINT_TO_POINTER(i));
    }
    gtk_box_pack_start(GTK_BOX(drill), definitions, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(drill), sites, TRUE, TRUE, 0);
    g_signal_connect(definitions, "clicked", G_CALLBACK(relation_selected_query), GINT_TO_POINTER(1));
    g_signal_connect(sites, "clicked", G_CALLBACK(relation_selected_query), GINT_TO_POINTER(7));
    for (guint i = 0;i<2;i++)
    {
        GtkWidget *button = gtk_button_new_from_icon_name(i ? "go-up-symbolic":"go-down-symbolic", GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_name(button, i ? "insight-relation-callers":"insight-relation-callees");
        gtk_widget_set_tooltip_text(button, i ? "Browse callers of selected function (Clang)":"Browse callees of selected function (Clang)");
        gtk_widget_set_sensitive(button, insight_clang_available());
        gtk_box_pack_start(GTK_BOX(drill), button, FALSE, FALSE, 0);
        g_signal_connect(button, "clicked", G_CALLBACK(relation_selected_query), GINT_TO_POINTER(i ? 10:9));
    }
    g_signal_connect(wb.relation_filter_entry, "changed", G_CALLBACK(relation_filter_changed), NULL);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), wb.usage_view);
    wb.relation_stack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(wb.relation_stack), scroll, "list");
    gtk_stack_add_named(GTK_STACK(wb.relation_stack), hdl_instance_widget(), "hdl-instances");
    wb.relation_routes = insight_source_routes_new();
    gtk_stack_add_named(GTK_STACK(wb.relation_stack), wb.relation_routes, "routes");
    g_signal_connect(insight_source_routes_back_button(wb.relation_routes), "clicked", G_CALLBACK(relation_routes_back), NULL);
    g_signal_connect(wb.relation_stack, "notify::visible-child-name", G_CALLBACK(relation_stack_changed), NULL);
    wb.relation_tree = insight_relation_tree_new();
    gtk_stack_add_named(GTK_STACK(wb.relation_stack), wb.relation_tree, "tree");
    GtkTreeView *tree_view = insight_relation_tree_view(wb.relation_tree);
    g_signal_connect(gtk_tree_view_get_selection(tree_view), "changed", G_CALLBACK(relation_tree_selected), NULL);
    g_signal_connect(tree_view, "row-activated", G_CALLBACK(relation_tree_activated), NULL);
    wb.relation_tree_toggle = gtk_check_button_new_with_label("Expandable relation tree");
    gtk_widget_set_name(wb.relation_tree_toggle, "insight-relation-tree-toggle");
    gtk_widget_set_sensitive(wb.relation_tree_toggle, FALSE);
    gtk_box_pack_start(GTK_BOX(controls), wb.relation_tree_toggle, FALSE, FALSE, 0);
    g_signal_connect(wb.relation_tree_toggle, "toggled", G_CALLBACK(relation_tree_toggle), NULL);
    wb.relation_graph = insight_relation_graph_new(GTK_TREE_VIEW(wb.usage_view), GTK_ENTRY(wb.usage_entry), USAGE_SYMBOL, USAGE_DISPLAY, USAGE_LINE);
    insight_relation_graph_set_query_handler(wb.relation_graph, relation_graph_query, NULL);
    insight_relation_graph_set_node_handler(wb.relation_graph, relation_node_action, NULL);
    gtk_stack_add_named(GTK_STACK(wb.relation_stack), wb.relation_graph, "graph");
    gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "list");
    wb.relation_tree_graph = insight_relation_graph_new_tree(tree_view, GTK_ENTRY(wb.usage_entry), USAGE_SYMBOL, USAGE_DISPLAY, USAGE_LINE);
    insight_relation_graph_set_query_handler(wb.relation_tree_graph, relation_graph_query, NULL);
    insight_relation_graph_set_node_handler(wb.relation_tree_graph, relation_node_action, NULL);
    gtk_stack_add_named(GTK_STACK(wb.relation_stack), wb.relation_tree_graph, "treegraph");
    wb.relation_graph_toggle = gtk_check_button_new_with_label("Graph view");
    gtk_widget_set_name(wb.relation_graph_toggle, "insight-relation-graph-toggle");
    gtk_box_pack_start(GTK_BOX(controls), wb.relation_graph_toggle, FALSE, FALSE, 0);
    g_signal_connect(wb.relation_graph_toggle, "toggled", G_CALLBACK(relation_graph_toggle), NULL);
    GtkWidget *routes = gtk_button_new_with_label("Source routes / flow"), *relations = gtk_button_new_with_label("Relations");
    gtk_widget_set_name(routes, "insight-relation-source-routes");
    gtk_widget_set_tooltip_text(routes, "Graphs of source locations and function control flow");
    gtk_box_pack_start(GTK_BOX(controls), routes, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), relations, FALSE, FALSE, 0);
    g_signal_connect(routes, "clicked", G_CALLBACK(source_routes_show), NULL);
    g_signal_connect(relations, "clicked", G_CALLBACK(relation_routes_back), NULL);
    GtkWidget *next_view = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_name(next_view, "insight-relation-next-view");
    gtk_widget_set_tooltip_text(next_view, "Next Relation Window View");
    gtk_box_pack_start(GTK_BOX(controls), next_view, FALSE, FALSE, 0);
    g_signal_connect(next_view, "clicked", G_CALLBACK(relation_next_view), NULL);
    gtk_box_pack_start(GTK_BOX(box), wb.usage_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wb.relation_mode, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wb.relation_transitive, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wb.relation_follow, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wb.relation_enclosing, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wb.usage_code, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wb.usage_comments, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wb.usage_buffer, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), controls, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wb.usage_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wb.relation_filter_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), drill, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wb.relation_stack, TRUE, TRUE, 0);
    GtkWidget *options = insight_compare_ui_tabs(box, "insight-relation-toolbar-tabs");
    wb.relation_options = options;
    GtkWidget *query = insight_compare_ui_page(options, "Query"), *filter = insight_compare_ui_page(options, "Filters"), *view = insight_compare_ui_page(options, "View"), *navigation = insight_compare_ui_page(options, "Navigation");
    GtkWidget *query_widgets[] =
    {
        wb.usage_entry, wb.relation_mode, wb.relation_follow, wb.relation_enclosing, wb.relation_transitive
    };
    for (guint n = 0;n<G_N_ELEMENTS(query_widgets);n++)
        insight_compare_ui_move(query_widgets[n], query);
    GtkWidget *filter_widgets[] =
    {
        wb.usage_code, wb.usage_comments, wb.usage_buffer, wb.relation_filter_entry
    };
    for (guint n = 0;n<G_N_ELEMENTS(filter_widgets);n++)
        insight_compare_ui_move(filter_widgets[n], filter);
    insight_compare_ui_move(controls, view);
    insight_compare_ui_move(drill, navigation);
    insight_ai_ui_fold(options, "Collapse");
    gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook), box, gtk_label_new("Relations"));
    g_signal_connect(search, "clicked", G_CALLBACK(usage_start), NULL);
    g_signal_connect(cancel, "clicked", G_CALLBACK(usage_cancel), NULL);
    g_signal_connect(wb.usage_entry, "activate", G_CALLBACK(usage_start), NULL);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.usage_view)), "changed", G_CALLBACK(usage_selected), NULL);
    g_signal_connect(wb.usage_view, "row-activated", G_CALLBACK(usage_activated), NULL);
    g_signal_connect(wb.usage_entry, "key-press-event", G_CALLBACK(browser_key), NULL);
    g_signal_connect(wb.usage_view, "key-press-event", G_CALLBACK(relation_node_key), NULL);
    g_signal_connect(wb.usage_view, "key-press-event", G_CALLBACK(browser_key), NULL);
    g_signal_connect(wb.usage_view, "button-press-event", G_CALLBACK(relation_node_popup), NULL);
    g_signal_connect(tree_view, "key-press-event", G_CALLBACK(relation_node_key), NULL);
    g_signal_connect(tree_view, "button-press-event", G_CALLBACK(relation_node_popup), NULL);
    g_signal_connect(wb.relation_transitive, "toggled", G_CALLBACK(relation_controls), NULL);
    g_signal_connect(wb.relation_mode, "changed", G_CALLBACK(relation_controls), NULL);
    g_signal_connect(wb.relation_follow, "toggled", G_CALLBACK(relation_follow_changed), NULL);
    g_signal_connect(wb.relation_enclosing, "toggled", G_CALLBACK(relation_controls), NULL);
    g_signal_connect(wb.usage_code, "toggled", G_CALLBACK(reference_region_changed), NULL);
    g_signal_connect(wb.usage_comments, "changed", G_CALLBACK(reference_region_changed), NULL);
    g_signal_connect(wb.usage_buffer, "toggled", G_CALLBACK(relation_controls), NULL);
    gtk_widget_show_all(box);
}

/* EN: Show information for the selected symbol.
 * RU: Показывает сведения о выбранном символе. */
static void browser_symbol_info(GtkWidget *widget, gpointer data)
{
    (void)widget;
    GtkTreeView *view = GTK_TREE_VIEW(data);
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view), &model, &iter))
    {
        ui_set_statusbar(TRUE, "Select a symbol first");
        return;
    }
    gchar *name = NULL, *path = NULL;
    gulong line = 0;
    gtk_tree_model_get(model, &iter, SYMBOL_NAME, &name, SYMBOL_PATH, &path, SYMBOL_LINE, &line, -1);
    if (!line)
    {
        g_free(name);
        g_free(path);
        ui_set_statusbar(TRUE, "Select a symbol, not a type group");
        return;
    }
    if (GTK_WIDGET(view) == wb.local_view && (!path || !strcmp(path, "@untitled")))
    {
        g_free(path);
        path = g_strdup_printf("@buffer:%u", wb.local_doc_id);
    }
    if (name) info_show_query(name, path, line);
    g_free(name);
    g_free(path);
}
typedef struct
{
    const TMTag *tag;
    GtkTreePath *path;
}
SymbolLocate;
