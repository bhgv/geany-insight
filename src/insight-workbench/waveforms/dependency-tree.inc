/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: HDL waveform dependency tree.
 * RU: Дерево зависимостей сигналов HDL. */

/* Append distinct reads and writes for the declaration of a graph node. */
/* Добавляет уникальные места чтения и записи объявления узла графа. */
static void waveform_graph_uses(GtkTreeStore *graph, GtkTreeIter *parent,
    WaveDependencyIndex *dependencies, const gchar *hierarchy, const gchar *root_key)
{
    const InsightSymbol *declaration = g_hash_table_lookup(dependencies->declarations, hierarchy);
    gchar *key = NULL;
    if (root_key)
        key = g_strdup(root_key);
    else if (declaration)
        key = g_strdup_printf("%s\n%u:%s", declaration->path, declaration->line, declaration->signature);
    if (!key)
        return;

    GPtrArray *references = g_hash_table_lookup(dependencies->references, key);
    g_free(key);
    if (!references)
        return;

    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; i < references->len; i++)
    {
        const InsightSymbol *row = g_ptr_array_index(references, i);
        gchar *location = g_strdup_printf("%s\n%u\n%s\n%s",
            row->path, row->line, row->signature, row->inheritance);
        if (!g_hash_table_add(seen, location))
            continue;
        if (!dependencies->references_remaining || g_hash_table_size(seen) > 24)
        {
            dependencies->reference_limit_reached = TRUE;
            break;
        }
        gtk_tree_store_insert_with_values(graph, NULL, parent, -1,
            WAVE_USE_NAME, row->name, WAVE_USE_ROLE, row->inheritance,
            WAVE_USE_FILE, row->path, WAVE_USE_LINE, row->line, -1);
        dependencies->references_remaining--;
    }
    g_hash_table_unref(seen);
}

/* Group index rows while borrowing their data from the live HDL snapshot. */
/* Группирует строки индекса, используя данные снимка HDL без копирования строк. */
static void waveform_group_index_row(GHashTable *groups, gchar *key, const InsightSymbol *row)
{
    GPtrArray *group = g_hash_table_lookup(groups, key);
    if (group)
        g_free(key);
    else
    {
        group = g_ptr_array_new();
        g_hash_table_insert(groups, key, group);
    }
    g_ptr_array_add(group, (gpointer)row);
}

/* Recognize controls without turning them into traversable data-flow edges. */
/* Распознаёт управляющие связи без включения их в обход потока данных. */
static gboolean waveform_control_edge(const gchar *role, gboolean upstream)
{
    return (g_str_has_prefix(role, "condition-") || g_str_has_prefix(role, "event-") ||
            g_str_has_prefix(role, "posedge-") || g_str_has_prefix(role, "negedge-") ||
            g_str_has_prefix(role, "edge-")) &&
        g_str_has_suffix(role, upstream ? "-source" : "-destination");
}

typedef struct {
    const gchar *signal;
    gchar *selection;
    GtkTreeIter row;
} WaveDependencyNode;

static void waveform_dependency_node_free(gpointer data)
{
    WaveDependencyNode *node = data;
    g_free(node->selection);
    g_free(node);
}

/* EN: Parse selection into ordered endpoints first and last.
 * Return false for unknown or multidimensional selections.
 * RU: Разбирает константный срез selection в упорядоченные first и last.
 * Возвращает FALSE для неизвестного или многомерного среза. */
static gboolean waveform_selection_endpoints(const gchar *selection, gint64 *first, gint64 *last)
{
    if (!selection || *selection != '[')
        return FALSE;
    const gchar *start = selection + 1;
    gchar *end;
    gint64 left = g_ascii_strtoll(start, &end, 10);
    if (end == start || left < G_MININT32 || left > G_MAXINT32)
        return FALSE;
    while (g_ascii_isspace(*end))
        end++;
    gint64 right = left;
    gchar indexed = '\0';
    if ((*end == '+' || *end == '-') && end[1] == ':')
        indexed = *end++;
    if (*end == ':')
    {
        start = end + 1;
        right = g_ascii_strtoll(start, &end, 10);
        if (end == start || right < G_MININT32 || right > G_MAXINT32)
            return FALSE;
    }
    while (g_ascii_isspace(*end))
        end++;
    if (indexed)
    {
        if (right <= 0)
            return FALSE;
        right = indexed == '+' ? left + right - 1 : left - right + 1;
    }
    if (*end != ']' || (end[1] && !g_str_has_prefix(end + 1, " / ") &&
        !g_str_has_prefix(end + 1, " => ")))
        return FALSE;
    *first = left;
    *last = right;
    return TRUE;
}

