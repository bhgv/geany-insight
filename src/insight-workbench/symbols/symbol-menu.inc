/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Symbol actions and context menu.
 * RU: Действия над символами и контекстное меню. */


/* EN: Run action for the selection in view.
 * RU: Выполняет action для выбранной строки в view. */
static void symbol_action(GtkTreeView *view, guint action)
{
    if (action != SP_HELP && !gtk_widget_get_sensitive(GTK_WIDGET(view))) return;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter iter;
    gboolean selected = gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view), NULL, &iter);
    gchar *name = NULL, *path = NULL, *scope = NULL;
    gulong line = 0;
    if (selected) gtk_tree_model_get(model, &iter, SYMBOL_NAME, &name, SYMBOL_PATH, &path, SYMBOL_SCOPE, &scope, SYMBOL_LINE, &line, -1);
    switch(action)
    {
        case SP_COPY_TEXT: case SP_CUT_TEXT: case SP_DUPLICATE: case SP_PASTE_TEXT: symbol_text_from_outline(view, action-SP_COPY_TEXT);
        break;
        case SP_OPEN: if (selected)
        {
            GtkTreePath *row = gtk_tree_model_get_path(model, &iter);
            browser_activated(view, row, NULL, NULL);
            gtk_tree_path_free(row);
        }
        break;
        case SP_INFO: if (line) browser_symbol_info(NULL, view);
        break;
        case SP_REFERENCES:
        if (line && name)
        {
            wb.relation_setting = TRUE;
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow), FALSE);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_buffer), FALSE);
            gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode), 0);
            gtk_entry_set_text(GTK_ENTRY(wb.usage_entry), name);
            gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry), "");
            wb.relation_setting = FALSE;
            relation_controls(NULL, NULL);
            focus_widget_panel(wb.usage_entry);
        }
        break;
        case SP_QUALIFIED:
        if (line && name)
        {
            gchar *native = path && path[0] != '@' ? utils_get_locale_from_utf8(path) : NULL;
            GeanyDocument *owner = native ? document_find_by_filename(native) : document_get_current();
            g_free(native);
            GeanyFiletype *ft = owner ? owner->file_type : (path ? filetypes_detect_from_extension(path) : NULL);
            const gchar *separator = owner && owner->tm_file ? tm_parser_scope_separator_printable(owner->tm_file->lang) : (ft ? tm_parser_scope_separator_printable(ft->lang) : NULL);
            gchar *qualified = scope && *scope ? g_strconcat(scope, separator && *separator ? separator : "::", name, NULL) : g_strdup(name);
            gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), qualified, -1);
            g_free(qualified);
        }
        break;
        case SP_PATH: if (line && path && path[0] != '@') gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), path, -1);
        break;
        case SP_LINE: if (line)
        {
            gchar *number = g_strdup_printf("%lu", line);
            gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), number, -1);
            g_free(number);
        }
        break;
        case SP_FILTER_NAME: case SP_FILTER_SCOPE: case SP_FILTER_FILE:
        if (line)
        {
            const gchar *value = action == SP_FILTER_NAME ? name : (action == SP_FILTER_SCOPE ? scope : path);
            if (value && (action != SP_FILTER_FILE || value[0] != '@'))
            {
                gchar *query = g_strdup_printf("%s\"%s\"", action == SP_FILTER_SCOPE ? "scope:" : (action == SP_FILTER_FILE ? "file:" : ""), value);
                if (strlen(query) <= 1024)
                {
                    GtkWidget *entry = GTK_WIDGET(view) == wb.local_view ? wb.local_entry : wb.symbol_entry;
                    gtk_entry_set_text(GTK_ENTRY(entry), query);
                    gtk_widget_grab_focus(entry);
                }
                else ui_set_statusbar(TRUE, "Selected value is too long for a lookup filter");
                g_free(query);
            }
        }
        break;
        case SP_CLEAR:
        {
            GtkWidget *entry = GTK_WIDGET(view) == wb.local_view ? wb.local_entry : wb.symbol_entry;
            gtk_entry_set_text(GTK_ENTRY(entry), "");
            gtk_widget_grab_focus(entry);
        }
        break;
        case SP_EXPAND_ONE: case SP_COLLAPSE_ONE:
        if (selected && insight_symbol_view_grouped(view))
        {
            GtkTreePath *row = gtk_tree_model_get_path(model, &iter);
            while (gtk_tree_path_get_depth(row)>1) gtk_tree_path_up(row);
            if (action == SP_EXPAND_ONE) gtk_tree_view_expand_row(view, row, TRUE);
            else gtk_tree_view_collapse_row(view, row);
            gtk_tree_path_free(row);
        }
        break;
        case SP_INVERT: insight_symbol_view_set_types(view, ~insight_symbol_view_types_mask(view));
        break;
        case SP_NAME: if (name) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), name, -1);
        break;
        case SP_LOCATION: if (line && path && path[0] != '@')
        {
            gchar *location = g_strdup_printf("%s:%lu", path, line);
            gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), location, -1);
            g_free(location);
        }
        break;
        case SP_ONLY: case SP_HIDE: insight_symbol_view_filter_selected(view, action == SP_ONLY);
        break;
        case SP_ALL: insight_symbol_view_set_types(view, G_MAXUINT);
        break;
        case SP_EXPAND: gtk_tree_view_expand_all(view);
        break;
        case SP_COLLAPSE: gtk_tree_view_collapse_all(view);
        break;
        case SP_MORE: insight_symbol_view_load_more(view);
        break;
        case SP_COLUMNS: insight_symbol_view_columns_reset(view);
        break;
        case SP_LOCATE: if (GTK_WIDGET(view) == wb.local_view) symbols_locate(NULL, NULL);
        break;
        case SP_HELP: insight_help_show(NULL, GTK_WIDGET(view) == wb.local_view ? "panel-file-symbols.md" : "panel-project-symbols.md");
        break;
        default: break;
    }
    g_free(name);
    g_free(path);
    g_free(scope);
}

