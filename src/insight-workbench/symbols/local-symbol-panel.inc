/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Local symbol filtering, sorting and panel setup.
 * RU: Фильтрация и сортировка локальных символов, настройка панели. */


/* EN: Refresh the structure query from entry.
 * RU: Обновляет запрос структуры из entry. */
static void local_structure_query(GtkEditable *entry, gpointer data)
{
    (void)data;
    if (wb.local_mode && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode)) == 0 && g_strcmp0(gtk_entry_get_text(GTK_ENTRY(entry)), gtk_entry_get_text(GTK_ENTRY(wb.local_entry)))) gtk_entry_set_text(GTK_ENTRY(wb.local_entry), gtk_entry_get_text(GTK_ENTRY(entry)));
}

/* EN: Switch the local browser mode selected in combo.
 * RU: Переключает режим локальной панели по combo. */
static void local_mode_changed(GtkComboBox *combo, gpointer data)
{
    (void)data;
    gboolean details = gtk_combo_box_get_active(combo) == 1;
    gtk_stack_set_visible_child_name(GTK_STACK(wb.local_stack), details ? "details" : "structure");
    local_filter(GTK_EDITABLE(wb.local_entry), NULL);
}

/* EN: Change native source structure sorting.
 * RU: Меняет сортировку структуры исходника. */
static void local_native_sort(GtkButton *button, gpointer data)
{
    (void)button;
    GeanyDocument *doc = document_get_current();
    if (doc) symbols_recreate_tag_list(doc, GPOINTER_TO_INT(data));
}

/* EN: Refresh the local symbol list for the active document.
 * RU: Обновляет локальные символы активного документа. */
static void local_filter(GtkEditable *entry, gpointer data)
{
    (void)data;
    if (!wb.local_symbols) return;
    GeanyDocument *doc = document_get_current();
    if (wb.native_symbol_query && wb.local_mode && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.local_mode)) == 0 && g_strcmp0(gtk_entry_get_text(GTK_ENTRY(wb.local_entry)), gtk_entry_get_text(GTK_ENTRY(wb.native_symbol_query)))) gtk_entry_set_text(GTK_ENTRY(wb.native_symbol_query), gtk_entry_get_text(GTK_ENTRY(wb.local_entry)));
    if (entry || !wb.local_symbol_limit || wb.local_doc_id != (doc ? doc->id : 0))
        wb.local_symbol_limit = 2000;
    wb.local_doc_id = doc ? doc->id : 0;
    gchar *file_label = doc && doc->file_name ? g_path_get_basename(doc->file_name) : g_strdup("Current file symbols");
    gtk_label_set_text(GTK_LABEL(wb.local_label), file_label);
    gtk_widget_set_tooltip_text(wb.local_label, doc ? doc->file_name : NULL);
    g_free(file_label);
    insight_symbol_view_filetype(GTK_TREE_VIEW(wb.local_view), doc ? doc->file_type : NULL);
    SymbolListJob *j = symbol_list_begin(wb.local_symbols, wb.local_entry, wb.local_symbol_limit, FALSE);
    if (!j) return;
    j->local = TRUE;
    j->lang = doc && doc->tm_file ? doc->tm_file->lang : -1;
    j->path = g_strdup(doc ? doc->file_name : NULL);
    j->tags = g_ptr_array_new_with_free_func((GDestroyNotify)tm_tag_unref);
    /* Tags are immutable after publication and their reference count is atomic.
  * Capture references, not GTK or a mutable TMSourceFile, for the worker. */
    if (doc && doc->tm_file && doc->tm_file->tags_array) for (guint n = 0;n<doc->tm_file->tags_array->len;n++)
    {
        TMTag *tag = g_ptr_array_index(doc->tm_file->tags_array, n);
        if (tag)
            g_ptr_array_add(j->tags, tm_tag_ref(tag));
    }
    symbol_list_launch(wb.local_symbols, j);
}
typedef struct
{
    GtkTreeSortable *model;
    GtkWidget *field, *direction;
    gboolean syncing;
    const gint *fields;
    guint count;
}
SymbolSort;
static const gint file_sort_fields[] =
{
    0, 1, 2, 3
};
static const gint symbol_sort_fields[] =
{
    SYMBOL_NAME, SYMBOL_LINE, SYMBOL_KIND, SYMBOL_SCOPE, SYMBOL_PATH
};

