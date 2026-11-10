/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Relation query snapshots and history state.
 * RU: Снимки запросов связей и состояние истории. */


/* EN: Release the saved visit in data.
 * RU: Освобождает сохранённый переход из data. */
static void relation_visit_free(gpointer data)
{
    RelationVisit *visit = data;
    if (!visit) return;
    g_free(visit->query);
    g_free(visit->filter);
    g_free(visit->usr);
    g_free(visit->source);
    g_free(visit);
}

/* EN: Capture the current relation query state.
 * RU: Сохраняет состояние текущего запроса связей. */
static RelationVisit *relation_visit_current(void)
{
    RelationVisit *visit = g_new0(RelationVisit, 1);
    visit->query = g_strdup(gtk_entry_get_text(GTK_ENTRY(wb.usage_entry)));
    visit->filter = g_strdup(gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));
    visit->mode = gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));
    if (visit->mode == 9 || visit->mode == 10)
    {
        visit->usr = g_strdup(g_object_get_data(G_OBJECT(wb.relation_tree), "insight-call-root-usr"));
        visit->source = g_strdup(g_object_get_data(G_OBJECT(wb.relation_tree), "insight-call-root-source"));
    }
    visit->buffer = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_buffer));
    visit->code = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code));
    visit->comment_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(wb.usage_comments));
    visit->transitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_transitive));
    visit->tree = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle));
    visit->graph = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle));
    const gchar *shown = gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));
    insight_relation_graph_get_options(shown && !strcmp(shown, "treegraph") ? wb.relation_tree_graph : wb.relation_graph, &visit->vertical, &visit->zoom);
    return visit;
}

/* EN: Refresh the history controls.
 * RU: Обновляет элементы управления историей. */
static void relation_history_state(void)
{
    gtk_widget_set_sensitive(wb.relation_back, wb.relation_history->len>0);
    gtk_widget_set_sensitive(wb.relation_forward, wb.relation_future->len>0);
}

/* EN: Append visit to the bounded stack.
 * RU: Добавляет visit в стек с ограниченной длиной. */
static void relation_visit_push(GPtrArray *stack, RelationVisit *visit)
{
    if (stack->len >= 32)
        g_ptr_array_remove_index(stack, 0);
    g_ptr_array_add(stack, visit);
}

/* EN: Remove and return the latest entry from stack.
 * RU: Извлекает последний переход из stack. */
static RelationVisit *relation_visit_pop(GPtrArray *stack)
{
    RelationVisit *visit = g_ptr_array_index(stack, stack->len-1);
    g_ptr_array_index(stack, stack->len-1) = NULL;
    g_ptr_array_remove_index(stack, stack->len-1);
    return visit;
}

/* EN: Restore the query and view saved in visit.
 * RU: Восстанавливает запрос и представление из visit. */
static void relation_visit_restore(RelationVisit *visit)
{
    wb.relation_setting = TRUE;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow), FALSE);
    gtk_entry_set_text(GTK_ENTRY(wb.usage_entry), visit->query);
    gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry), visit->filter);
    gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode), visit->mode);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_buffer), visit->buffer);
    gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments), visit->comment_mode);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_code), visit->code);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_transitive), visit->transitive);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle), visit->tree);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle), visit->graph);
    /* Toggle callbacks can synchronize the two canvases. Restore geometry last. */
    insight_relation_graph_set_options(wb.relation_graph, visit->vertical, visit->zoom);
    insight_relation_graph_set_options(wb.relation_tree_graph, visit->vertical, visit->zoom);
    if (visit->mode == 9 || visit->mode == 10)
    {
        g_object_set_data(G_OBJECT(wb.usage_entry), "insight-semantic-restore", GINT_TO_POINTER(1));
        g_object_set_data_full(G_OBJECT(wb.usage_entry), "insight-semantic-usr", g_strdup(visit->usr), g_free);
        g_object_set_data_full(G_OBJECT(wb.usage_entry), "insight-semantic-source", g_strdup(visit->source), g_free);
    }
    wb.relation_setting = FALSE;
    relation_visit_free(visit);
    relation_history_state();
    relation_controls(NULL, NULL);
}
