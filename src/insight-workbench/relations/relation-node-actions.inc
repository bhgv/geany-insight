/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Relation queries and commands on tree nodes.
 * RU: Запросы связей и команды для узлов дерева. */


/* EN: Run the selected query using widget and data.
 * RU: Запускает выбранный запрос по widget и data. */
static void relation_selected_query(GtkWidget *widget, gpointer data)
{
    (void)widget;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean tree_view = g_str_has_prefix(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)), "tree");
    GtkTreeView *view = tree_view ? insight_relation_tree_view(wb.relation_tree) : GTK_TREE_VIEW(wb.usage_view);
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view), &model, &iter))
    {
        ui_set_statusbar(TRUE, "Select a relation row first");
        return;
    }
    gchar *name = NULL;
    gulong line = 0;
    gtk_tree_model_get(model, &iter, USAGE_SYMBOL, &name, USAGE_LINE, &line, -1);
    if (tree_view && !line)
    {
        g_free(name);
        return;
    }
    if (!name || !*name)
    {
        g_free(name);
        return;
    }
    relation_visit_push(wb.relation_history, relation_visit_current());
    g_ptr_array_set_size(wb.relation_future, 0);
    relation_history_state();
    if (tree_view && (GPOINTER_TO_INT(data) == 9 || GPOINTER_TO_INT(data) == 10) && gtk_tree_model_get_n_columns(model)>13)
    {
        gchar *usr = NULL, *source = NULL;
        gtk_tree_model_get(model, &iter, 10, &usr, 13, &source, -1);
        if (usr && *usr)
        {
            g_object_set_data(G_OBJECT(wb.usage_entry), "insight-semantic-restore", GINT_TO_POINTER(1));
            g_object_set_data_full(G_OBJECT(wb.usage_entry), "insight-semantic-usr", usr, g_free);
            g_object_set_data_full(G_OBJECT(wb.usage_entry), "insight-semantic-source", source, g_free);
        }
        else
        {
            g_free(usr);
            g_free(source);
        }
    }
    wb.relation_setting = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow), FALSE);
    gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry), "");
    gtk_entry_set_text(GTK_ENTRY(wb.usage_entry), name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode), GPOINTER_TO_INT(data));
    wb.relation_setting = FALSE;
    g_free(name);
    relation_controls(NULL, NULL);
}

/* EN: Return whether view has a source selection.
 * RU: Проверяет, выбран ли исходник в view. */
static gboolean relation_source_selected(GtkTreeView *view)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gulong line = 0;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view), &model, &iter))
        return FALSE;
    gtk_tree_model_get(model, &iter, USAGE_LINE, &line, -1);
    return line>0;
}

/* EN: Open the selected node or restore its query, according to restore.
 * RU: Открывает выбранный узел или восстанавливает запрос по restore. */
static void relation_node_action(gboolean restore, gpointer data)
{
    (void)data;
    if (restore)
    {
        usage_start(NULL, NULL);
        return;
    }
    const gchar *shown = gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));
    gboolean tree = shown && g_str_has_prefix(shown, "tree");
    GtkTreeView *view = tree ? insight_relation_tree_view(wb.relation_tree) : GTK_TREE_VIEW(wb.usage_view);
    if (!relation_source_selected(view))
    {
        set_usage_status("Select a source node; query roots and notices cannot be removed");
        return;
    }
    wb.relation_setting = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow), FALSE);
    wb.relation_setting = FALSE;
    stop_usage();
    gboolean removed = FALSE;
    if (tree) removed = insight_relation_tree_remove_selected(wb.relation_tree);
    else
    {
        GtkTreeModel *model;
        GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view), &model, &iter))
        {
            /* Selection coordinates belong to the visible sorted/filtered model. */
            while (GTK_IS_TREE_MODEL_SORT(model) || GTK_IS_TREE_MODEL_FILTER(model))
            {
                GtkTreeIter child;
                if (GTK_IS_TREE_MODEL_SORT(model))
                {
                    gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(model), &child, &iter);
                    model = gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(model));
                }
                else
                {
                    gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &child, &iter);
                    model = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));
                }
                iter = child;
            }
            if (model == GTK_TREE_MODEL(wb.matches))
            {
                gtk_list_store_remove(wb.matches, &iter);
                removed = TRUE;
            }
        }
    }
    if (removed) set_usage_status("Node removed from view; refresh to restore");
}

/* EN: Run the node action selected by data. widget is unused.
 * RU: Выполняет действие над узлом из data. widget не используется. */
static void relation_node_button(GtkWidget *widget, gpointer data)
{
    (void)widget;
    relation_node_action(GPOINTER_TO_INT(data), NULL);
}

/* EN: Destroy and release the node menu from data.
 * RU: Уничтожает и освобождает меню узла из data. */