/* EN: Synchronize the selector in data with sorting in model.
 * RU: Согласует переключатель из data с сортировкой model. */
static void symbol_sort_sync(GtkTreeSortable *model, gpointer data)
{
    SymbolSort *s = g_object_get_data(G_OBJECT(data), "insight-symbol-sort-state");
    gint field = -1;
    GtkSortType direction;
    if (!s || !gtk_tree_sortable_get_sort_column_id(model, &field, &direction)) return;
    s->syncing = TRUE;
    for (guint i = 0;i<s->count;i++) if (s->fields[i] == field) gtk_combo_box_set_active(GTK_COMBO_BOX(s->field), i);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->direction), direction == GTK_SORT_DESCENDING);
    gtk_button_set_image(GTK_BUTTON(s->direction), gtk_image_new_from_icon_name(direction == GTK_SORT_DESCENDING ? "view-sort-descending" : "view-sort-ascending", GTK_ICON_SIZE_MENU));
    s->syncing = FALSE;
}

/* EN: Apply the selected sort from widget to the model in data.
 * RU: Применяет сортировку из widget к модели из data. */
static void symbol_sort_changed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    SymbolSort *s = data;
    if (s->syncing) return;
    gint mode = gtk_combo_box_get_active(GTK_COMBO_BOX(s->field));
    if (mode >= 0 && mode<(gint)s->count) gtk_tree_sortable_set_sort_column_id(s->model, s->fields[mode], gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->direction)) ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}

/* EN: Build a sort selector for model.
 * RU: Создаёт переключатель сортировки для model. */
static GtkWidget *sort_selector(GtkListStore *model)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    SymbolSort *s = g_new0(SymbolSort, 1);
    s->model = GTK_TREE_SORTABLE(model);
    s->fields = model == wb.file_store ? file_sort_fields : symbol_sort_fields;
    s->count = model == wb.file_store ? G_N_ELEMENTS(file_sort_fields) : G_N_ELEMENTS(symbol_sort_fields);
    s->field = gtk_combo_box_text_new();
    s->direction = gtk_toggle_button_new();
    gtk_widget_set_name(box, model == wb.file_store ? "insight-files-sort" : (model == wb.local_symbols ? "insight-local-sort" : "insight-project-sort"));
    gtk_widget_set_name(s->field, "insight-symbol-sort-field");
    gtk_widget_set_name(s->direction, "insight-symbol-sort-descending");
    const char *labels[] =
    {
        "Sort: name", "Sort: source line", "Sort: symbol type", "Sort: scope", "Sort: file path"
    };
    const gchar *file_labels[] =
    {
        "Sort: file name", "Sort: full path", "Sort: directory", "Sort: size"
    };
    for (guint i = 0;i<s->count;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->field), model == wb.file_store ? file_labels[i] : labels[i]);
    gtk_widget_set_tooltip_text(s->direction, "Descending order");
    gtk_button_set_relief(GTK_BUTTON(s->direction), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(box), s->field, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), s->direction, FALSE, FALSE, 0);
    g_object_set_data_full(G_OBJECT(box), "insight-symbol-sort-state", s, g_free);
    g_signal_connect(s->field, "changed", G_CALLBACK(symbol_sort_changed), s);
    g_signal_connect(s->direction, "toggled", G_CALLBACK(symbol_sort_changed), s);
    g_signal_connect_object(model, "sort-column-changed", G_CALLBACK(symbol_sort_sync), box, 0);
    gtk_combo_box_set_active(GTK_COMBO_BOX(s->field), 0);
    compact_panel_control(s->field);
    return box;
}

/* EN: Save sort settings for model under prefix in settings.
 * RU: Сохраняет сортировку model в settings с префиксом prefix. */
static void symbol_sort_write(GKeyFile *settings, GtkListStore *model, const gchar *prefix)
{
    gint column = -1;
    GtkSortType direction;
    if (!gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(model), &column, &direction)) return;
    gchar *key = g_strconcat(prefix, "-sort-column", NULL);
    g_key_file_set_integer(settings, "workbench", key, column);
    g_free(key);
    key = g_strconcat(prefix, "-sort-descending", NULL);
    g_key_file_set_boolean(settings, "workbench", key, direction == GTK_SORT_DESCENDING);
    g_free(key);
}