/* EN: Normalize selection endpoints into inclusive bounds low and high.
 * RU: Нормализует границы среза selection в low и high. */
static gboolean waveform_selection_range(const gchar *selection, gint64 *low, gint64 *high)
{
    gint64 first, last;
    if (!waveform_selection_endpoints(selection, &first, &last))
        return FALSE;
    *low = MIN(first, last);
    *high = MAX(first, last);
    return TRUE;
}

static gboolean waveform_selections_disjoint(const gchar *current, const gchar *edge)
{
    gint64 current_low, current_high, edge_low, edge_high;
    return waveform_selection_range(current, &current_low, &current_high) &&
        waveform_selection_range(edge, &edge_low, &edge_high) &&
        (current_high < edge_low || edge_high < current_low);
}

/* EN: Format the packed range from first to last in declaration order.
 * RU: Форматирует срез от first до last в порядке объявления. */
static gchar *waveform_format_range(gint64 first, gint64 last)
{
    if (first == last)
        return g_strdup_printf("[%" G_GINT64_FORMAT "]", first);
    return g_strdup_printf("[%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT "]", first, last);
}

/* EN: Follow an exact range edge using the selected current intersection.
 * edge holds both ordered ranges. Return the peer selection and set display.
 * RU: Переносит пересечение current по точной связи edge. Возвращает срез
 * соседнего сигнала и записывает подпись в display. */
static gchar *waveform_project_selection(const gchar *current, const gchar *edge, gchar **display)
{
    const gchar *exact = edge ? strstr(edge, " => ") : NULL;
    if (!exact)
    {
        const gchar *separator = edge ? strstr(edge, " / ") : NULL;
        *display = g_strdup(edge);
        return g_strdup(separator ? separator + 3 : "all");
    }

    gint64 owner_first, owner_last, peer_first, peer_last;
    if (!waveform_selection_endpoints(edge, &owner_first, &owner_last) ||
        !waveform_selection_endpoints(exact + 4, &peer_first, &peer_last) ||
        ABS(owner_first - owner_last) != ABS(peer_first - peer_last))
    {
        *display = g_strdup(edge);
        return g_strdup("all");
    }
    gint64 low = MIN(owner_first, owner_last), high = MAX(owner_first, owner_last);
    gint64 selected_low, selected_high;
    if (waveform_selection_range(current, &selected_low, &selected_high))
    {
        low = MAX(low, selected_low);
        high = MIN(high, selected_high);
    }
    const gboolean ascending = owner_first <= owner_last;
    const gint64 first = ascending ? low : high, last = ascending ? high : low;
    const gint64 step = peer_first <= peer_last ? 1 : -1;
    gchar *owner = waveform_format_range(first, last);
    gchar *peer = waveform_format_range(peer_first + step * ABS(first - owner_first),
        peer_first + step * ABS(last - owner_first));
    *display = g_strconcat(owner, " / ", peer, NULL);
    g_free(owner);
    return peer;
}

/* EN: Walk from start in the upstream direction or downstream, using the
 * indexed dependencies and root_key for references. selection filters the root. Append data to rows and
 * graph, and record terminal conditions and events in their own lists.
 * remaining and control_remaining bound the number of emitted relationships.
 * RU: Обходит dependencies от start вверх или вниз по потоку (upstream).
 * root_key определяет ссылки корня, selection ограничивает его срез. Поток данных попадает в rows и graph,
 * условия в conditions, события в events. remaining и control_remaining
 * ограничивают число добавленных связей. */
