/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Watch graph layout, drawing and interaction.
 * RU: Компоновка, отрисовка и управление графом наблюдения. */

/* Resolve a pinned member path against the current graph. */
/* Находит закреплённый член по его пути в текущем графе. */
static gint debug_watch_resolve(const InsightLuaWatchGraph *graph, const gchar *path)
{
    guint node = 0;
    while (*path == '\x1f')
    {
        path++;
        const gchar *end = strchr(path, '\x1f');
        gsize length = end ? (gsize)(end - path) : strlen(path);
        gint next = -1;
        for (guint i = 0; i < graph->edge_count; i++)
        {
            const InsightLuaWatchEdge *edge = &graph->edges[i];
            if (edge->from != node || strlen(edge->label) != length ||
                strncmp(edge->label, path, length))
                continue;
            if (next >= 0 && (guint)next != edge->to)
                return -1;
            next = (gint)edge->to;
        }
        if (next < 0)
            return -1;
        node = (guint)next;
        path += length;
    }
    return *path ? -1 : (gint)node;
}

/* Find a saved watch path by its exact edge sequence. */
/* Ищет сохранённый путь по точной последовательности подписей стрелок. */
static gint debug_watch_pin_index(const ScriptDebugWindow *view, const gchar *path)
{
    for (guint i = 0; i < view->watch_paths->len; i++)
        if (!strcmp(view->watch_paths->pdata[i], path))
            return (gint)i;
    return -1;
}

/* Sort link descriptions for stable comparisons across stops. */
/* Сортирует описания связей для стабильного сравнения между остановками. */
static gint debug_watch_compare_link(gconstpointer left, gconstpointer right)
{
    const gchar *const *left_text = left;
    const gchar *const *right_text = right;
    return strcmp(*left_text, *right_text);
}

/* Compare values and direct links with the previous source stop. */
/* Сравнивает значения и прямые связи с предыдущей остановкой на строке. */
static void debug_watch_compare(ScriptDebugWindow *view)
{
    memset(view->watch_changed, 0, sizeof view->watch_changed);
    view->watch_change_count = 0;
    const InsightLuaWatchGraph *graph = view->watch_graph;
    if (!graph)
        return;

    GHashTable *current = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (guint i = 1; i < graph->node_count; i++)
    {
        const InsightLuaWatchNode *node = &graph->nodes[i];
        if (!*node->path)
            continue;

        /* Order-independent direct links keep Lua table iteration from causing false changes.
         * Порядок обхода таблицы Lua не должен считаться изменением её связей. */
        GPtrArray *links = g_ptr_array_new_with_free_func(g_free);
        for (guint j = 0; j < graph->edge_count; j++)
        {
            const InsightLuaWatchEdge *edge = &graph->edges[j];
            if (edge->from == i)
                g_ptr_array_add(links, g_strdup_printf("%s\x1e%s\x1e%s", edge->label,
                    graph->nodes[edge->to].path, graph->nodes[edge->to].value));
        }
        g_ptr_array_sort(links, debug_watch_compare_link);
        GString *signature = g_string_new(node->value);
        for (guint j = 0; j < links->len; j++)
        {
            g_string_append_c(signature, '\x1d');
            g_string_append(signature, links->pdata[j]);
        }
        g_ptr_array_unref(links);

        if (view->watch_previous_values)
        {
            const gchar *before = g_hash_table_lookup(view->watch_previous_values, node->path);
            view->watch_changed[i] = g_strcmp0(before, signature->str) != 0;
            if (view->watch_changed[i])
                view->watch_change_count++;
        }
        g_hash_table_insert(current, g_strdup(node->path), g_string_free(signature, FALSE));
    }
    g_clear_pointer(&view->watch_previous_values, g_hash_table_unref);
    view->watch_previous_values = current;
}

