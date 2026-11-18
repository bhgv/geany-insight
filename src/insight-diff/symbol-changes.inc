/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Review only the captured differences intersecting one indexed definition, without editing Result.
 * RU: Просмотр только сохранённых различий одного индексированного определения без редактирования Result. */
typedef struct { InsightDiffHunk hunk; guint id; } DiffScopeChange;
typedef struct {
    GtkWidget *dialog, *tree, *views[2], *previous, *next, *copy, *status;
    GtkTextBuffer *buffers[2];
    gchar *sources[2];
    GArray *changes;
} DiffScopeReview;

/* EN: Return whether hunk on side intersects nonempty [begin,end); an empty insertion belongs only inside it.
 * RU: Проверяет пересечение hunk стороны side с непустым [begin,end); пустая вставка учитывается только внутри него. */
static gboolean diff_scope_intersects(const InsightDiffHunk *hunk, guint side, gsize begin, gsize end)
{
    if (begin >= end || side > 1)
        return FALSE;
    gsize first = side ? hunk->b_begin : hunk->a_begin;
    gsize last = side ? hunk->b_end : hunk->a_end;
    return first == last ? begin <= first && first < end : first < end && last > begin;
}

/* EN: Return review's selected local row index, or -1 when its list is empty/unselected.
 * RU: Возвращает локальный индекс выбранной строки review либо -1 при пустом списке/отсутствии выбора. */
static gint diff_scope_selected(DiffScopeReview *review)
{
    GtkTreeIter row;
    GtkTreeModel *model;
    guint index;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(review->tree)), &model, &row))
        return -1;
    gtk_tree_model_get(model, &row, 0, &index, -1);
    return index < review->changes->len ? (gint)index : -1;
}

/* EN: Refresh the two immutable previews and guarded actions after selection changes for review in data.
 * RU: Обновляет два неизменных предпросмотра и доступность команд при изменении selection для review из data. */
static void diff_scope_preview(GtkTreeSelection *selection, gpointer data)
{
    (void)selection;
    DiffScopeReview *review = data;
    gint index = diff_scope_selected(review);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(review->dialog), GTK_RESPONSE_ACCEPT, index >= 0);
    gtk_widget_set_sensitive(review->copy, index >= 0);
    gtk_widget_set_sensitive(review->previous, index > 0);
    gtk_widget_set_sensitive(review->next, index >= 0 && (guint)index + 1 < review->changes->len);
    gchar *label = g_strdup_printf(insight_tr("%u of %u changes in definition"), index < 0 ? 0 : index + 1, review->changes->len);
    gtk_label_set_text(GTK_LABEL(review->status), label);
    g_free(label);
    if (index < 0)
        return;
    DiffScopeChange *change = &g_array_index(review->changes, DiffScopeChange, index);
    for (guint side = 0; side < 2; side++)
    {
        gsize first = side ? change->hunk.b_begin : change->hunk.a_begin;
        gsize last = side ? change->hunk.b_end : change->hunk.a_end;
        GtkTextIter begin, end;
        gtk_text_buffer_get_bounds(review->buffers[side], &begin, &end);
        gtk_text_buffer_remove_tag_by_name(review->buffers[side], "scope-change", &begin, &end);
        gtk_text_buffer_get_iter_at_offset(review->buffers[side], &begin, chars(review->sources[side], first));
        gtk_text_buffer_get_iter_at_offset(review->buffers[side], &end, chars(review->sources[side], last));
        gtk_text_buffer_apply_tag_by_name(review->buffers[side], "scope-change", &begin, &end);
        gtk_text_buffer_select_range(review->buffers[side], &begin, &end);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(review->views[side]), &begin, 0.1, TRUE, 0, 0.4);
    }
}

/* EN: Move review in data one selected row according to button; stop at either endpoint.
 * RU: Перемещает выбранную строку review из data на одну по button; останавливается на концах списка. */
