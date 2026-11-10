/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Relation view switching and source route controls.
 * RU: Переключение представлений связей и управление маршрутами. */


/* EN: Refresh the current relation view and its controls.
 * RU: Обновляет текущее представление связей и его кнопки. */
static void relation_view_update(void)
{
    if (!wb.relation_tree_toggle || !wb.relation_stack) return;
    if (!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)), "routes")) return;
    gint mode = gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));
    if (wb.usage_view)
    {
        GtkTreeView *view = GTK_TREE_VIEW(wb.usage_view);
        GtkTreeViewColumn *extra = gtk_tree_view_get_column(view, 4);
        if (extra) gtk_tree_view_column_set_visible(extra, mode == 12);
        gtk_tree_view_column_set_title(gtk_tree_view_get_column(view, 0), insight_tr(mode == 12 ? "Severity":"Symbol"));
        gtk_tree_view_column_set_title(gtk_tree_view_get_column(view, 3), insight_tr(mode == 12 ? "Message / code":"Source / kind • scope"));
        insight_i18n_key(G_OBJECT(gtk_tree_view_get_column(view, 0)), "title", mode == 12 ? "Severity":"Symbol");
        insight_i18n_key(G_OBJECT(gtk_tree_view_get_column(view, 3)), "title", mode == 12 ? "Message / code":"Source / kind • scope");
    }
    gboolean semantic = mode == 9 || mode == 10;
    gboolean supported = mode == 2 || mode == 4 || mode == 5 || semantic;
    if (semantic && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle)))
    {
        gboolean setting = wb.relation_setting;
        wb.relation_setting = TRUE;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle), TRUE);
        wb.relation_setting = setting;
    }
    gtk_widget_set_sensitive(wb.relation_tree_toggle, supported && !semantic);
    gboolean tree = semantic || (supported && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle)));
    const gchar *old = gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));
    gboolean graph = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle));
    if (old && wb.relation_tree_graph && (!strcmp(old, "treegraph") || !strcmp(old, "graph")))
    {
        gboolean vertical = FALSE;
        gint zoom = 100;
        gboolean old_tree = !strcmp(old, "treegraph");
        insight_relation_graph_get_options(old_tree ? wb.relation_tree_graph : wb.relation_graph, &vertical, &zoom);
        insight_relation_graph_set_options(old_tree ? wb.relation_graph : wb.relation_tree_graph, vertical, zoom);
    }
    gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), tree ? (graph ? "treegraph" : "tree") : (graph ? "graph" : "list"));
}
/* Presentation changes deliberately do not dispatch usage_start: keep the
 * current query, loaded branches, filter and selection intact. */

/* EN: Switch to the next view. widget and data are unused.
 * RU: Переключает представление. widget и data не используются. */
static void relation_next_view(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    if (!wb.relation_stack || !wb.relation_graph_toggle) return;
    focus_widget_panel(wb.usage_entry);
    if (!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)), "routes"))
    {
        gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "list");
        relation_view_update();
    }
    gboolean graph = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle));
    gboolean tree = !g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)), "treegraph");
    gboolean vertical = FALSE;
    gint zoom = 100;
    insight_relation_graph_get_options(tree ? wb.relation_tree_graph : wb.relation_graph, &vertical, &zoom);
    if (!graph)
    {
        /* Read the displayed outline's graph settings before switching. */
        gboolean outline_tree = !g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)), "tree");
        insight_relation_graph_get_options(outline_tree ? wb.relation_tree_graph : wb.relation_graph, NULL, &zoom);
        insight_relation_graph_set_options(wb.relation_graph, FALSE, zoom);
        insight_relation_graph_set_options(wb.relation_tree_graph, FALSE, zoom);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle), TRUE);
    }
    else if (!vertical)
    {
        insight_relation_graph_set_options(wb.relation_graph, TRUE, zoom);
        insight_relation_graph_set_options(wb.relation_tree_graph, TRUE, zoom);
    }
    else gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle), FALSE);
}

/* EN: Show source routes using the mode in data.
 * RU: Показывает маршруты исходников в режиме из data. */
static void source_routes_show(GtkWidget *widget, gpointer data)
{
    (void)widget;
    focus_widget_panel(wb.relation_routes);
    gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "routes");
    relation_stack_changed(G_OBJECT(wb.relation_stack), NULL, NULL);
    if (GPOINTER_TO_INT(data))
        insight_source_routes_capture(wb.relation_routes);
}

/* EN: Dispatch the call path command in data.
 * RU: Выполняет команду маршрутов вызовов из data. */
static void call_paths_command(GtkWidget *widget, gpointer data)
{
    (void)widget;
    focus_widget_panel(wb.relation_routes);
    gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "routes");
    relation_stack_changed(G_OBJECT(wb.relation_stack), NULL, NULL);
    insight_source_routes_calls(wb.relation_routes, (InsightCallCommand)GPOINTER_TO_INT(data));
}

/* EN: Build a call path menu identified by prefix.
 * RU: Создаёт меню маршрутов вызовов с префиксом prefix. */