/* Lay out reachable nodes by distance from the displayed roots. */
/* Раскладывает достижимые узлы по расстоянию от отображаемых корней. */
static void debug_watch_layout(ScriptDebugWindow *view)
{
    const InsightLuaWatchGraph *graph = view->watch_graph;
    guint queue[INSIGHT_LUA_WATCH_NODES];
    guint rows[INSIGHT_LUA_WATCH_NODES] = {0};
    guint queued = 0, columns = 0, height = 0, missing = 0;

    for (guint i = 0; i < INSIGHT_LUA_WATCH_NODES; i++)
        view->watch_column[i] = view->watch_row[i] = -1;

    if (graph)
    {
        gboolean pinned_only = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->watch_only));
        if (!pinned_only)
        {
            view->watch_column[0] = 0;
            queue[queued++] = 0;
        }
        for (guint i = 0; i < view->watch_paths->len; i++)
        {
            gint node = debug_watch_resolve(graph, view->watch_paths->pdata[i]);
            if (node < 0)
                missing++;
            else if (pinned_only && view->watch_column[node] < 0)
            {
                view->watch_column[node] = 0;
                queue[queued++] = (guint)node;
            }
        }
        for (guint i = 0; i < queued; i++)
        {
            guint node = queue[i];
            guint column = (guint)view->watch_column[node];
            view->watch_row[node] = (gint)rows[column]++;
            columns = MAX(columns, column + 1);
            height = MAX(height, rows[column]);
            for (guint j = 0; j < graph->edge_count; j++)
            {
                const InsightLuaWatchEdge *edge = &graph->edges[j];
                if (edge->from == node && view->watch_column[edge->to] < 0)
                {
                    view->watch_column[edge->to] = (gint)column + 1;
                    queue[queued++] = edge->to;
                }
            }
        }
    }
    if (!graph || view->watch_selected < 0 ||
        view->watch_column[view->watch_selected] < 0)
        view->watch_selected = -1;
    const gchar *path = view->watch_selected > 0 ? graph->nodes[view->watch_selected].path : NULL;
    gtk_widget_set_sensitive(view->watch_pin, path && *path);
    gtk_button_set_label(GTK_BUTTON(view->watch_pin),
        path && debug_watch_pin_index(view, path) >= 0 ? "Unpin member" : "Pin member");
    gchar *status = graph ? g_strdup_printf("Pinned: %u   Unavailable: %u   Changed: %u%s",
        view->watch_paths->len, missing, view->watch_change_count, graph->limited ? "   Snapshot limit reached" : "") :
        g_strdup("Pause on a Lua line to inspect values");
    gtk_label_set_text(GTK_LABEL(view->watch_status), status);
    g_free(status);
    gtk_widget_set_tooltip_text(view->watch_canvas, NULL);
    if (graph && view->watch_selected >= 0)
    {
        guint selected = (guint)view->watch_selected;
        GString *links = g_string_new(graph->nodes[selected].label);
        for (guint i = 0; i < graph->edge_count && links->len < 2048; i++)
        {
            const InsightLuaWatchEdge *edge = &graph->edges[i];
            if (edge->from == selected)
                g_string_append_printf(links, "\n→ %s: %s", edge->label, graph->nodes[edge->to].value);
            if (edge->to == selected)
                g_string_append_printf(links, "\n← %s: %s", graph->nodes[edge->from].label, edge->label);
        }
        gchar *valid = g_utf8_make_valid(links->str, -1);
        gtk_widget_set_tooltip_text(view->watch_canvas, valid);
        g_free(valid);
        g_string_free(links, TRUE);
    }
    gtk_widget_set_size_request(view->watch_canvas,
        WATCH_MARGIN * 2 + (gint)columns * WATCH_COLUMN_WIDTH,
        WATCH_MARGIN * 2 + (gint)height * WATCH_ROW_HEIGHT);
    gtk_widget_queue_draw(view->watch_canvas);
}

