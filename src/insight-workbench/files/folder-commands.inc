/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Folder context commands and keyboard navigation.
 * RU: Контекстные команды папок и навигация с клавиатуры. */


/* EN: Apply action to the selected folder or file.
 * RU: Выполняет action для выбранной папки или файла. */
static void folder_action(guint action)
{
    gboolean directory = FALSE;
    gchar *path = folder_selected_path(&directory);
    switch(action)
    {
        case FOLDER_OPEN:
        if (path && !directory)
        {
            FolderVisit visit =
            {
                .wanted = path
            };
            gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree), folder_visit, &visit);
            if (visit.found)
            {
                tree_activated(GTK_TREE_VIEW(wb.tree_view), visit.found, NULL, NULL);
                gtk_tree_path_free(visit.found);
            }
        }
        break;
        case FOLDER_PATH: case FOLDER_RELATIVE:
        if (path)
        {
            gchar *copy = g_strdup(path);
            if (action == FOLDER_RELATIVE && wb.root)
            {
                gchar *root = utils_get_utf8_from_locale(wb.root);
                GFile *base = g_file_new_for_path(root), *file = g_file_new_for_path(path);
                gchar *relative = g_file_get_relative_path(base, file);
                if (relative)
                {
                    g_free(copy);
                    copy = relative;
                }
                g_object_unref(base);
                g_object_unref(file);
                g_free(root);
            }
            gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), copy, -1);
            g_free(copy);
        }
        break;
        case FOLDER_LOCATE: folder_locate(TRUE);
        break;
        case FOLDER_EXPAND: case FOLDER_COLLAPSE:
        if (path && directory)
        {
            FolderVisit visit =
            {
                .wanted = path
            };
            gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree), folder_visit, &visit);
            if (visit.found)
            {
                if (action == FOLDER_EXPAND) gtk_tree_view_expand_row(GTK_TREE_VIEW(wb.tree_view), visit.found, TRUE);
                else gtk_tree_view_collapse_row(GTK_TREE_VIEW(wb.tree_view), visit.found);
                gtk_tree_path_free(visit.found);
            }
        }
        break;
        case FOLDER_EXPAND_ALL: gtk_tree_view_expand_all(GTK_TREE_VIEW(wb.tree_view));
        break;
        case FOLDER_COLLAPSE_ALL: gtk_tree_view_collapse_all(GTK_TREE_VIEW(wb.tree_view));
        break;
        case FOLDER_CLEAR: gtk_entry_set_text(GTK_ENTRY(wb.tree_entry), "");
        gtk_widget_grab_focus(wb.tree_entry);
        break;
        case FOLDER_HELP: insight_help_show(NULL, "panel-project-folders.md");
        break;
    }
    g_free(path);
}

/* EN: Dispatch the folder action from data. item is unused.
 * RU: Выполняет действие над папкой из data. item не используется. */
static void folder_menu_action(GtkMenuItem *item, gpointer data)
{
    (void)item;
    folder_action(GPOINTER_TO_UINT(data));
}

/* EN: Create the folder context menu.
 * RU: Создаёт контекстное меню папок. */
static GtkWidget *folder_menu(void)
{
    GtkWidget *menu = gtk_menu_new();
    gtk_menu_attach_to_widget(GTK_MENU(menu), wb.tree_view, NULL);
    g_object_set_data(G_OBJECT(wb.tree_view), "insight-folder-popup", menu);
    gboolean directory = FALSE;
    gchar *path = folder_selected_path(&directory);
    const gchar *labels[] =
    {
        "Open project file", "Copy full folder/file path", "Copy project-relative path", "Locate current file in folders", "Expand selected folder branch", "Collapse selected folder branch", "Expand all folders", "Collapse all folders", "Clear folder filter", "Panel help"
    };
    for (guint i = 0;i<G_N_ELEMENTS(labels);i++)
    {
        GtkWidget *item = gtk_menu_item_new_with_label(labels[i]);
        gchar *name = g_strdup_printf("insight-folder-action-%u", i);
        gtk_widget_set_name(item, name);
        g_free(name);
        gtk_widget_set_sensitive(item, i == FOLDER_OPEN ? path && !directory : (i == FOLDER_PATH || i == FOLDER_RELATIVE ? path != NULL : (i == FOLDER_EXPAND || i == FOLDER_COLLAPSE ? path && directory : TRUE)));
        g_signal_connect(item, "activate", G_CALLBACK(folder_menu_action), GUINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    g_free(path);
    g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
    insight_i18n_apply(menu);
    gtk_widget_show_all(menu);
    return menu;
}

/* EN: Open the folder menu. widget and data are unused.
 * RU: Открывает меню папок. widget и data не используются. */
static gboolean folder_popup_key(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    gtk_menu_popup_at_widget(GTK_MENU(folder_menu()), wb.tree_view, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
    return TRUE;
}

/* EN: Handle mouse event on widget.
 * RU: Обрабатывает событие мыши event в widget. */
static gboolean folder_button(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)data;
    if (event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;
    wb.tree_setting = TRUE;
    GtkTreePath *row = NULL;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), (gint)event->x, (gint)event->y, &row, NULL, NULL, NULL))
    {
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), row, NULL, FALSE);
        gtk_tree_path_free(row);
    }
    else gtk_tree_selection_unselect_all(selection);
    wb.tree_setting = FALSE;
    gtk_widget_grab_focus(widget);
    gtk_menu_popup_at_pointer(GTK_MENU(folder_menu()), (GdkEvent *)event);
    return TRUE;
}

