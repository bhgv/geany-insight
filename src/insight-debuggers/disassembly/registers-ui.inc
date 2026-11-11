/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Show target-defined general-purpose register snapshots with names, changes and inspection actions.
 * RU: Показывает снимки регистров общего назначения цели с именами, изменениями и действиями исследования. */

/* EN: Rebuild session's register rows in GDB definition order; compare only snapshots from the same selected thread/frame.
 * RU: Перестраивает регистры session в порядке GDB; сравнивает только снимки одного выбранного потока/кадра. */
static void native_assembly_register_rows(NativeDebug *session)
{
    NativeAssembly *assembly = session->assembly;
    if (!assembly->register_rows || !assembly->previous_registers || !assembly->register_names)
        return;
    gtk_list_store_clear(assembly->register_rows);
    gboolean compare = !g_strcmp0(assembly->register_thread, session->selected_thread) &&
        !g_strcmp0(assembly->register_frame, session->selected_frame);
    for (guint i = 0; i < assembly->register_names->len; i++)
    {
        const gchar *name = assembly->register_names->pdata[i];
        guint64 *value = g_hash_table_lookup(assembly->registers, name);
        if (!value || !native_name_register(name))
            continue;
        guint64 *previous = compare ? g_hash_table_lookup(assembly->previous_registers, name) : NULL;
        gboolean changed = previous && *previous != *value;
        gchar *hex = g_strdup_printf("0x%016" G_GINT64_MODIFIER "x", *value);
        gchar *decimal = g_strdup_printf("%" G_GUINT64_FORMAT, *value);
        const NativeNamedRange *range = native_name_lookup(assembly, *value, assembly->function, FALSE);
        gchar *label = range ? native_name_label(range, *value) : g_strdup("");
        gtk_list_store_insert_with_values(assembly->register_rows, NULL, -1,
            0, name, 1, hex, 2, decimal, 3, label,
            4, changed ? insight_tr("Changed") : "", 5, changed ? "#b35a00" : NULL, -1);
        g_free(label);
        g_free(decimal);
        g_free(hex);
    }
    g_hash_table_remove_all(assembly->previous_registers);
    GHashTableIter iterator;
    gpointer name, value;
    g_hash_table_iter_init(&iterator, assembly->registers);
    while (g_hash_table_iter_next(&iterator, &name, &value))
    {
        guint64 *copy = g_new(guint64, 1);
        *copy = *(guint64 *)value;
        g_hash_table_insert(assembly->previous_registers, g_strdup(name), copy);
    }
    g_free(assembly->register_thread);
    g_free(assembly->register_frame);
    assembly->register_thread = g_strdup(session->selected_thread);
    assembly->register_frame = g_strdup(session->selected_frame);
}

/* EN: Inspect the selected register of session data using button's memory flag; raw register names stay expressions.
 * RU: Исследует выбранный регистр session data по флагу памяти button; исходные имена регистров остаются выражениями. */
static void native_assembly_register_action(GtkButton *button, gpointer data)
{
    NativeDebug *session = data;
    if (!session->stopped || session->pending || session->writing || session->assembly->active)
        return;
    GtkTreeModel *model = NULL;
    GtkTreeIter row;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(session->assembly->register_view));
    if (!gtk_tree_selection_get_selected(selection, &model, &row))
        return;
    gchar *name = NULL;
    gtk_tree_model_get(model, &row, 0, &name, -1);
    if (native_name_register(name))
    {
        gchar *expression = g_strconcat("$", name, NULL);
        native_data_use_expression(session, expression, g_object_get_data(G_OBJECT(button), "register-memory") ?
            NATIVE_DATA_MEMORY : NATIVE_DATA_WATCH);
        g_free(expression);
    }
    g_free(name);
}

/* EN: Create session's bounded register snapshot table and paused-only watch/memory buttons.
 * RU: Создаёт ограниченную таблицу регистров session и доступные на паузе кнопки наблюдения/памяти. */
static GtkWidget *native_assembly_register_widget(NativeDebug *session)
{
    NativeAssembly *assembly = session->assembly;
    assembly->previous_registers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *hint = gtk_label_new(insight_tr("Registers from the last instruction read. Changed values are highlighted within the same thread/frame. Click Register to sort by name."));
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);
    assembly->register_rows = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(assembly->register_rows));
    assembly->register_view = view;
    gtk_widget_set_name(view, "insight-gdb-registers");
    g_object_unref(assembly->register_rows);
    const gchar *titles[] = {"Register", "Hex", "Unsigned value", "Resolved name / field", "Changed"};
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
    {
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(insight_tr(titles[i]),
            gtk_cell_renderer_text_new(), "text", i, "foreground", 5, NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        if (!i)
            gtk_tree_view_column_set_sort_column_id(column, 0);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
    }
    g_ptr_array_add(session->paused_controls, view);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, -1, 130);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    GtkWidget *bar = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(bar), GTK_SELECTION_NONE);
    const gchar *labels[] = {"Watch selected register", "View register address in memory"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        g_object_set_data(G_OBJECT(button), "register-memory", GUINT_TO_POINTER(i));
        g_signal_connect(button, "clicked", G_CALLBACK(native_assembly_register_action), session);
        g_ptr_array_add(session->paused_controls, button);
        gtk_flow_box_insert(GTK_FLOW_BOX(bar), button, -1);
    }
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    return box;
}
