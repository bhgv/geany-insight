/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Navigate definitions and literal occurrences within immutable compared source snapshots.
 * RU: Навигация по определениям и текстовым вхождениям внутри неизменных снимков сравнения. */
typedef struct { guint side; gint offset; } DiffSymbolLocation;

/* EN: Return an owned identifier at side's caret/selection in m, or NULL; never change selection or search options.
 * RU: Возвращает собственный идентификатор у курсора/выделения стороны side в m либо NULL; не меняет выделение и поиск. */
static gchar *diff_symbol_query(Merge *m, guint side)
{
    if (side > 1 || m->busy || !m->sources[side])
        return NULL;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));
    GtkTextIter begin, end;
    if (!gtk_text_buffer_get_selection_bounds(buffer, &begin, &end))
    {
        gtk_text_buffer_get_iter_at_mark(buffer, &begin, gtk_text_buffer_get_insert(buffer));
        if (!identifier_char(gtk_text_iter_get_char(&begin)) && gtk_text_iter_get_char(&begin) != '$')
        {
            if (!gtk_text_iter_backward_char(&begin))
                return NULL;
        }
        if (!identifier_char(gtk_text_iter_get_char(&begin)) && gtk_text_iter_get_char(&begin) != '$')
            return NULL;
        end = begin;
        guint scanned = 0;
        for (;;)
        {
            GtkTextIter previous = begin;
            if (!gtk_text_iter_backward_char(&previous) ||
                (!identifier_char(gtk_text_iter_get_char(&previous)) && gtk_text_iter_get_char(&previous) != '$'))
                break;
            if (++scanned > 4096)
                return NULL;
            begin = previous;
        }
        while (!gtk_text_iter_is_end(&end) &&
               (identifier_char(gtk_text_iter_get_char(&end)) || gtk_text_iter_get_char(&end) == '$'))
        {
            if (++scanned > 4096)
                return NULL;
            gtk_text_iter_forward_char(&end);
        }
    }
    gint count = gtk_text_iter_get_offset(&end) - gtk_text_iter_get_offset(&begin);
    if (count <= 0 || count > 4096)
        return NULL;
    gchar *query = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
    for (const gchar *at = query; *at; at = g_utf8_next_char(at))
        if (!identifier_char(g_utf8_get_char(at)) && *at != '$')
        {
            g_free(query);
            return NULL;
        }
    return query;
}

/* EN: Match unqualified query against a valid scoped symbol name; no fuzzy or cross-version resolution.
 * RU: Сопоставляет простое query с корректным именем symbol с областью; без нечёткого поиска и смешения версий. */
static gboolean diff_symbol_matches(const InsightDiffSymbol *symbol, const gchar *query)
{
    if (!symbol || !symbol->name || !g_utf8_validate(symbol->name, -1, NULL))
        return FALSE;
    const gchar *leaf = symbol->name;
    for (const gchar *at = symbol->name; *at; at++)
        if (*at == '.' || *at == ':')
            leaf = at + 1;
    return !strcmp(leaf, query);
}

/* EN: Move m's source side to character offset; remember the old source position when remember is TRUE.
 * RU: Перемещает сторону side в m к символьному offset; при remember сохраняет прежнюю позицию исходника. */
static void diff_symbol_jump(Merge *m, guint side, gint offset, gboolean remember)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));
    GtkTextIter at;
    if (remember)
    {
        gtk_text_buffer_get_iter_at_mark(buffer, &at, gtk_text_buffer_get_insert(buffer));
        DiffSymbolLocation location = {side, gtk_text_iter_get_offset(&at)};
        if (!m->symbol_history)
            m->symbol_history = g_array_new(FALSE, FALSE, sizeof(DiffSymbolLocation));
        if (m->symbol_history->len == 128)
            g_array_remove_index(m->symbol_history, 0);
        g_array_append_val(m->symbol_history, location);
    }
    gtk_text_buffer_get_iter_at_offset(buffer, &at, offset);
    gtk_text_buffer_place_cursor(buffer, &at);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]), &at, 0.1, TRUE, 0, 0.3);
    gtk_combo_box_set_active(GTK_COMBO_BOX(m->line_side), side);
    gtk_widget_grab_focus(m->views[side]);
}

/* EN: Activate candidate row in view/path/column by accepting dialog; row offsets are owned by its model.
 * RU: Принимает dialog при активации строки view/path/column; смещениями владеет модель диалога. */
