/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Project folder panel and disk browser integration.
 * RU: Панель каталогов проекта и связь с обозревателем диска. */


/* EN: Locate the active document. button and data are unused.
 * RU: Находит активный документ. button и data не используются. */
static void folder_locate_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    folder_locate(TRUE);
}

/* EN: Locate the current file when button enables following. data is unused.
 * RU: Находит текущий файл при включении слежения в button. data не используется. */
static void folder_follow_changed(GtkToggleButton *button, gpointer data)
{
    (void)data;
    if (gtk_toggle_button_get_active(button)) folder_locate(FALSE);
}

/* EN: Refresh the folder tree. widget and data are unused.
 * RU: Обновляет дерево каталогов. widget и data не используются. */
static void folder_mapped(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    tree_refresh();
}

/* EN: Open path from the disk browser. activate controls focus.
 * RU: Открывает path из обозревателя диска. activate управляет фокусом. */
static void disk_file_open(const gchar *path, gboolean activate, gpointer data)
{
    (void)data;
    if (!wb.alive) return;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow), FALSE);
    if (show_location(path, 0, 1, TRUE) && activate) open_context(NULL, NULL);
}

/* EN: Locate the current document in the disk browser.
 * RU: Находит текущий документ в обозревателе диска. */
static void disk_current_file(void)
{
    if (!wb.alive || !wb.disk_browser || !wb.folder_mode) return;
    GeanyDocument *doc = document_get_current();
    insight_disk_browser_current_file(wb.disk_browser, doc && doc->is_valid ? doc->file_name : NULL, gtk_combo_box_get_active(GTK_COMBO_BOX(wb.folder_mode)) == 1);
}

/* EN: Switch the folder panel mode selected in combo.
 * RU: Переключает режим панели папок по combo. */
static void folder_mode_changed(GtkComboBox *combo, gpointer data)
{
    (void)data;
    gboolean disk = gtk_combo_box_get_active(combo) == 1;
    gtk_stack_set_visible_child_name(GTK_STACK(wb.folder_stack), disk ? "disk" : "project");
    if (disk)
    {
        gchar *root = wb.root ? utils_get_utf8_from_locale(wb.root) : g_get_current_dir();
        insight_disk_browser_resume(wb.disk_browser, root);
        g_free(root);
    }
    else insight_disk_browser_cancel(wb.disk_browser);
    disk_current_file();
}

/* EN: Build the project folder panel and disk browser.
 * RU: Создаёт панель каталогов проекта и обозреватель диска. */
static void create_project_tree(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3), *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    wb.tree_entry = gtk_search_entry_new();
    gtk_widget_set_name(wb.tree_entry, "insight-folder-query");
    gtk_entry_set_max_length(GTK_ENTRY(wb.tree_entry), 1024);
    gtk_entry_set_placeholder_text(GTK_ENTRY(wb.tree_entry), "Filter project folders/files…");
    gtk_box_pack_start(GTK_BOX(row), wb.tree_entry, TRUE, TRUE, 0);
    GtkWidget *locate = gtk_button_new();
    gtk_widget_set_name(locate, "insight-folder-locate");
    gtk_button_set_image(GTK_BUTTON(locate), insight_command_icon("mark-location-symbolic"));
    gtk_widget_set_tooltip_text(locate, "Locate current file in folders");
    gtk_box_pack_start(GTK_BOX(row), locate, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    wb.tree_follow = gtk_check_button_new_with_label("Follow current file");
    gtk_widget_set_name(wb.tree_follow, "insight-folder-follow");
    gtk_box_pack_start(GTK_BOX(box), wb.tree_follow, FALSE, FALSE, 0);
    wb.tree_status = gtk_label_new("");
    gtk_widget_set_name(wb.tree_status, "insight-folder-count");
    gtk_label_set_xalign(GTK_LABEL(wb.tree_status), 0);
    gtk_box_pack_start(GTK_BOX(box), wb.tree_status, FALSE, FALSE, 0);
    wb.project_tree = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    wb.tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.project_tree));
    gtk_widget_set_name(wb.tree_view, "insight-project-folders");
    gtk_tree_view_append_column(GTK_TREE_VIEW(wb.tree_view), gtk_tree_view_column_new_with_attributes("Project folders", gtk_cell_renderer_text_new(), "text", 0, NULL));
    gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(wb.project_tree), folder_compare, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.project_tree), GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.tree_view), 1);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), wb.tree_view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    GtkWidget *appearance = insight_list_style_button(wb.tree_view);
    gtk_widget_set_name(appearance, "insight-folder-appearance");
    gtk_box_pack_start(GTK_BOX(box), appearance, FALSE, FALSE, 0);
    GtkWidget *host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    wb.folder_mode = gtk_combo_box_text_new();
    gtk_widget_set_name(wb.folder_mode, "insight-folder-mode");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.folder_mode), "Project files");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.folder_mode), "Browse disk");
    gtk_combo_box_set_active(GTK_COMBO_BOX(wb.folder_mode), 0);
    gtk_box_pack_start(GTK_BOX(host), wb.folder_mode, FALSE, FALSE, 0);
    wb.folder_stack = gtk_stack_new();
    wb.disk_browser = insight_disk_browser_new(disk_file_open, NULL);
    gtk_stack_add_named(GTK_STACK(wb.folder_stack), box, "project");
    gtk_stack_add_named(GTK_STACK(wb.folder_stack), wb.disk_browser, "disk");
    gtk_stack_set_visible_child_name(GTK_STACK(wb.folder_stack), "project");
    gtk_box_pack_start(GTK_BOX(host), wb.folder_stack, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook), host, gtk_label_new("Folders"));
    g_signal_connect(wb.folder_mode, "changed", G_CALLBACK(folder_mode_changed), NULL);
    gtk_widget_show_all(host);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.tree_view)), "changed", G_CALLBACK(tree_selected), NULL);
    g_signal_connect(wb.tree_view, "row-activated", G_CALLBACK(tree_activated), NULL);
    g_signal_connect(wb.tree_view, "key-press-event", G_CALLBACK(folder_key), NULL);
    g_signal_connect(wb.tree_entry, "key-press-event", G_CALLBACK(folder_key), NULL);
    g_signal_connect(wb.tree_entry, "changed", G_CALLBACK(folder_filter_changed), NULL);
    g_signal_connect(wb.tree_view, "button-press-event", G_CALLBACK(folder_button), NULL);
    g_signal_connect(wb.tree_view, "popup-menu", G_CALLBACK(folder_popup_key), NULL);
    g_signal_connect(wb.tree_view, "map", G_CALLBACK(folder_mapped), NULL);
    g_signal_connect(locate, "clicked", G_CALLBACK(folder_locate_clicked), NULL);
    g_signal_connect(wb.tree_follow, "toggled", G_CALLBACK(folder_follow_changed), NULL);
    folder_count(0, 0);
    gtk_widget_show_all(box);
}
/* Shared interaction/state for the two grouped project browsers. */