/* Draw a single UTF-8 label without allowing it to overflow its card. */
/* Рисует подпись UTF-8, ограничивая её шириной карточки. */
static void debug_watch_label(PangoLayout *layout, cairo_t *cr, const gchar *text,
                              gint width, double x, double y)
{
    gchar *valid = g_utf8_make_valid(text, -1);
    pango_layout_set_text(layout, valid, -1);
    g_free(valid);
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
}

/* Paint cards and directed links, routing aliases back to their shared node. */
/* Рисует карточки и стрелки, направляя общие ссылки к одному узлу. */
static gboolean debug_watch_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    ScriptDebugWindow *view = data;
    const InsightLuaWatchGraph *graph = view->watch_graph;
    if (!graph)
        return FALSE;

    PangoLayout *layout = gtk_widget_create_pango_layout(widget, NULL);
    for (guint i = 0; i < graph->edge_count; i++)
    {
        const InsightLuaWatchEdge *edge = &graph->edges[i];
        gint from_column = view->watch_column[edge->from], to_column = view->watch_column[edge->to];
        if (from_column < 0 || to_column < 0)
            continue;
        double source_x = WATCH_MARGIN + from_column * WATCH_COLUMN_WIDTH + WATCH_CARD_WIDTH;
        double target_x = WATCH_MARGIN + to_column * WATCH_COLUMN_WIDTH;
        double source_y = WATCH_MARGIN + view->watch_row[edge->from] * WATCH_ROW_HEIGHT + WATCH_CARD_HEIGHT / 2;
        double target_y = WATCH_MARGIN + view->watch_row[edge->to] * WATCH_ROW_HEIGHT + WATCH_CARD_HEIGHT / 2;
        gboolean forward = to_column > from_column;
        double bend = forward ? (target_x - source_x) / 2 : 34;
        if (!forward)
        {
            target_x += WATCH_CARD_WIDTH;
            target_y -= 10;
        }
        cairo_set_source_rgb(cr, .34, .48, .63);
        cairo_move_to(cr, source_x, source_y);
        cairo_curve_to(cr, source_x + bend, source_y,
                      target_x + (forward ? -bend : bend), target_y, target_x, target_y);
        cairo_stroke(cr);
        double arrow_x = target_x + (forward ? -7 : 7);
        cairo_move_to(cr, target_x, target_y);
        cairo_line_to(cr, arrow_x, target_y - 4);
        cairo_line_to(cr, arrow_x, target_y + 4);
        cairo_close_path(cr);
        cairo_fill(cr);
        if (forward)
            debug_watch_label(layout, cr, edge->label, WATCH_COLUMN_WIDTH - WATCH_CARD_WIDTH - 6,
                              target_x - (WATCH_COLUMN_WIDTH - WATCH_CARD_WIDTH), target_y - 18);
    }
    for (guint i = 0; i < graph->node_count; i++)
    {
        if (view->watch_column[i] < 0)
            continue;
        const InsightLuaWatchNode *node = &graph->nodes[i];
        double x = WATCH_MARGIN + view->watch_column[i] * WATCH_COLUMN_WIDTH;
        double y = WATCH_MARGIN + view->watch_row[i] * WATCH_ROW_HEIGHT;
        gboolean selected = (gint)i == view->watch_selected;
        gboolean pinned = *node->path && debug_watch_pin_index(view, node->path) >= 0;
        if (view->watch_changed[i])
            cairo_set_source_rgb(cr, 1., selected ? .78 : .88, .62);
        else
            cairo_set_source_rgb(cr, selected ? .79 : .92, pinned ? .85 : .94, .97);
        cairo_rectangle(cr, x, y, WATCH_CARD_WIDTH, WATCH_CARD_HEIGHT);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, .29, .43, .60);
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, .10, .18, .28);
        debug_watch_label(layout, cr, node->label, WATCH_CARD_WIDTH - 12, x + 6, y + 5);
        debug_watch_label(layout, cr, node->value, WATCH_CARD_WIDTH - 12, x + 6, y + 27);
    }
    g_object_unref(layout);
    return FALSE;
}