/* EN: Restore sort settings for model from settings under prefix.
 * RU: Восстанавливает сортировку model из settings по prefix. */
static void symbol_sort_read(GKeyFile *settings, GtkListStore *model, const gchar *prefix)
{
    gchar *key = g_strconcat(prefix, "-sort-column", NULL);
    GError *error = NULL;
    gint column = g_key_file_get_integer(settings, "workbench", key, &error);
    g_free(key);
    if (error || column<0 || column >= gtk_tree_model_get_n_columns(GTK_TREE_MODEL(model)))
    {
        g_clear_error(&error);
        return;
    }
    key = g_strconcat(prefix, "-sort-descending", NULL);
    gboolean descending = g_key_file_get_boolean(settings, "workbench", key, NULL);
    g_free(key);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model), column, descending ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
}

/* EN: Build the local symbol browser and its controls.
 * RU: Создаёт обозреватель локальных символов и его элементы управления. */
static void create_local_browser(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    wb.local_label = gtk_label_new("Current file symbols");
    gtk_label_set_ellipsize(GTK_LABEL(wb.local_label), PANGO_ELLIPSIZE_MIDDLE);
    PangoAttrList *title_style = pango_attr_list_new();
    pango_attr_list_insert(title_style, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(wb.local_label), title_style);
    pango_attr_list_unref(title_style);
    wb.local_entry = gtk_search_entry_new();
    gtk_widget_set_name(wb.local_entry, "insight-local-query");
    gtk_entry_set_placeholder_text(GTK_ENTRY(wb.local_entry), "Symbol Name…");
    wb.local_symbols = gtk_list_store_new(SYMBOL_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_ULONG, G_TYPE_STRING);
    wb.local_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(wb.local_symbols));
    gtk_widget_set_name(wb.local_view, "insight-local-symbols");
    g_object_set_data(G_OBJECT(wb.local_view), "insight-source-nesting", GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(wb.local_view), "insight-bold-symbol-names", GINT_TO_POINTER(1));
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view)), GTK_SELECTION_BROWSE);
    gint columns[] =
    {
        SYMBOL_NAME, SYMBOL_LINE, SYMBOL_KIND
    };
    const char *labels[] =
    {
        "Symbol", "Line", "Type"
    };
    for (guint i = 0;i<3;i++)
    {
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(labels[i], gtk_cell_renderer_text_new(), "text", columns[i], NULL);
        gtk_tree_view_column_set_sort_column_id(c, columns[i]);
        gtk_tree_view_append_column(GTK_TREE_VIEW(wb.local_view), c);
    }
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), wb.local_view);
    scroll = insight_virtual_list_wrap(scroll, GTK_TREE_VIEW(wb.local_view), wb.local_symbols, virtual_reload, NULL);
    gtk_box_pack_start(GTK_BOX(box), wb.local_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), symbol_query_controls(wb.local_entry, TRUE), FALSE, FALSE, 0);
    GtkWidget *details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_pack_start(GTK_BOX(details), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(details), sort_selector(wb.local_symbols), FALSE, FALSE, 0);
    GtkWidget *group = insight_symbol_view_attach(GTK_TREE_VIEW(wb.local_view), wb.local_symbols, NULL);
    gtk_widget_set_name(group, "insight-local-group-types");
    compact_panel_control(group);
    gtk_box_pack_start(GTK_BOX(details), group, FALSE, FALSE, 0);
    GtkWidget *scopes = insight_symbol_view_scopes_control(GTK_TREE_VIEW(wb.local_view));
    gtk_widget_set_name(scopes, "insight-local-scope-hierarchy");
    compact_panel_control(scopes);
    gtk_box_pack_start(GTK_BOX(details), scopes, FALSE, FALSE, 0);
    GtkWidget *types = insight_symbol_view_types_control(GTK_TREE_VIEW(wb.local_view));
    gtk_widget_set_name(types, "insight-local-type-filters");
    gtk_box_pack_start(GTK_BOX(details), types, FALSE, FALSE, 0);
    insight_symbol_view_more_loader(GTK_TREE_VIEW(wb.local_view), G_CALLBACK(symbols_load_more), 2000);
    GtkWidget *locate = gtk_button_new();
    gtk_widget_set_name(locate, "insight-local-locate");
    gtk_button_set_image(GTK_BUTTON(locate), insight_command_icon("find-location-symbolic"));
    gtk_widget_set_tooltip_text(locate, "Locate symbol at caret");
    g_signal_connect(locate, "clicked", G_CALLBACK(symbols_locate), NULL);
    gtk_box_pack_start(GTK_BOX(types), locate, FALSE, FALSE, 0);
    compact_panel_control(types);
    GtkWidget *info = gtk_button_new_with_label("Symbol Info…");
    compact_panel_control(info);
    g_signal_connect(info, "clicked", G_CALLBACK(browser_symbol_info), wb.local_view);
    gtk_box_pack_start(GTK_BOX(details), info, FALSE, FALSE, 0);
    GtkWidget *home = gtk_notebook_get_nth_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook), 0), *native = ui_lookup_widget(main_widgets.window, "scrolledwindow2");
    wb.native_symbol_query = ui_lookup_widget(main_widgets.window, "entry_tagfilter");
    gtk_widget_set_no_show_all(wb.native_symbol_query, TRUE);
    gtk_widget_hide(wb.native_symbol_query);
    wb.local_mode = gtk_combo_box_text_new();
    gtk_widget_set_name(wb.local_mode, "insight-file-symbol-mode");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.local_mode), "Structure");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.local_mode), "Detailed symbols");
    gtk_combo_box_set_active(GTK_COMBO_BOX(wb.local_mode), 0);
    compact_panel_control(wb.local_mode);
    gtk_box_pack_start(GTK_BOX(box), wb.local_mode, FALSE, FALSE, 0);
    wb.local_stack = gtk_stack_new();
    gtk_widget_set_name(wb.local_stack, "insight-file-symbol-stack");
    gtk_stack_set_homogeneous(GTK_STACK(wb.local_stack), FALSE);
    GtkWidget *structure = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    g_object_ref(native);
    gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(native)), native);
    gtk_box_pack_start(GTK_BOX(structure), native, TRUE, TRUE, 0);
    g_object_unref(native);
    GtkWidget *sortbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    const gchar *sorts[] =
    {
        "Name", "Source order"
    };
    for (guint i = 0;i<2;i++)
    {
        GtkWidget *sort = gtk_button_new_with_label(sorts[i]);
        compact_panel_control(sort);
        gtk_box_pack_start(GTK_BOX(sortbar), sort, TRUE, TRUE, 0);
        g_signal_connect(sort, "clicked", G_CALLBACK(local_native_sort), GINT_TO_POINTER(i ? SYMBOLS_SORT_BY_APPEARANCE : SYMBOLS_SORT_BY_NAME));
    }
    gtk_box_pack_start(GTK_BOX(structure), sortbar, FALSE, FALSE, 0);
    gtk_stack_add_named(GTK_STACK(wb.local_stack), structure, "structure");
    gtk_stack_add_named(GTK_STACK(wb.local_stack), details, "details");
    gtk_box_pack_start(GTK_BOX(box), wb.local_stack, TRUE, TRUE, 0);
    gtk_stack_set_visible_child_name(GTK_STACK(wb.local_stack), "structure");
    gtk_box_pack_start(GTK_BOX(home), box, TRUE, TRUE, 0);
    gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(main_widgets.sidebar_notebook), home, "File Symbols");
    g_signal_connect(wb.local_mode, "changed", G_CALLBACK(local_mode_changed), NULL);
    g_signal_connect(wb.native_symbol_query, "changed", G_CALLBACK(local_structure_query), NULL);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(main_widgets.sidebar_notebook), TRUE);
    g_signal_connect(wb.local_entry, "changed", G_CALLBACK(local_filter), NULL);
    g_signal_connect(wb.local_entry, "activate", G_CALLBACK(browser_entry_activate), NULL);
    g_signal_connect(wb.local_entry, "key-press-event", G_CALLBACK(browser_key), NULL);
    g_signal_connect(wb.local_view, "key-press-event", G_CALLBACK(browser_key), NULL);
    symbol_context_attach(wb.local_view);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.local_view)), "changed", G_CALLBACK(browser_selected), NULL);
    g_signal_connect(wb.local_view, "row-activated", G_CALLBACK(browser_activated), NULL);
    gtk_widget_show_all(box);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(wb.local_symbols), SYMBOL_LINE, GTK_SORT_ASCENDING);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.sidebar_notebook), 0);
    local_filter(NULL, NULL);
}
