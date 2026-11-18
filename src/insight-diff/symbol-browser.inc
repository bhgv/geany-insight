/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Filter and preview indexed definitions from one immutable File Compare snapshot.
 * RU: Фильтрация и предпросмотр индексированных определений одного снимка File Compare. */
typedef struct {
    GtkWidget *dialog, *tree, *entry, *changed, *status, *preview, *preview_scroll;
    GtkTreeModelFilter *filter;
    gchar *fold;
    guint total;
} DiffSymbolBrowser;

/* EN: Return whether [begin,end) on side of m intersects a changed block; insertions belong only inside the range.
 * RU: Проверяет пересечение [begin,end) стороны side в m с изменением; вставки учитываются только внутри диапазона. */
static gboolean diff_symbol_changed(Merge *m, guint side, gsize begin, gsize end)
{
    for (gsize i = 0; i < m->diff.count; i++)
    {
        InsightDiffHunk *hunk = &m->diff.hunks[i];
        gsize first = side ? hunk->b_begin : hunk->a_begin;
        gsize last = side ? hunk->b_end : hunk->a_end;
        if (first == last ? begin <= first && first < end : first < end && last > begin)
            return TRUE;
    }
    return FALSE;
}

/* EN: Filter model row by browser data's folded name and changed-only switch; no source text is modified.
 * RU: Фильтрует строку model по имени и переключателю изменений browser из data; не меняет исходный текст. */
static gboolean diff_symbol_visible(GtkTreeModel *model, GtkTreeIter *row, gpointer data)
{
    DiffSymbolBrowser *browser = data;
    gchar *name = NULL;
    gboolean changed;
    gtk_tree_model_get(model, row, 0, &name, 4, &changed, -1);
    gchar *fold = g_utf8_casefold(name, -1);
    gboolean visible = (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(browser->changed)) || changed) &&
        (!*browser->fold || strstr(fold, browser->fold));
    g_free(name);
    g_free(fold);
    return visible;
}

/* EN: Preview selection in browser data's independent snapshot buffer and guard the Go action.
 * RU: Показывает selection в отдельном буфере снимка browser из data и управляет доступностью перехода. */
static void diff_symbol_preview(GtkTreeSelection *selection, gpointer data)
{
    DiffSymbolBrowser *browser = data;
    GtkTreeModel *model;
    GtkTreeIter row;
    gboolean valid = gtk_tree_selection_get_selected(selection, &model, &row);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(browser->dialog), GTK_RESPONSE_ACCEPT, valid);
    gtk_widget_set_visible(browser->preview_scroll, valid);
    if (!valid)
        return;
    gint offset;
    gtk_tree_model_get(model, &row, 2, &offset, -1);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(browser->preview));
    GtkTextIter first, last;
    gtk_text_buffer_get_iter_at_offset(buffer, &first, offset);
    last = first;
    gtk_text_iter_forward_to_line_end(&last);
    gtk_text_buffer_select_range(buffer, &first, &last);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(browser->preview), &first, 0.1, TRUE, 0, 0.1);
}

/* EN: Refilter browser data after widget changes, select the first match and refresh the visible/total count.
 * RU: Обновляет фильтр browser из data после изменения widget, выбирает первое совпадение и обновляет счётчик. */
static void diff_symbol_filter(GtkWidget *widget, gpointer data)
{
    (void)widget;
    DiffSymbolBrowser *browser = data;
    g_free(browser->fold);
    browser->fold = g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(browser->entry)), -1);
    gtk_tree_model_filter_refilter(browser->filter);
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(browser->tree));
    guint visible = gtk_tree_model_iter_n_children(model, NULL);
    if (visible)
    {
        GtkTreePath *first = gtk_tree_path_new_first();
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(browser->tree), first, NULL, FALSE);
        gtk_tree_path_free(first);
    }
    gchar *label = g_strdup_printf(insight_tr("%u of %u symbols"), visible, browser->total);
    gtk_label_set_text(GTK_LABEL(browser->status), label);
    g_free(label);
    diff_symbol_preview(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), browser);
}