/* EN: Find the first file row in model.
 * RU: Находит первую строку файла в model. */
static gboolean folder_first_file(GtkTreeModel *model, GtkTreePath *row, GtkTreeIter *iter, gpointer data)
{
    gboolean directory = FALSE;
    gtk_tree_model_get(model, iter, 2, &directory, -1);
    if (directory)
        return FALSE;
    *(GtkTreePath **)data = gtk_tree_path_copy(row);
    return TRUE;
}

/* EN: Handle keyboard event in the folder widget.
 * RU: Обрабатывает клавишу event в widget дерева папок. */
static gboolean folder_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)data;
    guint mods = event->state&gtk_accelerator_get_default_mod_mask(), key = gdk_keyval_to_lower(event->keyval);
    if (widget == wb.tree_entry)
    {
        if (key == GDK_KEY_Escape && !mods)
        {
            if (*gtk_entry_get_text(GTK_ENTRY(widget))) gtk_entry_set_text(GTK_ENTRY(widget), "");
            else editor_focus();
            return TRUE;
        }
        if ((key == GDK_KEY_Down || key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) && !mods)
        {
            if (wb.tree_filter_idle)
            {
                g_source_remove(wb.tree_filter_idle);
                wb.tree_filter_idle = 0;
            }
            tree_refresh();
            if (gtk_style_context_has_class(gtk_widget_get_style_context(wb.tree_entry), "error"))
                return TRUE;
            GtkTreePath *row = NULL;
            gtk_tree_model_foreach(GTK_TREE_MODEL(wb.project_tree), folder_first_file, &row);
            if (row)
            {
                gtk_tree_view_expand_to_path(GTK_TREE_VIEW(wb.tree_view), row);
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(wb.tree_view), row, NULL, FALSE);
                gtk_tree_path_free(row);
                gtk_widget_grab_focus(wb.tree_view);
                if (key != GDK_KEY_Down) folder_action(FOLDER_OPEN);
            }
            return TRUE;
        }
        return FALSE;
    }
    if ((key == GDK_KEY_Menu && !mods) || (key == GDK_KEY_F10 && mods == GDK_SHIFT_MASK))
        return folder_popup_key(widget, NULL);
    if (key == GDK_KEY_f && mods == GEANY_PRIMARY_MOD_MASK)
    {
        gtk_widget_grab_focus(wb.tree_entry);
        return TRUE;
    }
    if (key == GDK_KEY_c && (mods == GEANY_PRIMARY_MOD_MASK || mods == (GEANY_PRIMARY_MOD_MASK|GDK_SHIFT_MASK)))
    {
        folder_action(mods&GDK_SHIFT_MASK ? FOLDER_RELATIVE : FOLDER_PATH);
        return TRUE;
    }
    if ((key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) && mods == GEANY_PRIMARY_MOD_MASK)
    {
        folder_action(FOLDER_OPEN);
        return TRUE;
    }
    if (key == GDK_KEY_l && mods == GEANY_PRIMARY_MOD_MASK)
    {
        folder_action(FOLDER_LOCATE);
        return TRUE;
    }
    if (key == GDK_KEY_Escape && !mods)
    {
        editor_focus();
        return TRUE;
    }
    return FALSE;
}