static void diff_scope_step(GtkWidget *button, gpointer data)
{
    DiffScopeReview *review = data;
    gint index = diff_scope_selected(review);
    index += button == review->previous ? -1 : 1;
    if (index < 0 || (guint)index >= review->changes->len)
        return;
    GtkTreePath *path = gtk_tree_path_new_from_indices(index, -1);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(review->tree), path, NULL, FALSE);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(review->tree), path, NULL, FALSE, 0, 0);
    gtk_tree_path_free(path);
}

/* EN: Copy the selected captured old/new block for review in data to the clipboard; button is unused.
 * RU: Копирует выбранный сохранённый старый/новый блок review из data в буфер обмена; button не используется. */
static void diff_scope_copy(GtkWidget *button, gpointer data)
{
    (void)button;
    DiffScopeReview *review = data;
    gint index = diff_scope_selected(review);
    if (index < 0)
        return;
    InsightDiffHunk *hunk = &g_array_index(review->changes, DiffScopeChange, index).hunk;
    gchar *text = g_strdup_printf("%s:\n%.*s\n%s:\n%.*s", insight_tr("Left"),
        (gint)(hunk->a_end - hunk->a_begin), review->sources[0] + hunk->a_begin, insight_tr("Right"),
        (gint)(hunk->b_end - hunk->b_begin), review->sources[1] + hunk->b_begin);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
    g_free(text);
}

/* EN: Handle F3/Shift+F3 and Enter in review data's dialog; widget is unused. Return whether handled.
 * RU: Обрабатывает F3/Shift+F3 и Enter в диалоге review из data; widget не используется. Возвращает признак обработки. */
static gboolean diff_scope_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    DiffScopeReview *review = data;
    if (event->keyval == GDK_KEY_F3)
    {
        diff_scope_step(event->state & GDK_SHIFT_MASK ? review->previous : review->next, review);
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
    {
        if (diff_scope_selected(review) >= 0)
            gtk_dialog_response(GTK_DIALOG(review->dialog), GTK_RESPONSE_ACCEPT);
        return TRUE;
    }
    return FALSE;
}

/* EN: Review captured hunks intersecting definition name/[begin,end) on side of m; acceptance selects the original hunk.
 * The previews own both source snapshots. Stale acceptance and cancellation never change the main selection or Result.
 * RU: Показывает сохранённые блоки определения name/[begin,end) стороны side в m; подтверждение выбирает исходный блок.
 * Предпросмотры владеют снимками обеих сторон. Устаревшее подтверждение и отмена не меняют основной выбор и Result. */