static void diff_symbol_candidate(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer dialog)
{
    (void)view;
    (void)path;
    (void)column;
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
}

/* EN: Resolve query in side of m; jump directly for one candidate, offer a chooser for ambiguity, retain snapshots.
 * RU: Разрешает query стороны side в m; переходит к единственному кандидату либо предлагает выбор, сохраняя снимки. */
static void diff_symbol_definition(Merge *m, guint side, const gchar *query)
{
    GtkListStore *model = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INT);
    guint count = 0;
    gint target = 0;
    gsize length = strlen(m->sources[side]);
    GPtrArray *symbols = m->symbols[side];
    for (guint i = 0; symbols && i < MIN(symbols->len, 4096u); i++)
    {
        InsightDiffSymbol *symbol = symbols->pdata[i];
        if (!diff_symbol_matches(symbol, query) || symbol->begin >= symbol->end || symbol->end > length)
            continue;
        target = g_utf8_pointer_to_offset(m->sources[side], m->sources[side] + symbol->begin);
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_offset(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side])), &at, target);
        GtkTreeIter row;
        gtk_list_store_append(model, &row);
        gtk_list_store_set(model, &row, 0, symbol->name, 1, gtk_text_iter_get_line(&at) + 1, 2, target, -1);
        count++;
    }
    if (count == 1)
        diff_symbol_jump(m, side, target, TRUE);
    else if (!count)
        message(m, insight_tr("No indexed definition in this snapshot."));
    else
    {
        GtkWidget *window = g_object_ref(m->window);
        guint64 serial = m->history_serial;
        GtkWidget *dialog = gtk_dialog_new_with_buttons(insight_tr("Definitions in this snapshot"),
            GTK_WINDOW(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            insight_tr("Cancel"), GTK_RESPONSE_CANCEL, insight_tr("Go to definition"), GTK_RESPONSE_ACCEPT, NULL);
        g_object_ref(dialog);
        gtk_widget_set_name(dialog, "insight-diff-definition-choices");
        gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 300);
        GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, insight_tr("Symbol / scope"),
            gtk_cell_renderer_text_new(), "text", 0, NULL);
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, insight_tr("Line"),
            gtk_cell_renderer_text_new(), "text", 1, NULL);
        GtkWidget *scroller = scroll(tree);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroller, TRUE, TRUE, 4);
        GtkTreeSelection *selection = g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)));
        GtkTreePath *first = gtk_tree_path_new_first();
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), first, NULL, FALSE);
        gtk_tree_path_free(first);
        g_signal_connect(tree, "row-activated", G_CALLBACK(diff_symbol_candidate), dialog);
        gtk_widget_show_all(dialog);
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        m = g_object_get_data(G_OBJECT(window), "insight-diff-state");
        GtkTreeIter row;
        GtkTreeModel *selected_model;
        if (response == GTK_RESPONSE_ACCEPT && m && !m->busy && serial == m->history_serial &&
            gtk_tree_selection_get_selected(selection, &selected_model, &row))
        {
            gtk_tree_model_get(selected_model, &row, 2, &target, -1);
            diff_symbol_jump(m, side, target, TRUE);
        }
        gtk_widget_destroy(dialog);
        g_object_unref(selection);
        g_object_unref(dialog);
        g_object_unref(window);
    }
    g_object_unref(model);
}

/* EN: Execute action (0 definition, 1 literal occurrences, 2 back) on source side of m; never write files or Result.
 * RU: Выполняет action (0 определение, 1 текстовые вхождения, 2 назад) для стороны side в m; не меняет файлы и Result. */
static void diff_symbol_action(Merge *m, guint side, guint action)
{
    if (m->busy || side > 1 || !m->sources[side])
        return;
    if (action == 2)
    {
        if (m->symbol_history && m->symbol_history->len)
        {
            guint index = m->symbol_history->len - 1;
            DiffSymbolLocation location = g_array_index(m->symbol_history, DiffSymbolLocation, index);
            g_array_set_size(m->symbol_history, index);
            diff_symbol_jump(m, location.side, location.offset, FALSE);
        }
        return;
    }
    gchar *query = diff_symbol_query(m, side);
    if (!query)
    {
        message(m, insight_tr("Place the caret on one identifier in a source pane."));
        return;
    }
    if (!action)
        diff_symbol_definition(m, side, query);
    else
    {
        gtk_entry_set_text(GTK_ENTRY(m->find_entry), query);
        gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side), side);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex), FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_case), TRUE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_word), TRUE);
        find_all(m, side);
    }
    g_free(query);
}