/* EN: Dispatch the action on item to the view in data.
 * RU: Выполняет команду из item для представления из data. */
static void symbol_menu_action(GtkMenuItem *item, gpointer data)
{
    guint action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "insight-symbol-action"));
    if (action == SP_GROUP)
        insight_symbol_view_set_grouped(GTK_TREE_VIEW(data), gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)));
    else symbol_action(GTK_TREE_VIEW(data), action);
}

/* EN: Build a context menu for view.
 * RU: Создаёт контекстное меню для view. */
static GtkWidget *symbol_menu(GtkTreeView *view)
{
    GtkWidget *menu = g_object_get_data(G_OBJECT(view), "insight-symbol-popup");
    if (!menu)
    {
        menu = gtk_menu_new();
        gtk_widget_set_name(menu, "insight-symbol-popup");
        gtk_menu_attach_to_widget(GTK_MENU(menu), GTK_WIDGET(view), NULL);
        g_object_set_data_full(G_OBJECT(view), "insight-symbol-popup", g_object_ref_sink(menu), g_object_unref);
        const gchar *labels[] =
        {
            "Open definition", "Symbol Info…", "Find references to this symbol", "Copy symbol name", "Copy source location", "Show only this category", "Hide this category", "Show all symbol types", "Group by symbol type", "Expand all symbol groups", "Collapse all symbol groups", "Help for this panel", "Load more symbols", "Locate symbol at caret", "Reset symbol columns", "Copy qualified symbol name", "Copy source file path", "Copy source line number", "Filter by this symbol name", "Filter by this scope", "Filter by this source file", "Clear symbol query", "Expand selected symbol group", "Collapse selected symbol group", "Invert symbol type selection", "Visible symbol columns", "Copy Symbol", "Cut Symbol", "Duplicate Symbol", "Paste Symbol…"
        };
        const gchar *icons[] =
        {
            "go-jump", "dialog-information", "relations", "edit-copy", "edit-copy", "view-filter-symbolic", "view-filter-symbolic", "view-list-symbolic", "view-list-symbolic", "list-add", "list-remove", "panel-help", "list-add-symbolic", "find-location-symbolic", "view-refresh", "edit-copy", "edit-copy", "edit-copy", "edit-find", "view-filter-symbolic", "view-filter-symbolic", "edit-clear", "list-add", "list-remove", "object-flip-horizontal", "view-list-symbolic", "edit-copy", "edit-cut", "edit-copy", "edit-paste"
        };
        for (guint i = 0;i<SP_COUNT;i++)
        {
            if (i == SP_ONLY || i == SP_GROUP || i == SP_HELP) gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
            GtkWidget *item;
            if (i == SP_GROUP)
            {
                item = gtk_check_menu_item_new_with_label(labels[i]);
                GtkWidget *label = gtk_bin_get_child(GTK_BIN(item)), *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
                g_object_ref(label);
                gtk_container_remove(GTK_CONTAINER(item), label);
                gtk_box_pack_start(GTK_BOX(box), insight_command_icon(icons[i]), FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
                g_object_unref(label);
                gtk_container_add(GTK_CONTAINER(item), box);
                g_object_set_data(G_OBJECT(item), "insight-icon-added", GINT_TO_POINTER(1));
            }
            else if (i == SP_VISIBILITY)
            {
                item = gtk_image_menu_item_new_with_label(labels[i]);
                gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), insight_command_icon(icons[i]));
                gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), insight_symbol_view_columns_menu(view));
            }
            else
            {
                item = gtk_image_menu_item_new_with_label(labels[i]);
                gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), insight_command_icon(icons[i]));
                gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);
            }
            gchar *key = g_strdup_printf("insight-symbol-command-%u", i);
            gtk_widget_set_name(item, key);
            g_object_set_data(G_OBJECT(menu), key, item);
            g_free(key);
            g_object_set_data(G_OBJECT(item), "insight-symbol-action", GUINT_TO_POINTER(i));
            g_signal_connect_object(item, i == SP_GROUP ? "toggled" : "activate", G_CALLBACK(symbol_menu_action), view, 0);
            if (i >= SP_MORE)
            {
                GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
                gint position = (gint)g_list_length(children)-2;
                g_list_free(children);
                gtk_menu_shell_insert(GTK_MENU_SHELL(menu), item, position);
            }
            else gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        }
    }
    insight_list_tools_menu(view, menu);
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter iter;
    gboolean selected = gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view), NULL, &iter);
    gchar *path = NULL;
    gulong line = 0;
    if (selected) gtk_tree_model_get(model, &iter, SYMBOL_PATH, &path, SYMBOL_LINE, &line, -1);
    gboolean ready = gtk_widget_get_sensitive(GTK_WIDGET(view)), grouped = insight_symbol_view_grouped(view);
    for (guint i = 0;i<SP_COUNT;i++)
    {
        gchar *key = g_strdup_printf("insight-symbol-command-%u", i);
        GtkWidget *item = g_object_get_data(G_OBJECT(menu), key);
        g_free(key);
        gboolean enabled = ready;
        if (i == SP_OPEN || i == SP_INFO || i == SP_REFERENCES) enabled & = line != 0;
        if (i == SP_NAME || i == SP_ONLY || i == SP_HIDE) enabled & = selected;
        if (i == SP_LOCATION || i == SP_PATH || i == SP_FILTER_FILE) enabled & = line && path && path[0] != '@';
        if (i == SP_QUALIFIED || i == SP_LINE || i == SP_FILTER_NAME || i == SP_FILTER_SCOPE) enabled & = line != 0;
        if (i == SP_EXPAND_ONE || i == SP_COLLAPSE_ONE) enabled & = grouped && selected;
        if (i == SP_EXPAND || i == SP_COLLAPSE) enabled & = grouped;
        if (i == SP_MORE) enabled & = insight_symbol_view_has_more(view);
        if (i == SP_LOCATE)
        {
            enabled & = GTK_WIDGET(view) == wb.local_view;
            gtk_widget_set_no_show_all(item, GTK_WIDGET(view) != wb.local_view);
            gtk_widget_set_visible(item, GTK_WIDGET(view) == wb.local_view);
        }
        if (i >= SP_COPY_TEXT)
        {
            gboolean local = GTK_WIDGET(view) == wb.local_view;
            gtk_widget_set_no_show_all(item, !local);
            gtk_widget_set_visible(item, local);
            enabled & = local && line && document_get_current() && document_get_current()->id == wb.local_doc_id;
            if (i != SP_COPY_TEXT && document_get_current()) enabled & = !document_get_current()->readonly && !SSM(document_get_current()->editor->sci, SCI_GETREADONLY, 0, 0);
        }
        if (i == SP_HELP) enabled = TRUE;
        gtk_widget_set_sensitive(item, enabled);
        if (i == SP_GROUP) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), grouped);
    }
    g_free(path);
    insight_i18n_apply(menu);
    gtk_widget_show_all(menu);
    return menu;
}

/* EN: Show the symbol menu beside widget. data is unused.
 * RU: Открывает меню рядом с widget. data не используется. */
static gboolean symbol_popup_key(GtkWidget *widget, gpointer data)
{
    (void)data;
    GtkWidget *menu = symbol_menu(GTK_TREE_VIEW(widget));
    GdkEvent *event = gtk_get_current_event();
    gtk_menu_popup_at_widget(GTK_MENU(menu), widget, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, event);
    if (event) gdk_event_free(event);
    return TRUE;
}

/* EN: Handle a secondary click event on widget.
 * RU: Обрабатывает правый щелчок event в widget. */
static gboolean symbol_popup_button(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)data;
    if (event->button != 3 || event->type != GDK_BUTTON_PRESS)
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
    gtk_menu_popup_at_pointer(GTK_MENU(symbol_menu(view)), (GdkEvent*)event);
    return TRUE;
}