static void diff_scope_show(Merge *m, guint side, gsize begin, gsize end, const gchar *name)
{
    if (m->busy || side > 1 || !m->sources[0] || !m->sources[1] || begin >= end || end > strlen(m->sources[side]))
        return;
    GtkWidget *window = g_object_ref(m->window);
    guint64 serial = m->history_serial;
    DiffScopeReview review = {0};
    review.changes = g_array_new(FALSE, FALSE, sizeof(DiffScopeChange));
    review.dialog = gtk_dialog_new_with_buttons(insight_tr("Changes in definition"), GTK_WINDOW(window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, insight_tr("Close"), GTK_RESPONSE_CANCEL,
        insight_tr("Go to change"), GTK_RESPONSE_ACCEPT, NULL);
    g_object_ref(review.dialog);
    gtk_widget_set_name(review.dialog, "insight-diff-scope-changes");
    gtk_window_set_default_size(GTK_WINDOW(review.dialog), 920, 600);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(review.dialog));
    gchar *title = g_strdup_printf("%s — %s", side ? insight_tr("Right snapshot") : insight_tr("Left snapshot"), name);
    GtkWidget *heading = gtk_label_new(title);
    gtk_label_set_selectable(GTK_LABEL(heading), TRUE);
    gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 4);
    g_free(title);
    GtkListStore *store = gtk_list_store_new(4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);
    for (gsize i = 0; i < m->diff.count; i++)
    {
        if (!diff_scope_intersects(&m->diff.hunks[i], side, begin, end))
            continue;
        DiffScopeChange change = {m->diff.hunks[i], i};
        GtkTreeIter row;
        gtk_list_store_append(store, &row);
        GtkTextIter left, right;
        gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[0])), &left,
            chars(m->sources[0], change.hunk.a_begin));
        gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[1])), &right,
            chars(m->sources[1], change.hunk.b_begin));
        gtk_list_store_set(store, &row, 0, review.changes->len, 1, change.id + 1,
            2, gtk_text_iter_get_line(&left) + 1, 3, gtk_text_iter_get_line(&right) + 1, -1);
        g_array_append_val(review.changes, change);
    }
    review.tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_widget_set_name(review.tree, "insight-diff-scope-list");
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(review.tree), -1, insight_tr("Difference"),
        gtk_cell_renderer_text_new(), "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(review.tree), -1, insight_tr("Left line"),
        gtk_cell_renderer_text_new(), "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(review.tree), -1, insight_tr("Right line"),
        gtk_cell_renderer_text_new(), "text", 3, NULL);
    gtk_box_pack_start(GTK_BOX(box), scroll(review.tree), TRUE, TRUE, 4);
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 4);
    review.previous = gtk_button_new_with_label(insight_tr("Previous change"));
    review.next = gtk_button_new_with_label(insight_tr("Next change"));
    review.copy = gtk_button_new_with_label(insight_tr("Copy change"));
    GtkWidget *controls[] = {review.previous, review.next, review.copy};
    const gchar *control_names[] = {"insight-diff-scope-previous", "insight-diff-scope-next", "insight-diff-scope-copy"};
    for (guint i = 0; i < G_N_ELEMENTS(controls); i++)
    {
        gtk_widget_set_name(controls[i], control_names[i]);
        gtk_box_pack_start(GTK_BOX(bar), controls[i], FALSE, FALSE, 0);
    }
    review.status = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(bar), review.status, FALSE, FALSE, 4);
    GtkWidget *panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(panes), 460);
    gtk_box_pack_start(GTK_BOX(box), panes, TRUE, TRUE, 4);
    for (guint source = 0; source < 2; source++)
    {
        review.sources[source] = g_strdup(m->sources[source]);
        review.buffers[source] = gtk_text_buffer_new(NULL);
        gtk_text_buffer_set_text(review.buffers[source], review.sources[source], -1);
        if (colorizer)
            colorizer(review.buffers[source], m->loaded_paths[source]);
        gtk_text_buffer_create_tag(review.buffers[source], "scope-change", "background",
            source ? "#ddf2dd" : "#ffe0d3", NULL);
        review.views[source] = gtk_text_view_new_with_buffer(review.buffers[source]);
        gtk_widget_set_name(review.views[source], source ? "insight-diff-scope-right" : "insight-diff-scope-left");
        gtk_text_view_set_editable(GTK_TEXT_VIEW(review.views[source]), FALSE);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(review.views[source]), TRUE);
        GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_box_pack_start(GTK_BOX(column), gtk_label_new(source ? insight_tr("Right snapshot") : insight_tr("Left snapshot")), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(column), scroll(review.views[source]), TRUE, TRUE, 0);
        if (source)
            gtk_paned_pack2(GTK_PANED(panes), column, TRUE, FALSE);
        else
            gtk_paned_pack1(GTK_PANED(panes), column, TRUE, FALSE);
    }
    GtkTreeSelection *tree_selection = g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(review.tree)));
    g_signal_connect(tree_selection, "changed", G_CALLBACK(diff_scope_preview), &review);
    g_signal_connect(review.previous, "clicked", G_CALLBACK(diff_scope_step), &review);
    g_signal_connect(review.next, "clicked", G_CALLBACK(diff_scope_step), &review);
    g_signal_connect(review.copy, "clicked", G_CALLBACK(diff_scope_copy), &review);
    g_signal_connect(review.tree, "row-activated", G_CALLBACK(diff_symbol_candidate), review.dialog);
    g_signal_connect(review.dialog, "key-press-event", G_CALLBACK(diff_scope_key), &review);
    GtkWidget *retained[] = {review.tree, review.previous, review.next, review.copy, review.views[0], review.views[1]};
    for (guint i = 0; i < G_N_ELEMENTS(retained); i++)
        g_object_ref(retained[i]);
    gtk_widget_show_all(review.dialog);
    if (review.changes->len)
    {
        GtkTreePath *first = gtk_tree_path_new_first();
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(review.tree), first, NULL, FALSE);
        gtk_tree_path_free(first);
    }
    diff_scope_preview(tree_selection, &review);
    gint response = gtk_dialog_run(GTK_DIALOG(review.dialog));
    m = g_object_get_data(G_OBJECT(window), "insight-diff-state");
    gint index = m ? diff_scope_selected(&review) : -1;
    if (response == GTK_RESPONSE_ACCEPT && index >= 0 && m && !m->busy && serial == m->history_serial)
    {
        DiffScopeChange *change = &g_array_index(review.changes, DiffScopeChange, index);
        if (change->id < m->diff.count)
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));
            GtkTextIter current;
            gtk_text_buffer_get_iter_at_mark(buffer, &current, gtk_text_buffer_get_insert(buffer));
            diff_symbol_jump(m, side, gtk_text_iter_get_offset(&current), TRUE);
            GtkTreePath *path = gtk_tree_path_new_from_indices(change->id, -1);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view), path, NULL, FALSE);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(m->hunk_view), path, NULL, FALSE, 0, 0);
            gtk_tree_path_free(path);
            selection(NULL, m);
            gtk_widget_grab_focus(m->views[side]);
        }
    }
    g_signal_handlers_disconnect_by_data(tree_selection, &review);
    g_signal_handlers_disconnect_by_data(review.previous, &review);
    g_signal_handlers_disconnect_by_data(review.next, &review);
    g_signal_handlers_disconnect_by_data(review.copy, &review);
    g_signal_handlers_disconnect_by_data(review.dialog, &review);
    gtk_widget_destroy(review.dialog);
    g_object_unref(tree_selection);
    g_object_unref(review.dialog);
    for (guint i = 0; i < G_N_ELEMENTS(retained); i++)
        g_object_unref(retained[i]);
    for (guint source = 0; source < 2; source++)
    {
        g_object_unref(review.buffers[source]);
        g_free(review.sources[source]);
    }
    g_array_unref(review.changes);
    g_object_unref(window);
}

