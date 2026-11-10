/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Commands and context menus for grouped rows.
 * RU: Команды и контекстные меню для сгруппированных строк. */


/* EN: Run action on the current row of p.
 * RU: Выполняет action для текущей строки p. */
static void grouped_action(GroupedPanel *p, guint action)
{
    grouped_flush(p);
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    gchar *name = NULL, *path = NULL;
    gulong line = 0;
    gboolean selected = gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)), &model, &iter), leaf = selected && grouped_row(model, &iter, p, &name, &path, &line);
    switch(action)
    {
        case GP_OPEN: if (leaf)
        {
            GtkTreePath *row = gtk_tree_model_get_path(model, &iter);
            if (p->symbols) category_activated(GTK_TREE_VIEW(p->view), row, NULL, NULL);
            else tree_activated(GTK_TREE_VIEW(p->view), row, NULL, NULL);
            gtk_tree_path_free(row);
        }
        break;
        case GP_NAME: if (leaf) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), name, -1);
        break;
        case GP_PATH: if (leaf && path && path[0] != '@') gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), path, -1);
        break;
        case GP_LOCATION: if (leaf && path && path[0] != '@')
        {
            gchar *text = p->symbols ? g_strdup_printf("%s:%lu", path, line) : g_strdup(path);
            gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
            g_free(text);
        }
        break;
        case GP_INFO: if (leaf && p->symbols) symbol_action(GTK_TREE_VIEW(p->view), SP_INFO);
        break;
        case GP_REFERENCES: if (leaf && p->symbols) symbol_action(GTK_TREE_VIEW(p->view), SP_REFERENCES);
        break;
        case GP_FILTER: if (leaf)
        {
            gchar *query = g_strdup_printf(p->symbols ? "\"%s\"" : "file:\"%s\"", p->symbols ? name : path);
            if (strlen(query) <= 1024) gtk_entry_set_text(GTK_ENTRY(p->entry), query);
            else ui_set_statusbar(TRUE, "Selected value is too long for a lookup filter");
            g_free(query);
        }
        break;
        case GP_CLEAR: gtk_entry_set_text(GTK_ENTRY(p->entry), "");
        gtk_widget_grab_focus(p->entry);
        break;
        case GP_EXPAND: case GP_COLLAPSE:
        if (selected)
        {
            GtkTreePath *row = gtk_tree_model_get_path(model, &iter);
            if (leaf && !gtk_tree_model_iter_has_child(model, &iter)) gtk_tree_path_up(row);
            if (gtk_tree_path_get_depth(row))
            {
                if (action == GP_EXPAND) gtk_tree_view_expand_row(GTK_TREE_VIEW(p->view), row, TRUE);
                else gtk_tree_view_collapse_row(GTK_TREE_VIEW(p->view), row);
            }
            gtk_tree_path_free(row);
        }
        break;
        case GP_EXPAND_ALL: gtk_tree_view_expand_all(GTK_TREE_VIEW(p->view));
        break;
        case GP_COLLAPSE_ALL: gtk_tree_view_collapse_all(GTK_TREE_VIEW(p->view));
        break;
        case GP_RESET: insight_symbol_view_columns_reset(GTK_TREE_VIEW(p->view));
        break;
        case GP_MORE: if (p->symbols && p->limit<250000)
        {
            p->limit = MIN(250000, p->limit+5000);
            grouped_refresh(p);
        }
        break;
        case GP_HELP: insight_help_show(NULL, p->symbols ? "panel-categories.md" : "panel-file-types.md");
        break;
        default: break;
    }
    g_free(name);
    g_free(path);
}

/* EN: Run the command from item on the panel in data.
 * RU: Выполняет команду из item в панели из data. */
static void grouped_command(GtkMenuItem *item, gpointer data)
{
    grouped_action(data, GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "insight-grouped-action")));
}

/* EN: Create the context menu for p.
 * RU: Создаёт контекстное меню для p. */
static GtkWidget *grouped_menu(GroupedPanel *p)
{
    grouped_flush(p);
    GtkWidget *menu = gtk_menu_new();
    gtk_menu_attach_to_widget(GTK_MENU(menu), p->view, NULL);
    g_object_set_data(G_OBJECT(p->view), "insight-grouped-popup", menu);
    GtkTreeModel *model = NULL;
    GtkTreeIter iter;
    gchar *name = NULL, *path = NULL;
    gulong line = 0;
    gboolean selected = gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)), &model, &iter), leaf = selected && grouped_row(model, &iter, p, &name, &path, &line);
    const gchar *labels[] =
    {
        "Open selected source", "Copy selected name", "Copy source path", "Copy source location", "Symbol Info…", "Find usages…", "Filter to selected item", "Clear panel filter", "Expand selected branch", "Collapse selected branch", "Expand all groups", "Collapse all groups", "Visible columns", "Reset panel columns", "Load more category symbols", "Panel help"
    };
    for (guint i = 0;i<G_N_ELEMENTS(labels);i++)
    {
        if (!p->symbols && (i == GP_INFO || i == GP_REFERENCES || i == GP_MORE)) continue;
        GtkWidget *item = gtk_menu_item_new_with_label(labels[i]);
        gchar *id = g_strdup_printf("insight-grouped-action-%u", i);
        gtk_widget_set_name(item, id);
        g_free(id);
        g_object_set_data(G_OBJECT(item), "insight-grouped-action", GUINT_TO_POINTER(i));
        gboolean enabled = i <= GP_FILTER ? leaf : (i == GP_EXPAND || i == GP_COLLAPSE ? selected : (i == GP_MORE ? p->shown<p->total && p->limit<250000 : TRUE));
        if ((i == GP_PATH || i == GP_LOCATION) && (!path || path[0] == '@')) enabled = FALSE;
        gtk_widget_set_sensitive(item, enabled);
        if (i == GP_COLUMNS) gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), insight_symbol_view_columns_menu(GTK_TREE_VIEW(p->view)));
        else g_signal_connect(item, "activate", G_CALLBACK(grouped_command), p);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    g_free(name);
    g_free(path);
    g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
    insight_i18n_apply(menu);
    gtk_widget_show_all(menu);
    return menu;
}

/* EN: Show the menu for the panel in data. widget is unused.
 * RU: Открывает меню панели из data. widget не используется. */
static gboolean grouped_popup(GtkWidget *widget, gpointer data)
{
    (void)widget;
    GroupedPanel *p = data;
    gtk_menu_popup_at_widget(GTK_MENU(grouped_menu(p)), p->view, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
    return TRUE;
}

/* EN: Handle a click event on widget for the panel in data.
 * RU: Обрабатывает щелчок event по widget для панели из data. */
static gboolean grouped_button(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    GroupedPanel *p = data;
    if (event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;
    grouped_flush(p);
    p->setting = TRUE;
    GtkTreePath *row = NULL;
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x, (gint)event->y, &row, NULL, NULL, NULL))
    {
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), row, NULL, FALSE);
        gtk_tree_path_free(row);
    }
    else gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));
    p->setting = FALSE;
    gtk_widget_grab_focus(widget);
    gtk_menu_popup_at_pointer(GTK_MENU(grouped_menu(p)), (GdkEvent *)event);
    return TRUE;
}
