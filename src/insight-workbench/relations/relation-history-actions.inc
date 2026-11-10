/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: History traversal and menus.
 * RU: Переходы по истории и меню. */


/* EN: Move distance entries in the direction selected by forward.
 * RU: Переходит на distance записей в направлении forward. */
static void relation_travel(gboolean forward, guint distance)
{
    GPtrArray *from = forward ? wb.relation_future : wb.relation_history, *to = forward ? wb.relation_history : wb.relation_future;
    if (!distance || distance>from->len) return;
    relation_visit_push(to, relation_visit_current());
    /* Move intervening visits without issuing intermediate asynchronous queries. */
    while (distance-->1) relation_visit_push(to, relation_visit_pop(from));
    relation_visit_restore(relation_visit_pop(from));
}

/* EN: Go back one entry. widget and data are unused.
 * RU: Переходит на запись назад. widget и data не используются. */
static void relation_back(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    relation_travel(FALSE, 1);
}

/* EN: Go forward one entry. widget and data are unused.
 * RU: Переходит на запись вперёд. widget и data не используются. */
static void relation_forward(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    relation_travel(TRUE, 1);
}

/* EN: Jump to the history entry selected by item and data.
 * RU: Переходит к записи истории по item и data. */
static void relation_history_pick(GtkMenuItem *item, gpointer data)
{
    (void)data;
    relation_travel(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "forward")), GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "distance")));
}

/* EN: Clear relation history. item and data are unused.
 * RU: Очищает историю связей. item и data не используются. */
static void relation_history_clear(GtkMenuItem *item, gpointer data)
{
    (void)item;
    (void)data;
    g_ptr_array_set_size(wb.relation_history, 0);
    g_ptr_array_set_size(wb.relation_future, 0);
    relation_history_state();
}

/* EN: Populate the history menu using its direction in data.
 * RU: Заполняет menu историей в направлении из data. */
static void relation_history_show(GtkWidget *menu, gpointer data)
{
    (void)data;
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *p = children;p;p = p->next) gtk_widget_destroy(p->data);
    g_list_free(children);
    GPtrArray *stacks[] =
    {
        wb.relation_history, wb.relation_future
    };
    const gchar *headings[] =
    {
        "Back", "Forward"
    };
    for (guint side = 0;side<2;side++)
    {
        GtkWidget *heading = gtk_menu_item_new_with_label(headings[side]);
        gtk_widget_set_sensitive(heading, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), heading);
        GPtrArray *stack = stacks[side];
        for (guint i = 0;i<stack->len;i++)
        {
            RelationVisit *visit = g_ptr_array_index(stack, stack->len-1-i);
            GtkTreeModel *modes = gtk_combo_box_get_model(GTK_COMBO_BOX(wb.relation_mode));
            GtkTreeIter mode_iter;
            gchar *method = NULL;
            if (gtk_tree_model_iter_nth_child(modes, &mode_iter, NULL, visit->mode)) gtk_tree_model_get(modes, &mode_iter, 0, &method, -1);
            gchar *short_query = g_utf8_substring(visit->query, 0, MIN((glong)64, g_utf8_strlen(visit->query, -1))), *label = g_strdup_printf("%u. %s — %s", i+1, short_query, method ? method : "");
            g_free(short_query);
            g_free(method);
            GtkWidget *item = gtk_menu_item_new_with_label(label);
            g_free(label);
            gchar *name = g_strdup_printf("insight-relation-history-%s-%u", side ? "forward" : "back", i+1);
            gtk_widget_set_name(item, name);
            g_free(name);
            gtk_widget_set_tooltip_text(item, visit->query);
            g_object_set_data(G_OBJECT(item), "insight-no-i18n", GINT_TO_POINTER(1));
            g_object_set_data(G_OBJECT(item), "forward", GUINT_TO_POINTER(side));
            g_object_set_data(G_OBJECT(item), "distance", GUINT_TO_POINTER(i+1));
            g_signal_connect(item, "activate", G_CALLBACK(relation_history_pick), NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        }
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    GtkWidget *clear = gtk_menu_item_new_with_label("Clear relation history");
    gtk_widget_set_name(clear, "insight-relation-history-clear");
    gtk_widget_set_sensitive(clear, wb.relation_history->len || wb.relation_future->len);
    g_signal_connect(clear, "activate", G_CALLBACK(relation_history_clear), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), clear);
    insight_i18n_apply(menu);
    gtk_widget_show_all(menu);
}
