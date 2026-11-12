/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercise filtered/sorted register selection, clipboard fields and code navigation with real GTK/GDB snapshots.
 * RU: Проверяет выбор фильтрованных/сортированных регистров, копирование полей и переход к коду на настоящих снимках GTK/GDB. */

/* EN: Activate label in window's register copy menu; return FALSE if the production item is missing.
 * RU: Выбирает label в меню копирования регистров window; возвращает FALSE при отсутствии настоящего пункта. */
static gboolean register_tools_copy(GtkWidget *window, const gchar *label)
{
    GtkWidget *button = find(window, "insight-gdb-register-copy");
    GtkWidget *menu = button ? GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(button))) : NULL;
    GtkWidget *item = menu ? find(menu, label) : NULL;
    if (!item || !GTK_IS_MENU_ITEM(item))
        return FALSE;
    gtk_menu_item_activate(GTK_MENU_ITEM(item));
    return TRUE;
}

/* EN: Run phases 101..102 in window/tabs without executing target, then resume existing value tests.
 * RU: Выполняет фазы 101..102 в window/tabs без исполнения программы, затем продолжает существующие проверки значений. */
static gboolean register_tools_gui_step(GtkWidget *window, GtkWidget *tabs)
{
    (void)tabs;
    GtkWidget *entry = find(window, "insight-gdb-instruction-address");
    if (!entry || !gtk_widget_is_sensitive(entry))
        return G_SOURCE_CONTINUE;
    GtkWidget *search = find(window, "insight-gdb-register-search");
    GtkWidget *changed = find(window, "insight-gdb-register-changed");
    GtkWidget *view = find(window, "insight-gdb-registers");
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    static gchar *expected_code;
    if (phase == 101)
    {
        gtk_entry_set_text(GTK_ENTRY(search), "nonexistent-register-for-search");
        g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 0);
        gtk_entry_set_text(GTK_ENTRY(search), "RiP");
        g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 1);
        g_assert_true(native_assembly_gui_select_register(window, "rip"));
        GtkTreeIter row;
        GtkTreeModel *selected = NULL;
        g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), &selected, &row));
        gchar *hex = NULL, *decimal = NULL;
        gtk_tree_model_get(selected, &row, 1, &hex, 2, &decimal, -1);
        expected_code = g_strdup_printf("0x%" G_GINT64_MODIFIER "x", g_ascii_strtoull(hex, NULL, 16));
        const gchar *labels[] = {"Copy register expression", "Copy register hex value", "Copy register decimal value", "Copy register row"};
        for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
        {
            g_assert_true(register_tools_copy(window, labels[i]));
            gchar *text = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
            if (i < 3)
                g_assert_cmpstr(text, ==, i == 0 ? "$rip" : i == 1 ? hex : decimal);
            else
            {
                g_assert_true(g_str_has_prefix(text, "rip = "));
                g_assert_nonnull(strstr(text, hex));
                g_assert_nonnull(strstr(text, decimal));
            }
            g_free(text);
        }
        g_free(decimal);
        g_free(hex);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(changed), TRUE);
        g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(changed), FALSE);
        g_assert_true(native_assembly_gui_select_register(window, "rip"));
        if (click(window, "View register address as code"))
            phase = 102;
    }
    else if (phase == 102 && !g_strcmp0(gtk_entry_get_text(GTK_ENTRY(entry)), "$rip"))
    {
        gchar *address = navigation_gui_address(window);
        g_assert_cmpstr(address, ==, expected_code);
        g_free(address);
        g_clear_pointer(&expected_code, g_free);
        gtk_entry_set_text(GTK_ENTRY(search), "");
        g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), >, 8);
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model), 0, GTK_SORT_DESCENDING);
        GtkTreeIter row;
        gchar *previous = NULL;
        for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
             valid = gtk_tree_model_iter_next(model, &row))
        {
            gchar *name = NULL;
            gtk_tree_model_get(model, &row, 0, &name, -1);
            if (previous)
                g_assert_cmpint(g_strcmp0(previous, name), >=, 0);
            g_free(previous);
            previous = name;
        }
        g_free(previous);
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model), GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
        g_assert_true(has_local(window, "marker", "41"));
        g_print("C/C++ GTK: register search/change filter, sorted selection, clipboard fields and code address passed.\n");
        phase = 16;
    }
    return G_SOURCE_CONTINUE;
}