static void relation_node_menu_free(gpointer data)
{
    gtk_widget_destroy(GTK_WIDGET(data));
    g_object_unref(data);
}

/* EN: Create or return the context menu for view.
 * RU: Создаёт или возвращает контекстное меню для view. */
static GtkWidget *relation_node_menu(GtkTreeView *view)
{
    GtkWidget *menu = g_object_get_data(G_OBJECT(view), "insight-node-menu");
    if (!menu)
    {
        menu = gtk_menu_new();
        g_object_ref_sink(menu);
        const gchar *labels[] =
        {
            "Remove node from view", "Restore nodes (refresh)"
        };
        for (guint i = 0;i<2;i++)
        {
            GtkWidget *item = gtk_menu_item_new_with_label(labels[i]);
            gtk_widget_set_name(item, i ? "insight-relation-node-restore-menu" : "insight-relation-node-remove-menu");
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            g_signal_connect(item, "activate", G_CALLBACK(relation_node_button), GUINT_TO_POINTER(i));
        }
        const gchar *reports[] =
        {
            "View Relation Outline…", "Create Bookmarks from Relation Items…"
        };
        for (guint i = 0;i<2;i++)
        {
            GtkWidget *item = gtk_menu_item_new_with_label(reports[i]);
            gtk_widget_set_name(item, i ? "insight-relation-bookmark-menu":"insight-relation-outline-menu");
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            g_signal_connect(item, "activate", G_CALLBACK(relation_report_show), GUINT_TO_POINTER(i));
        }
        GtkWidget *print = gtk_menu_item_new_with_label("Print Relation Window…");
        gtk_widget_set_name(print, "insight-relation-print-menu");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), print);
        g_signal_connect(print, "activate", G_CALLBACK(relation_print_show), NULL);
        insight_list_tools_menu(view, menu);
        g_object_set_data_full(G_OBJECT(view), "insight-node-menu", menu, relation_node_menu_free);
    }
    GList *items = gtk_container_get_children(GTK_CONTAINER(menu));
    gtk_widget_set_sensitive(items->data, relation_source_selected(view));
    g_list_free(items);
    insight_i18n_apply(menu);
    gtk_widget_show_all(menu);
    return menu;
}

/* EN: Handle the node context menu shortcut in widget for event.
 * RU: Обрабатывает вызов меню узла в widget по event. */
static gboolean relation_node_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)data;
    if (!(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK)) && (event->keyval == GDK_KEY_Delete || event->keyval == GDK_KEY_KP_Delete || event->keyval == GDK_KEY_F5))
    {
        relation_node_action(event->keyval == GDK_KEY_F5, NULL);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Menu || (event->keyval == GDK_KEY_F10 && (event->state&GDK_SHIFT_MASK)))
    {
        gtk_menu_popup_at_widget(GTK_MENU(relation_node_menu(GTK_TREE_VIEW(widget))), widget, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, (GdkEvent *)event);
        return TRUE;
    }
    return FALSE;
}

/* EN: Show the node menu for a secondary click event in widget.
 * RU: Открывает меню узла по правому щелчку event в widget. */
static gboolean relation_node_popup(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)data;
    if (event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;
    GtkTreePath *path = NULL;
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x, (gint)event->y, &path, NULL, NULL, NULL))
    {
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, NULL, FALSE);
        gtk_tree_path_free(path);
    }
    else gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));
    gtk_widget_grab_focus(widget);
    gtk_menu_popup_at_pointer(GTK_MENU(relation_node_menu(GTK_TREE_VIEW(widget))), (GdkEvent *)event);
    return TRUE;
}

/* EN: Send the graph query in data using mode.
 * RU: Выполняет запрос графа из data в режиме mode. */
static void relation_graph_query(gint mode, gpointer data)
{
    (void)data;
    relation_selected_query(NULL, GINT_TO_POINTER(mode));
}

/* EN: Handle a change in selection.
 * RU: Обрабатывает изменение selection. */
static void relation_tree_selected(GtkTreeSelection *selection, gpointer data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gulong line = 0;
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) gtk_tree_model_get(model, &iter, USAGE_LINE, &line, -1);
    if (line) usage_selected(selection, data);
}

/* EN: Open the node at path in view.
 * RU: Открывает узел по path в view. */
static void relation_tree_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer data)
{
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter iter;
    gulong line = 0;
    if (!gtk_tree_model_get_iter(model, &iter, path)) return;
    gtk_tree_model_get(model, &iter, USAGE_LINE, &line, -1);
    if (line) usage_activated(view, path, column, data);
    else if (gtk_tree_view_row_expanded(view, path)) gtk_tree_view_collapse_row(view, path);
    else gtk_tree_view_expand_row(view, path, FALSE);
}
