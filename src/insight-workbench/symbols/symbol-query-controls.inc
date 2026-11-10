/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Query history and compact panel controls.
 * RU: История запросов и компактные элементы управления. */


/* EN: Return whether query parses in symbol or files mode.
 * RU: Проверяет синтаксис query в режиме символов или files. */
static gboolean symbol_query_valid(const gchar *query, gboolean files)
{
    if (!query || !*query || strlen(query)>256 || !g_utf8_validate(query, -1, NULL))
        return FALSE;
    gchar *trimmed = g_strdup(query);
    g_strstrip(trimmed);
    gboolean empty = !*trimmed;
    g_free(trimmed);
    if (empty)
        return FALSE;
    InsightLookupFilter *filter = insight_lookup_filter_new(query, files, NULL);
    if (!filter)
        return FALSE;
    insight_lookup_filter_free(filter);
    return TRUE;
}

/* EN: Record the current query from entry.
 * RU: Запоминает текущий запрос из entry. */
static void symbol_history_record(GtkWidget *entry)
{
    GPtrArray *history = g_object_get_data(G_OBJECT(entry), "insight-symbol-history");
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(entry));
    if (!history || !symbol_query_valid(query, entry == wb.file_entry)) return;
    insight_search_history_record(history, query);
    if (history->len>16)
        g_ptr_array_set_size(history, 16);
    save_workbench_settings();
}

/* EN: Record the query from the entry in data. item is unused.
 * RU: Запоминает запрос поля из data. item не используется. */
static void symbol_history_remember(GtkMenuItem *item, gpointer data)
{
    (void)item;
    symbol_history_record(GTK_WIDGET(data));
}

/* EN: Restore the query on item into the entry in data.
 * RU: Подставляет запрос из item в поле из data. */
static void symbol_history_pick(GtkMenuItem *item, gpointer data)
{
    GtkWidget *entry = data;
    const gchar *query = g_object_get_data(G_OBJECT(item), "query");
    gtk_entry_set_text(GTK_ENTRY(entry), query);
    gtk_widget_grab_focus(entry);
}

/* EN: Clear history for the entry in data. item is unused.
 * RU: Очищает историю поля из data. item не используется. */
static void symbol_history_clear(GtkMenuItem *item, gpointer data)
{
    (void)item;
    GPtrArray *history = g_object_get_data(G_OBJECT(data), "insight-symbol-history");
    g_ptr_array_set_size(history, 0);
    save_workbench_settings();
}

/* EN: Create a menu item with label and icon.
 * RU: Создаёт пункт меню с label и icon. */
static GtkWidget *symbol_history_item(const gchar *label, const gchar *icon)
{
    GtkWidget *item = gtk_image_menu_item_new_with_label(label);
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), insight_command_icon(icon));
    gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);
    return item;
}

/* EN: Populate menu from the query history attached to data.
 * RU: Заполняет menu историей запросов из data. */
static void symbol_history_show(GtkWidget *menu, gpointer data)
{
    GtkWidget *entry = data;
    GPtrArray *history = g_object_get_data(G_OBJECT(entry), "insight-symbol-history");
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *p = children;p;p = p->next) gtk_widget_destroy(p->data);
    g_list_free(children);
    GtkWidget *remember = symbol_history_item(entry == wb.file_entry ? "Remember current file filter" : "Remember current symbol filter", "document-save-symbolic");
    gtk_widget_set_name(remember, "insight-symbol-history-remember");
    gtk_widget_set_sensitive(remember, symbol_query_valid(gtk_entry_get_text(GTK_ENTRY(entry)), entry == wb.file_entry));
    g_signal_connect_object(remember, "activate", G_CALLBACK(symbol_history_remember), entry, 0);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), remember);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    for (guint i = 0;i<history->len;i++)
    {
        const gchar *query = g_ptr_array_index(history, i);
        gchar *label = g_utf8_substring(query, 0, MIN((glong)64, g_utf8_strlen(query, -1)));
        GtkWidget *item = symbol_history_item(label, "edit-find");
        g_free(label);
        gchar *name = g_strdup_printf("insight-symbol-history-%u", i);
        gtk_widget_set_name(item, name);
        g_free(name);
        g_object_set_data(G_OBJECT(item), "insight-no-i18n", GINT_TO_POINTER(1));
        gtk_widget_set_tooltip_text(item, query);
        g_object_set_data_full(G_OBJECT(item), "query", g_strdup(query), g_free);
        g_signal_connect_object(item, "activate", G_CALLBACK(symbol_history_pick), entry, 0);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    GtkWidget *clear = symbol_history_item(history->len ? "Clear history" : "History is empty", "edit-clear");
    gtk_widget_set_name(clear, "insight-symbol-history-clear");
    gtk_widget_set_sensitive(clear, history->len>0);
    g_signal_connect_object(clear, "activate", G_CALLBACK(symbol_history_clear), entry, 0);
    if (history->len) gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), clear);
    insight_i18n_apply(menu);
    gtk_widget_show_all(menu);
}
/* A narrow sidebar must shrink text, not allocate controls beyond its edge.
 * Keep native button/arrow/icon requisitions and ellipsize only their labels. */

/* EN: Allow widget to shrink inside a panel.
 * RU: Позволяет widget сжиматься внутри панели. */
static void panel_scroll_minimum(GtkWidget *widget)
{
    if (GTK_IS_SCROLLED_WINDOW(widget)) gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(widget), 48);
    if (GTK_IS_CONTAINER(widget))
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList *p = children;p;p = p->next) panel_scroll_minimum(p->data);
        g_list_free(children);
    }
}
static void compact_panel_control(GtkWidget *widget);