static GtkWidget *call_paths_menu(const gchar *prefix)
{
    GtkWidget *menu = gtk_menu_new();
    const gchar *labels[] =
    {
        "Open call path graph…", "Call path start from caret", "Call path target from caret", "Find call paths"
    };
    for (guint i = 0;i<G_N_ELEMENTS(labels);i++)
    {
        GtkWidget *item = gtk_menu_item_new_with_label(insight_tr(labels[i]));
        gchar *name = g_strdup_printf("%s-%u", prefix, i);
        gtk_widget_set_name(item, name);
        g_free(name);
        insight_i18n_key(G_OBJECT(item), "label", labels[i]);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_signal_connect(item, "activate", G_CALLBACK(call_paths_command), GUINT_TO_POINTER(i));
    }
    GtkWidget *help = gtk_menu_item_new_with_label(insight_tr("Call path tutorial…"));
    insight_i18n_key(G_OBJECT(help), "label", "Call path tutorial…");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), help);
    g_signal_connect(help, "activate", G_CALLBACK(insight_help_show), "source-routes.md");
    gtk_widget_show_all(menu);
    return menu;
}

/* EN: Append a call path submenu to parent using prefix.
 * RU: Добавляет подменю маршрутов к parent с префиксом prefix. */
static void append_call_paths_menu(GtkWidget *parent, const gchar *prefix)
{
    GtkWidget *item = gtk_menu_item_new_with_label(insight_tr("Call paths"));
    insight_i18n_key(G_OBJECT(item), "label", "Call paths");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), call_paths_menu(prefix));
    gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
    gtk_widget_show(item);
}

/* EN: Return from routes to the relation list.
 * RU: Возвращает из маршрутов к списку связей. */
static void relation_routes_back(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "list");
    relation_view_update();
}

/* EN: Select the view requested by widget and data.
 * RU: Выбирает представление по widget и data. */
static void relation_select_view(GtkWidget *widget, gpointer data)
{
    (void)widget;
    gint mode = GPOINTER_TO_INT(data);
    if (!wb.relation_stack) return;
    focus_widget_panel(wb.usage_entry);
    const gchar *shown = gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));
    gboolean tree = shown && g_str_has_prefix(shown, "tree");
    gint zoom = 100;
    if (!g_strcmp0(shown, "routes")) gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "list");
    insight_relation_graph_get_options(tree ? wb.relation_tree_graph:wb.relation_graph, NULL, &zoom);
    insight_relation_graph_set_options(wb.relation_graph, mode == 2, zoom);
    insight_relation_graph_set_options(wb.relation_tree_graph, mode == 2, zoom);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle), mode != 0);
    relation_view_update();
}

/* EN: Add a lock control to menu using follow, label and name.
 * RU: Добавляет в menu управление follow с подписью label и именем name. */
static void window_lock_item(GtkWidget *menu, GtkWidget *follow, const gchar *label, const gchar *name)
{
    GtkWidget *item = gtk_check_menu_item_new_with_label(label);
    gtk_widget_set_name(item, name);
    /* One authoritative state; selection previews and saved preferences update
  * the menu too. Bindings disconnect automatically when either side dies. */
    g_object_bind_property(follow, "active", item, "active", G_BINDING_BIDIRECTIONAL|G_BINDING_SYNC_CREATE|G_BINDING_INVERT_BOOLEAN);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

/* EN: Refresh the relation graph mode.
 * RU: Обновляет режим графа связей. */
static void relation_graph_toggle(GtkToggleButton *button, gpointer data)
{
    (void)button;
    (void)data;
    gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "list");
    relation_view_update();
}

/* EN: Refresh the relation tree and rerun the query when button is enabled.
 * RU: Обновляет дерево связей и повторяет запрос при включении button. */
static void relation_tree_toggle(GtkToggleButton *button, gpointer data)
{
    (void)data;
    gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "list");
    relation_view_update();
    if (!wb.relation_setting && gtk_toggle_button_get_active(button) && *gtk_entry_get_text(GTK_ENTRY(wb.usage_entry))) usage_start(NULL, NULL);
}

/* EN: Refresh the active source region after a control change.
 * RU: Обновляет область исходника после изменения элемента управления. */
static void reference_region_changed(GtkWidget *widget, gpointer data)
{
    (void)data;
    if (wb.reference_setting) return;
    wb.reference_setting = TRUE;
    if (widget == wb.usage_code && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code))) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments), 0);
    else if (widget == wb.usage_comments && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.usage_comments))>0) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_code), FALSE);
    wb.reference_setting = FALSE;
    relation_controls(NULL, NULL);
}

/* EN: Update status and options for the view in object.
 * RU: Обновляет состояние и параметры представления из object. */
static void relation_stack_changed(GObject *object, GParamSpec *spec, gpointer data)
{
    (void)spec;
    (void)data;
    gboolean routes = !g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(object)), "routes");
    if (wb.relation_options)
    {
        gtk_widget_set_no_show_all(wb.relation_options, routes);
        gtk_widget_set_visible(wb.relation_options, !routes);
    }
    gtk_widget_set_no_show_all(wb.usage_status, routes);
    gtk_widget_set_visible(wb.usage_status, !routes);
}