/* EN: Dispatch widget's encoded side/action using the still-live comparison owned by window.
 * RU: Выполняет закодированные в widget сторону/команду для живого сравнения во владении window. */
static void diff_symbol_activate(GtkWidget *widget, gpointer window)
{
    Merge *m = g_object_get_data(G_OBJECT(window), "insight-diff-state");
    guint code = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "symbol-action"));
    if (m)
        diff_symbol_action(m, code / 3, code % 3);
}

/* EN: Add source view's definition/occurrence/back actions to menu for m in data; callbacks expire with its window.
 * RU: Добавляет в menu определения/вхождения/возврат для исходника view из m в data; обработчики привязаны к окну. */
static void diff_symbol_menu(GtkTextView *view, GtkWidget *menu, gpointer data)
{
    Merge *m = data;
    guint side = GTK_WIDGET(view) == m->views[1];
    const gchar *labels[] = {"Go to definition — F12", "Text occurrences in snapshot — Shift+F12", "Back — Ctrl+Alt+Left"};
    for (guint action = 0; action < 3; action++)
    {
        GtkWidget *item = gtk_menu_item_new_with_label(insight_tr(labels[action]));
        g_object_set_data(G_OBJECT(item), "symbol-action", GUINT_TO_POINTER(side * 3 + action));
        gtk_widget_set_sensitive(item, !m->busy);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_signal_connect_object(item, "activate", G_CALLBACK(diff_symbol_activate), m->window, 0);
        gtk_widget_show(item);
    }
}

/* EN: Handle F12/Shift+F12 and Ctrl+Alt+Left only in source view for m in data; return whether consumed.
 * RU: Обрабатывает F12/Shift+F12 и Ctrl+Alt+Left только в исходнике view для m из data; возвращает признак обработки. */
static gboolean diff_symbol_key(GtkWidget *view, GdkEventKey *event, gpointer data)
{
    Merge *m = data;
    guint side = view == m->views[1];
    guint modifiers = event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK);
    if (event->keyval == GDK_KEY_F12 && !(modifiers & (GDK_CONTROL_MASK | GDK_MOD1_MASK)))
    {
        diff_symbol_action(m, side, !!(modifiers & GDK_SHIFT_MASK));
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Left && modifiers == (GDK_CONTROL_MASK | GDK_MOD1_MASK))
    {
        diff_symbol_action(m, side, 2);
        return TRUE;
    }
    return FALSE;
}

/* EN: Install source-local controls and shortcuts on public comparison m, leaving AI review unchanged.
 * RU: Добавляет команды и клавиши исходников в файловое сравнение m, сохраняя прежнее поведение AI review. */
static void diff_symbol_tools(Merge *m)
{
    const gchar *labels[] = {"Definition", "Text occurrences…", "Back"};
    for (guint side = 0; side < 2; side++)
    {
        GtkWidget *column = gtk_widget_get_parent(gtk_widget_get_parent(m->headers[side]));
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        gtk_box_pack_start(GTK_BOX(column), row, FALSE, FALSE, 0);
        gtk_box_reorder_child(GTK_BOX(column), row, 1);
        for (guint action = 0; action < 3; action++)
        {
            GtkWidget *control = gtk_button_new_with_label(insight_tr(labels[action]));
            gchar *name = g_strdup_printf("insight-diff-symbol-%u-%u", side, action);
            gtk_widget_set_name(control, name);
            g_free(name);
            gtk_widget_set_tooltip_text(control, insight_tr(
                "Definitions use this version only. Occurrences are case-sensitive whole-word text matches, including comments and strings."));
            g_object_set_data(G_OBJECT(control), "symbol-action", GUINT_TO_POINTER(side * 3 + action));
            g_signal_connect_object(control, "clicked", G_CALLBACK(diff_symbol_activate), m->window, 0);
            gtk_box_pack_start(GTK_BOX(row), control, FALSE, FALSE, 0);
        }
        g_signal_connect(m->views[side], "populate-popup", G_CALLBACK(diff_symbol_menu), m);
        g_signal_connect(m->views[side], "key-press-event", G_CALLBACK(diff_symbol_key), m);
        gtk_widget_show_all(row);
    }
}