static void waveform_walk_dependencies(GtkListStore *rows, GtkTreeStore *graph,
    GtkListStore *conditions, GtkListStore *events, WaveDependencyIndex *dependencies,
    const gchar *start, const gchar *root_key, const gchar *selection,
    gboolean upstream, guint *remaining,
    guint *control_remaining)
{
    const gchar *port_role = upstream ? "port-source" : "port-destination";
    const gchar *assign_role = upstream ? "assign-source" : "assign-destination";
    const gchar *procedural_role = upstream ? "procedural-source" : "procedural-destination";
    const gchar *interface_role = upstream ? "interface-source" : "interface-destination";
    GHashTable *visited = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, waveform_dependency_node_free);
    GtkTreeIter root;
    gtk_tree_store_insert_with_values(graph, &root, NULL, -1,
        WAVE_USE_NAME, start, WAVE_USE_ROLE, upstream ? insight_tr("Upstream") : insight_tr("Downstream"),
        WAVE_USE_SELECTION, selection, -1);
    if (!upstream)
        waveform_graph_uses(graph, &root, dependencies, start, root_key);
    GQueue pending = G_QUEUE_INIT;
    WaveDependencyNode *saved_root = g_new0(WaveDependencyNode, 1);
    saved_root->signal = start;
    saved_root->row = root;
    saved_root->selection = g_strdup(selection);
    g_hash_table_insert(visited, g_strdup_printf("%s\n%s", start, selection ? selection : "all"), saved_root);
    g_queue_push_tail(&pending, saved_root);

    for (guint distance = 1; !g_queue_is_empty(&pending) && *remaining; distance++)
    {
        guint level_size = g_queue_get_length(&pending);
        while (level_size-- && *remaining)
        {
            const WaveDependencyNode *node = g_queue_pop_head(&pending);
            GPtrArray *edges = g_hash_table_lookup(dependencies->edges, node->signal);
            if (!edges)
                continue;

            for (guint i = 0; i < edges->len && *remaining; i++)
            {
                const InsightSymbol *edge = g_ptr_array_index(edges, i);
                gboolean control = waveform_control_edge(edge->inheritance, upstream);
                if (!control && g_strcmp0(edge->inheritance, port_role) &&
                    g_strcmp0(edge->inheritance, assign_role) &&
                    g_strcmp0(edge->inheritance, procedural_role) &&
                    g_strcmp0(edge->inheritance, interface_role) &&
                    g_strcmp0(edge->inheritance, "port-peer"))
                    continue;
                if (waveform_selections_disjoint(node->selection, edge->type))
                    continue;

                if (control)
                {
                    if (*control_remaining)
                    {
                        GtkListStore *controls = g_str_has_prefix(edge->inheritance, "condition-") ?
                            conditions : events;
                        gtk_list_store_insert_with_values(controls, NULL, -1,
                            WAVE_USE_NAME, edge->name, WAVE_USE_FROM, node->signal,
                            WAVE_USE_ROLE, edge->inheritance, WAVE_USE_FILE, edge->path,
                            WAVE_USE_LINE, edge->line, WAVE_USE_CONNECTION, TRUE,
                            WAVE_USE_HOPS, distance, WAVE_USE_SELECTION, edge->type, -1);
                        (*control_remaining)--;
                    }
                    continue;
                }

                gchar *display;
                gchar *peer_selection = waveform_project_selection(node->selection, edge->type, &display);
                gchar *key = g_strdup_printf("%s\n%s", edge->name, peer_selection);
                GtkTreeIter branch;
                GtkTreeIter parent = node->row;
                gboolean repeated = g_hash_table_contains(visited, key);
                gtk_tree_store_insert_with_values(graph, &branch, &parent, -1,
                    WAVE_USE_ROLE, repeated ? insight_tr("Already shown") : edge->inheritance,
                    WAVE_USE_NAME, edge->name, WAVE_USE_FILE, edge->path,
                    WAVE_USE_LINE, edge->line, WAVE_USE_CONNECTION, TRUE,
                    WAVE_USE_HOPS, distance, WAVE_USE_SELECTION, display, -1);
                (*remaining)--;
                if (repeated)
                {
                    g_free(key);
                    g_free(peer_selection);
                    g_free(display);
                    continue;
                }

                waveform_graph_uses(graph, &branch, dependencies, edge->name, NULL);
                WaveDependencyNode *saved = g_new0(WaveDependencyNode, 1);
                saved->signal = edge->name;
                saved->selection = peer_selection;
                saved->row = branch;
                g_hash_table_insert(visited, key, saved);
                g_queue_push_tail(&pending, saved);
                const gchar *role = edge->inheritance;
                if (distance > 1)
                    role = upstream ? insight_tr("Upstream") : insight_tr("Downstream");
                gtk_list_store_insert_with_values(rows, NULL, -1,
                    WAVE_USE_ROLE, role, WAVE_USE_NAME, edge->name,
                    WAVE_USE_FILE, edge->path, WAVE_USE_LINE, edge->line,
                    WAVE_USE_CONNECTION, TRUE, WAVE_USE_HOPS, distance, WAVE_USE_SELECTION, display, -1);
                g_free(display);
            }
        }
    }
    g_queue_clear(&pending);
    g_hash_table_unref(visited);
}