/* Toggle a selected member as a persistent root for later source stops. */
/* Закрепляет выбранный член как корень для следующих остановок. */
static void debug_watch_pin(GtkButton *button, gpointer data)
{
    (void)button;
    ScriptDebugWindow *view = data;
    if (!view->watch_graph || view->watch_selected <= 0)
        return;
    const gchar *path = view->watch_graph->nodes[view->watch_selected].path;
    if (!*path)
        return;

    gint index = debug_watch_pin_index(view, path);
    if (index >= 0)
        g_ptr_array_remove_index(view->watch_paths, (guint)index);
    else if (view->watch_paths->len < 32)
        g_ptr_array_add(view->watch_paths, g_strdup(path));
    debug_watch_layout(view);
}

/* Select a card, or pin it on a double-click. */
/* Выбирает карточку или закрепляет её двойным щелчком. */
static gboolean debug_watch_click(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget;
    ScriptDebugWindow *view = data;
    if (!view->watch_graph || event->button != 1)
        return FALSE;

    for (guint i = 0; i < view->watch_graph->node_count; i++)
    {
        if (view->watch_column[i] < 0)
            continue;
        gint x = WATCH_MARGIN + view->watch_column[i] * WATCH_COLUMN_WIDTH;
        gint y = WATCH_MARGIN + view->watch_row[i] * WATCH_ROW_HEIGHT;
        if (event->x < x || event->x > x + WATCH_CARD_WIDTH ||
            event->y < y || event->y > y + WATCH_CARD_HEIGHT)
            continue;
        view->watch_selected = (gint)i;
        if (event->type == GDK_2BUTTON_PRESS)
            debug_watch_pin(NULL, view);
        else
            debug_watch_layout(view);
        return TRUE;
    }
    return FALSE;
}

/* Rebuild the displayed subgraph when its root filter changes. */
/* Перестраивает видимый подграф при переключении фильтра корней. */
static void debug_watch_filter(GtkToggleButton *button, gpointer data)
{
    (void)button;
    debug_watch_layout(data);
}

/* Forget all pinned paths, including those outside the current scope. */
/* Удаляет все закреплённые пути, включая недоступные в текущей области. */
static void debug_watch_clear(GtkButton *button, gpointer data)
{
    (void)button;
    ScriptDebugWindow *view = data;
    g_ptr_array_set_size(view->watch_paths, 0);
    debug_watch_layout(view);
}

/* Build the scrollable watch graph tab and its controls. */
/* Создаёт вкладку графа наблюдения с прокруткой и кнопками. */
static GtkWidget *debug_create_watch_tab(ScriptDebugWindow *view)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(box), controls, FALSE, FALSE, 0);
    view->watch_pin = gtk_button_new_with_label("Pin member");
    view->watch_only = gtk_check_button_new_with_label("Pinned only");
    GtkWidget *clear = gtk_button_new_with_label("Clear pins");
    gtk_box_pack_start(GTK_BOX(controls), view->watch_pin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), view->watch_only, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), clear, FALSE, FALSE, 0);
    g_signal_connect(view->watch_pin, "clicked", G_CALLBACK(debug_watch_pin), view);
    g_signal_connect(view->watch_only, "toggled", G_CALLBACK(debug_watch_filter), view);
    g_signal_connect(clear, "clicked", G_CALLBACK(debug_watch_clear), view);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    view->watch_canvas = gtk_drawing_area_new();
    gtk_widget_add_events(view->watch_canvas, GDK_BUTTON_PRESS_MASK);
    gtk_container_add(GTK_CONTAINER(scroll), view->watch_canvas);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    g_signal_connect(view->watch_canvas, "draw", G_CALLBACK(debug_watch_draw), view);
    g_signal_connect(view->watch_canvas, "button-press-event", G_CALLBACK(debug_watch_click), view);
    view->watch_status = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(box), view->watch_status, FALSE, FALSE, 0);
    debug_watch_layout(view);
    return box;
}
