/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Grouped panel keyboard and shared controls.
 * RU: Клавиатурная навигация и общие кнопки сгруппированных панелей. */


/* EN: Find the first leaf in model using row and iter for the search in data.
 * RU: Находит первый лист в model по row и iter для поиска из data. */
static gboolean grouped_first(GtkTreeModel *model, GtkTreePath *row, GtkTreeIter *iter, gpointer data)
{
    GroupedVisit *v = data;
    gchar *name = NULL, *path = NULL;
    gulong line = 0;
    gboolean leaf = grouped_row(model, iter, v->panel, &name, &path, &line);
    g_free(name);
    g_free(path);
    if (leaf) v->found = gtk_tree_path_copy(row);
    return leaf;
}

/* EN: Handle event in widget for the grouped panel in data.
 * RU: Обрабатывает event в widget для сгруппированной панели из data. */
static gboolean grouped_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    GroupedPanel *p = data;
    guint key = gdk_keyval_to_lower(event->keyval), mods = event->state&gtk_accelerator_get_default_mod_mask();
    if (widget == p->entry)
    {
        if (key == GDK_KEY_Escape && !mods)
        {
            if (*gtk_entry_get_text(GTK_ENTRY(widget))) gtk_entry_set_text(GTK_ENTRY(widget), "");
            else editor_focus();
            return TRUE;
        }
        if ((key == GDK_KEY_Down || key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) && !mods)
        {
            grouped_flush(p);
            if (gtk_style_context_has_class(gtk_widget_get_style_context(p->entry), "error"))
                return TRUE;
            GroupedVisit v =
            {
                .panel = p
            };
            gtk_tree_model_foreach(GTK_TREE_MODEL(p->store), grouped_first, &v);
            if (v.found)
            {
                gtk_tree_view_expand_to_path(GTK_TREE_VIEW(p->view), v.found);
                gtk_tree_view_set_cursor(GTK_TREE_VIEW(p->view), v.found, NULL, FALSE);
                gtk_tree_path_free(v.found);
                gtk_widget_grab_focus(p->view);
                if (key != GDK_KEY_Down) grouped_action(p, GP_OPEN);
            }
            return TRUE;
        }
        return FALSE;
    }
    if ((key == GDK_KEY_Menu && !mods) || (key == GDK_KEY_F10 && mods == GDK_SHIFT_MASK))
        return grouped_popup(widget, p);
    if (key == GDK_KEY_f && mods == GEANY_PRIMARY_MOD_MASK)
    {
        gtk_widget_grab_focus(p->entry);
        return TRUE;
    }
    if (key == GDK_KEY_c && (mods == GEANY_PRIMARY_MOD_MASK || mods == (GEANY_PRIMARY_MOD_MASK|GDK_SHIFT_MASK)))
    {
        grouped_action(p, mods&GDK_SHIFT_MASK ? GP_LOCATION : GP_NAME);
        return TRUE;
    }
    if ((key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) && mods == GEANY_PRIMARY_MOD_MASK)
    {
        grouped_action(p, GP_OPEN);
        return TRUE;
    }
    if ((key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) && mods == GDK_MOD1_MASK && p->symbols)
    {
        grouped_action(p, GP_INFO);
        return TRUE;
    }
    if (key == GDK_KEY_Escape && !mods)
    {
        editor_focus();
        return TRUE;
    }
    return FALSE;
}

/* EN: Refresh the panel in data when mapped. widget is unused.
 * RU: Обновляет панель из data при показе. widget не используется. */
static void grouped_mapped(GtkWidget *widget, gpointer data)
{
    (void)widget;
    grouped_refresh(data);
}

/* EN: Request more rows for the panel in data. button is unused.
 * RU: Запрашивает следующие строки панели из data. button не используется. */
static void grouped_more(GtkButton *button, gpointer data)
{
    (void)button;
    grouped_action(data, GP_MORE);
}

/* EN: Add shared controls for p to box and connect their handlers.
 * RU: Добавляет общие кнопки p в box и подключает обработчики. */
static void grouped_attach(GroupedPanel *p, GtkWidget *box)
{
    gtk_entry_set_max_length(GTK_ENTRY(p->entry), 1024);
    gtk_widget_set_name(p->entry, p->symbols ? "insight-category-query" : "insight-type-query");
    gtk_widget_set_name(p->view, p->symbols ? "insight-symbol-categories" : "insight-file-types");
    g_object_set_data(G_OBJECT(p->view), "insight-grouped-panel", p);
    g_object_set_data(G_OBJECT(p->entry), "insight-grouped-panel", p);
    p->limit = p->symbols ? 5000 : 10000;
    p->status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(p->status), 0);
    gtk_widget_set_name(p->status, p->symbols ? "insight-category-count" : "insight-type-count");
    gtk_box_pack_start(GTK_BOX(box), p->status, FALSE, FALSE, 0);
    grouped_count(p, 0, 0);
    if (p->symbols)
    {
        p->more = gtk_button_new_with_label("Load more category symbols");
        gtk_widget_set_name(p->more, "insight-category-more");
        gtk_widget_set_no_show_all(p->more, TRUE);
        gtk_box_pack_start(GTK_BOX(box), p->more, FALSE, FALSE, 0);
        g_signal_connect(p->more, "clicked", G_CALLBACK(grouped_more), p);
    }
    g_signal_connect(p->entry, "changed", G_CALLBACK(grouped_changed), p);
    g_signal_connect(p->entry, "key-press-event", G_CALLBACK(grouped_key_press), p);
    g_signal_connect(p->view, "key-press-event", G_CALLBACK(grouped_key_press), p);
    g_signal_connect(p->view, "popup-menu", G_CALLBACK(grouped_popup), p);
    g_signal_connect(p->view, "button-press-event", G_CALLBACK(grouped_button), p);
    g_signal_connect(p->view, "map", G_CALLBACK(grouped_mapped), p);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)), "changed", G_CALLBACK(grouped_selected), p);
}