/* EN: Handle filter focus, Down and Enter in browser data's dialog; widget identifies the emitting dialog.
 * RU: Обрабатывает фокус фильтра, Down и Enter в диалоге browser из data; widget — источник события. */
static gboolean diff_symbol_browser_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    DiffSymbolBrowser *browser = data;
    if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F))
    {
        gtk_widget_grab_focus(browser->entry);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Down && gtk_window_get_focus(GTK_WINDOW(browser->dialog)) == browser->entry)
    {
        gtk_widget_grab_focus(browser->tree);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
    {
        GtkTreeModel *model;
        GtkTreeIter row;
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree)), &model, &row))
            gtk_dialog_response(GTK_DIALOG(browser->dialog), GTK_RESPONSE_ACCEPT);
        return TRUE;
    }
    return FALSE;
}

/* EN: Open a bounded symbol list for source side of m, with name filtering, changed-only view and snapshot preview.
 * Jump only after acceptance while the original comparison is still valid; cancellation changes no source position.
 * RU: Открывает ограниченный список символов стороны side в m с фильтром имён, изменённых определений и предпросмотром.
 * Переходит только после подтверждения в прежнем сравнении; отмена не меняет позицию исходника. */
static void diff_symbol_browser(Merge *m, guint side)
{
    if (m->busy || side > 1 || !m->sources[side])
        return;
    DiffSymbolBrowser browser = {0};
    GtkWidget *window = g_object_ref(m->window);
    guint64 serial = m->history_serial;
    browser.dialog = gtk_dialog_new_with_buttons(insight_tr("Symbols in this snapshot"), GTK_WINDOW(window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, insight_tr("Cancel"), GTK_RESPONSE_CANCEL,
        insight_tr("Go to definition"), GTK_RESPONSE_ACCEPT, NULL);
    g_object_ref(browser.dialog);
    gtk_widget_set_name(browser.dialog, "insight-diff-symbol-browser");
    gtk_window_set_default_size(GTK_WINDOW(browser.dialog), 760, 540);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(browser.dialog));
    GtkWidget *origin = gtk_label_new(side ? insight_tr("Right snapshot") : insight_tr("Left snapshot"));
    gtk_label_set_xalign(GTK_LABEL(origin), 0);
    gtk_box_pack_start(GTK_BOX(box), origin, FALSE, FALSE, 4);
    browser.entry = gtk_search_entry_new();
    gtk_widget_set_name(browser.entry, "insight-diff-symbol-filter");
    gtk_entry_set_placeholder_text(GTK_ENTRY(browser.entry), insight_tr("Filter symbol names"));
    gtk_box_pack_start(GTK_BOX(box), browser.entry, FALSE, FALSE, 4);
    browser.changed = gtk_check_button_new_with_label(insight_tr("Changed definitions only"));
    gtk_widget_set_name(browser.changed, "insight-diff-symbol-changed");
    gtk_box_pack_start(GTK_BOX(box), browser.changed, FALSE, FALSE, 4);
    GtkListStore *store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INT,
        G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_STRING);
    GPtrArray *symbols = m->symbols[side];
    gsize length = strlen(m->sources[side]);
    GtkTextBuffer *snapshot = gtk_text_buffer_new(NULL);
    gtk_text_buffer_set_text(snapshot, m->sources[side], -1);
    if (colorizer)
        colorizer(snapshot, m->loaded_paths[side]);
    for (guint i = 0; symbols && i < MIN(symbols->len, 4096u); i++)
    {
        InsightDiffSymbol *symbol = symbols->pdata[i];
        if (!symbol || !symbol->name || !g_utf8_validate(symbol->name, -1, NULL) ||
            symbol->begin >= symbol->end || symbol->end > length)
            continue;
        gint offset = g_utf8_pointer_to_offset(m->sources[side], m->sources[side] + symbol->begin);
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_offset(snapshot, &at, offset);
        gboolean changed = diff_symbol_changed(m, side, symbol->begin, symbol->end);
        GtkTreeIter row;
        gtk_list_store_append(store, &row);
        gtk_list_store_set(store, &row, 0, symbol->name, 1, gtk_text_iter_get_line(&at) + 1,
            2, offset, 3, (gint)browser.total, 4, changed, 5, insight_tr(changed ? "Changed" : "Unchanged"), -1);
        browser.total++;
    }
    browser.fold = g_strdup("");
    browser.filter = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL));
    g_object_unref(store);
    gtk_tree_model_filter_set_visible_func(browser.filter, diff_symbol_visible, &browser, NULL);
    GtkTreeModel *sorted = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(browser.filter));
    browser.tree = gtk_tree_view_new_with_model(sorted);
    g_object_unref(sorted);
    gtk_widget_set_name(browser.tree, "insight-diff-symbol-list");
    const gchar *titles[] = {"Symbol / scope", "Line", "Change"};
    const gint columns[] = {0, 1, 5};
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
    {
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(insight_tr(titles[i]),
            gtk_cell_renderer_text_new(), "text", columns[i], NULL);
        gtk_tree_view_column_set_sort_column_id(column, columns[i]);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(browser.tree), column);
    }
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(browser.tree), 0);
    gtk_box_pack_start(GTK_BOX(box), scroll(browser.tree), TRUE, TRUE, 4);
    browser.status = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(box), browser.status, FALSE, FALSE, 4);
    browser.preview = gtk_text_view_new_with_buffer(snapshot);
    g_object_unref(snapshot);
    gtk_widget_set_name(browser.preview, "insight-diff-symbol-preview");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(browser.preview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(browser.preview), TRUE);
    browser.preview_scroll = scroll(browser.preview);
    gtk_widget_set_size_request(browser.preview_scroll, -1, 170);
    gtk_box_pack_start(GTK_BOX(box), browser.preview_scroll, TRUE, TRUE, 4);
    GtkTreeSelection *selection = g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(browser.tree)));
    g_signal_connect(selection, "changed", G_CALLBACK(diff_symbol_preview), &browser);
    g_signal_connect(browser.entry, "changed", G_CALLBACK(diff_symbol_filter), &browser);
    g_signal_connect(browser.changed, "toggled", G_CALLBACK(diff_symbol_filter), &browser);
    g_signal_connect(browser.tree, "row-activated", G_CALLBACK(diff_symbol_candidate), browser.dialog);
    g_signal_connect(browser.dialog, "key-press-event", G_CALLBACK(diff_symbol_browser_key), &browser);
    gtk_widget_show_all(browser.dialog);
    diff_symbol_filter(NULL, &browser);
    gtk_widget_grab_focus(browser.entry);
    gint response = gtk_dialog_run(GTK_DIALOG(browser.dialog));
    m = g_object_get_data(G_OBJECT(window), "insight-diff-state");
    GtkTreeModel *model;
    GtkTreeIter row;
    if (response == GTK_RESPONSE_ACCEPT && m && !m->busy && serial == m->history_serial &&
        gtk_tree_selection_get_selected(selection, &model, &row))
    {
        gint offset;
        gtk_tree_model_get(model, &row, 2, &offset, -1);
        diff_symbol_jump(m, side, offset, TRUE);
    }
    g_signal_handlers_disconnect_by_data(selection, &browser);
    g_signal_handlers_disconnect_by_data(browser.entry, &browser);
    g_signal_handlers_disconnect_by_data(browser.changed, &browser);
    g_signal_handlers_disconnect_by_data(browser.dialog, &browser);
    gtk_widget_destroy(browser.dialog);
    g_object_unref(selection);
    g_object_unref(browser.dialog);
    g_object_unref(browser.filter);
    g_free(browser.fold);
    g_object_unref(window);
}
