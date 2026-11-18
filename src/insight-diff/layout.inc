/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: File Compare layout and explicit, non-destructive switching to merge tools.
 * RU: Компоновка File Compare и явное переключение к сведению без потери результата. */

/* EN: Apply m's mode to containers and controls; never replace Result text or its history.
 * RU: Применяет режим m к контейнерам и кнопкам, не заменяя текст Result и его историю. */
static void diff_mode_apply(Merge *m)
{
    if (!m->file_layout || m->memory)
        return;
    m->browse = !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->mode));
    GtkWidget *pages[] = {
        gtk_widget_get_ancestor(m->take[0], GTK_TYPE_SCROLLED_WINDOW),
        gtk_widget_get_ancestor(m->replace_entry, GTK_TYPE_SCROLLED_WINDOW),
        m->result_panel
    };
    for (guint i = 0; i < G_N_ELEMENTS(pages); i++)
    {
        if (!pages[i])
            continue;
        gtk_widget_set_no_show_all(pages[i], TRUE);
        gtk_widget_set_visible(pages[i], !m->browse);
    }
    gtk_tree_view_column_set_visible(m->merge_column, !m->browse);
    gtk_widget_set_sensitive(m->save_button, !m->busy && !m->browse);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(m->views[2]), !m->browse);
    if (m->browse)
    {
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side)) == 2)
            gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side), 0);
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(m->line_side)) == 2)
            gtk_combo_box_set_active(GTK_COMBO_BOX(m->line_side), 0);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(m->toolbar), 0);
        if (gtk_window_get_focus(GTK_WINDOW(m->window)) == m->views[2])
            gtk_widget_grab_focus(m->views[0]);
    }
    state(m);
    selection(NULL, m);
    alignment_queue(m);
}

/* EN: Apply toggle's chosen mode for m in data; no file is written.
 * RU: Применяет режим toggle для m из data; файлы не записываются. */
static void diff_mode_changed(GtkToggleButton *toggle, gpointer data)
{
    (void)toggle;
    diff_mode_apply(data);
}

/* EN: Reveal Result before a side selector combo requests it; data is the owning comparison.
 * RU: Показывает Result перед выбором его в combo; data — владелец сравнения. */
static void diff_mode_side(GtkComboBox *combo, gpointer data)
{
    Merge *m = data;
    if (m->browse && gtk_combo_box_get_active(combo) == 2)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mode), TRUE);
}

/* EN: Add m's explicit mode control to root; legacy/review construction initially retains merge mode.
 * RU: Добавляет переключатель m в root; исходная компоновка и review первоначально сохраняют режим сведения. */
static void diff_mode_create(Merge *m, GtkWidget *root)
{
    m->mode = gtk_check_button_new_with_label(insight_tr("Merge mode"));
    gtk_widget_set_name(m->mode, "insight-diff-merge-mode");
    gtk_widget_set_tooltip_text(m->mode, insight_tr(
        "Show editable Result and merge tools. Switching modes preserves Result and Undo history."));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mode), TRUE);
    gtk_box_pack_start(GTK_BOX(root), m->mode, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(root), m->mode, 1);
    g_signal_connect(m->mode, "toggled", G_CALLBACK(diff_mode_changed), m);
    g_signal_connect(m->find_side, "changed", G_CALLBACK(diff_mode_side), m);
    g_signal_connect(m->line_side, "changed", G_CALLBACK(diff_mode_side), m);
}

/* EN: Balance source columns once the retained window in data has completed its layout; ignore a destroyed session.
 * RU: Выравнивает ширину исходников после компоновки сохранённого окна data; уничтоженный сеанс пропускает. */
static gboolean diff_mode_balance(gpointer data)
{
    Merge *m = g_object_get_data(G_OBJECT(data), "insight-diff-state");
    if (!m)
        return G_SOURCE_REMOVE;
    GtkWidget *columns = gtk_widget_get_ancestor(m->views[0], GTK_TYPE_PANED);
    if (columns)
        gtk_paned_set_position(GTK_PANED(columns), gtk_widget_get_allocated_width(columns) / 2);
    alignment_queue(m);
    return G_SOURCE_REMOVE;
}

/* EN: Put public file comparison m into the sidebar/two-source layout and read-only comparison mode.
 * Review callers retain their existing layout. No comparison or disk write is started here.
 * RU: Переводит файловое сравнение m к боковому списку, двум исходникам и режиму просмотра.
 * Review сохраняет прежнюю компоновку. Сравнение и запись на диск здесь не запускаются. */
static void file_compare_layout(Merge *m)
{
    m->file_layout = TRUE;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(m->outer_pane), GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_size_request(m->list_scroll, 180, -1);
    gtk_paned_set_position(GTK_PANED(m->outer_pane), 320);
    gtk_tree_view_column_set_visible(m->symbol_column, TRUE);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(m->hunk_view), 5);
    for (guint side = 0; side < 2; side++)
    {
        gtk_tree_view_column_set_expand(m->file_columns[side], FALSE);
        gtk_tree_view_column_set_fixed_width(m->file_columns[side], 65);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mode), FALSE);
    g_idle_add_full(G_PRIORITY_LOW, diff_mode_balance, g_object_ref(m->window), g_object_unref);
}