/* EN: Open changes for the innermost unambiguous indexed definition at side's caret in m; report unresolved context.
 * RU: Открывает изменения внутреннего однозначного определения у курсора стороны side в m; сообщает об отсутствии контекста. */
static void diff_scope_current(Merge *m, guint side)
{
    if (m->busy || side > 1 || !m->sources[side])
        return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));
    GtkTextIter at, end;
    if (!gtk_text_buffer_get_selection_bounds(buffer, &at, &end))
        gtk_text_buffer_get_iter_at_mark(buffer, &at, gtk_text_buffer_get_insert(buffer));
    gsize offset = g_utf8_offset_to_pointer(m->sources[side], gtk_text_iter_get_offset(&at)) - m->sources[side];
    InsightDiffSymbol *best = NULL;
    gboolean ambiguous = FALSE;
    for (guint i = 0; m->symbols[side] && i < MIN(m->symbols[side]->len, 4096u); i++)
    {
        InsightDiffSymbol *symbol = m->symbols[side]->pdata[i];
        if (!symbol || !symbol->name || !g_utf8_validate(symbol->name, -1, NULL) ||
            symbol->begin > offset || symbol->end <= offset || symbol->end > strlen(m->sources[side]))
            continue;
        gsize span = symbol->end - symbol->begin;
        if (!best || span < best->end - best->begin)
        {
            best = symbol;
            ambiguous = FALSE;
        }
        else if (span == best->end - best->begin && strcmp(symbol->name, best->name))
            ambiguous = TRUE;
    }
    if (best && !ambiguous)
        diff_scope_show(m, side, best->begin, best->end, best->name);
    else
        message(m, insight_tr("Place the caret inside an indexed definition."));
}