/* EN: Compact the label on object. spec and data are unused.
 * RU: Уменьшает подпись object. spec и data не используются. */
static void compact_button_label(GObject *object, GParamSpec *spec, gpointer data)
{
    (void)spec;
    (void)data;
    /* GtkButton may rebuild its label when its translated caption changes. */
    GtkWidget *label = gtk_bin_get_child(GTK_BIN(object));
    if (label) compact_panel_control(label);
}

/* EN: Apply compact sizing to widget.
 * RU: Применяет компактные размеры к widget. */
static void compact_panel_control(GtkWidget *widget)
{
    if (GTK_IS_BUTTON(widget))
    {
        if (!g_object_get_data(G_OBJECT(widget), "insight-compact-label"))
        {
            g_object_set_data(G_OBJECT(widget), "insight-compact-label", GINT_TO_POINTER(1));
            g_signal_connect(widget, "notify::label", G_CALLBACK(compact_button_label), NULL);
        }
        compact_button_label(G_OBJECT(widget), NULL, NULL);
    }
    if (GTK_IS_SCROLLED_WINDOW(widget)) gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(widget), 48);
    if (GTK_IS_ENTRY(widget))
    {
        gtk_style_context_add_class(gtk_widget_get_style_context(widget), "insight-shrink-control");
        gtk_entry_set_width_chars(GTK_ENTRY(widget), 1);
        gtk_entry_set_max_width_chars(GTK_ENTRY(widget), 18);
    }
    if (GTK_IS_COMBO_BOX(widget))
    {
        gtk_style_context_add_class(gtk_widget_get_style_context(widget), "insight-shrink-control");
        GList *cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(widget));
        for (GList *p = cells;p;p = p->next) if (GTK_IS_CELL_RENDERER_TEXT(p->data))
            g_object_set(p->data, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 1, NULL);
        g_list_free(cells);
    }
    if (GTK_IS_LABEL(widget))
    {
        const gchar *text = gtk_label_get_text(GTK_LABEL(widget));
        if (text && *text && !gtk_widget_get_has_tooltip(widget)) gtk_widget_set_tooltip_text(widget, text);
        gtk_label_set_ellipsize(GTK_LABEL(widget), PANGO_ELLIPSIZE_END);
        gtk_label_set_width_chars(GTK_LABEL(widget), 1);
        gtk_label_set_max_width_chars(GTK_LABEL(widget), 24);
    }
    if (GTK_IS_CONTAINER(widget) && !GTK_IS_COMBO_BOX(widget))
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList *p = children;p;p = p->next) compact_panel_control(p->data);
        g_list_free(children);
    }
}

/* EN: Build query controls for entry in local or global mode.
 * RU: Создаёт элементы запроса для entry в локальном или общем режиме. */
static GtkWidget *symbol_query_controls(GtkWidget *entry, gboolean local)
{
    compact_panel_control(entry);
    gtk_widget_set_tooltip_text(entry, "Use ~term for fuzzy search (characters in order); @~path and scope=~owner filter fields");
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3), *button = gtk_menu_button_new(), *menu = gtk_menu_new();
    g_object_set_data_full(G_OBJECT(entry), "insight-symbol-history", g_ptr_array_new_with_free_func(g_free), (GDestroyNotify)g_ptr_array_unref);
    gtk_widget_set_name(button, entry == wb.file_entry ? "insight-files-query-history" : (local ? "insight-local-query-history" : "insight-project-query-history"));
    gtk_button_set_image(GTK_BUTTON(button), insight_command_icon("document-open-recent"));
    gtk_widget_set_tooltip_text(button, entry == wb.file_entry ? "File filter history: 16 valid queries, up to 256 bytes each" : "Symbol filter history: 16 valid queries, up to 256 bytes each");
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(button), menu);
    g_signal_connect_object(menu, "show", G_CALLBACK(symbol_history_show), entry, 0);
    gtk_box_pack_start(GTK_BOX(row), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), button, FALSE, FALSE, 0);
    return row;
}

/* EN: Save query history for entry in settings under prefix.
 * RU: Сохраняет историю entry в settings с префиксом prefix. */
static void symbol_history_write(GKeyFile *settings, GtkWidget *entry, const gchar *prefix)
{
    GPtrArray *history = g_object_get_data(G_OBJECT(entry), "insight-symbol-history");
    gchar *key = g_strconcat(prefix, "-query-history", NULL);
    g_key_file_set_string_list(settings, "workbench", key, (const gchar *const *)history->pdata, history->len);
    g_free(key);
}

/* EN: Restore query history for entry from settings under prefix.
 * RU: Восстанавливает историю entry из settings по prefix. */
static void symbol_history_read(GKeyFile *settings, GtkWidget *entry, const gchar *prefix)
{
    gchar *key = g_strconcat(prefix, "-query-history", NULL);
    gsize count = 0;
    gchar **queries = g_key_file_get_string_list(settings, "workbench", key, &count, NULL);
    g_free(key);
    GPtrArray *history = g_object_get_data(G_OBJECT(entry), "insight-symbol-history");
    if (queries && count <= 16) for (gsize i = count;i>0;i--) if (symbol_query_valid(queries[i-1], entry == wb.file_entry))
        insight_search_history_record(history, queries[i-1]);
    g_strfreev(queries);
}
