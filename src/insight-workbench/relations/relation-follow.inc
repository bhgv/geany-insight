/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Relation panel controls, follow mode and filters.
 * RU: Управление панелью связей, слежение и фильтры. */


/* EN: Dispatch the relation command attached to widget.
 * RU: Выполняет команду панели связей из widget. */
static void relation_controls(GtkWidget *widget, gpointer data)
{
    (void)data;
    if (wb.relation_setting) return;
    wb.relation_dirty = TRUE;
    stop_usage();
    gint relation = gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));
    /* A deliberate mode change selects the symbol browser, not the separate
  * source-route canvas. Filters and automatic tracking preserve that canvas. */
    if (widget == wb.relation_mode && wb.relation_stack && !g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)), "routes")) gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "list");
    gtk_widget_set_sensitive(wb.relation_transitive, relation == 2 || relation == 4 || relation == 5);
    relation_view_update();
    gboolean references = gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)) == 0;
    gtk_widget_set_sensitive(wb.usage_code, references);
    gtk_widget_set_sensitive(wb.usage_comments, references);
    gtk_widget_set_sensitive(wb.usage_buffer, references || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)) == 3 || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)) == 7 || gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)) == 8);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow)))
        insight_workbench_refresh();
    else if (relation == 12 || *gtk_entry_get_text(GTK_ENTRY(wb.usage_entry))) usage_start(NULL, NULL);
    else
    {
        gtk_list_store_clear(wb.matches);
        set_usage_status("Enter a symbol for Relations");
    }
}
/* Disabling tracking freezes the displayed result set. It must not be
 * treated as a query-options change, which clears and starts the query. */

/* EN: Handle a follow-mode change from button.
 * RU: Обрабатывает переключение слежения из button. */
static void relation_follow_changed(GtkToggleButton *button, gpointer data)
{
    (void)data;
    if (wb.relation_setting) return;
    if (gtk_toggle_button_get_active(button))
    {
        wb.relation_dirty = TRUE;
        insight_workbench_refresh();
    }
    else if (wb.usage_mode != 3) stop_usage();
}

/* EN: Refresh the current relation query.
 * RU: Обновляет текущий запрос связей. */
static void relation_refresh_command(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    focus_widget_panel(wb.usage_entry);
    /* A panel command never dispatches the unrelated project text search. */
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode)) == 3)
    {
        set_usage_status("Select a symbol relation mode before refreshing Relations");
        return;
    }
    usage_start(NULL, NULL);
}

/* EN: Refresh the relation panel from the active editor when dirty.
 * RU: Обновляет панель связей по активному редактору с учётом dirty. */
static void relation_update_from_editor(gboolean dirty)
{
    if ((wb.usage_idle || wb.search_worker) && wb.usage_mode == 3) return;
    if (!wb.relation_follow || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_follow))) return;
    GeanyDocument *doc = document_get_current();
    gchar word[1025] =
    {
        0
    };
    if (doc)
    {
        refresh_index();
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_enclosing)))
        {
            TMTag *tag = relation_enclosing_function(doc);
            if (tag && tag->name)
                g_strlcpy(word, tag->name, sizeof word);
        }
        else editor_find_current_word_sciwc(doc->editor, sci_get_current_position(doc->editor->sci), word, sizeof word);
        if (!*word && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_enclosing)))
        {
            gulong line = (gulong)sci_get_current_line(doc->editor->sci)+1;
            TMTag *selected = NULL;
            guint best = 0;
            if (doc->tm_file && doc->tm_file->tags_array) for (guint i = 0;i<doc->tm_file->tags_array->len;i++)
            {
                TMTag *tag = g_ptr_array_index(doc->tm_file->tags_array, i);
                if (!tag || tag->line != line || !tag->name || !g_utf8_validate(tag->name, -1, NULL) || strlen(tag->name) >= sizeof word) continue;
                guint rank = (tag->kind_letter && strchr("fmcsi", tag->kind_letter)) ? 2 : 1;
                if (!selected || rank>best)
                {
                    selected = tag;
                    best = rank;
                }
            }
            if (selected)
                g_strlcpy(word, selected->name, sizeof word);
        }
    }
    gint mode = gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));
    if (!dirty && !wb.relation_dirty && mode == wb.relation_last_mode && !g_strcmp0(word, wb.relation_word)) return;
    wb.relation_dirty = FALSE;
    wb.relation_last_mode = mode;
    g_free(wb.relation_word);
    wb.relation_word = g_strdup(word);
    gtk_entry_set_text(GTK_ENTRY(wb.usage_entry), word);
    if (*word) usage_start(NULL, NULL);
    else
    {
        stop_usage();
        gtk_list_store_clear(wb.matches);
        set_usage_status("Relations — no selected identifier");
    }
}

/* EN: Return whether iter in model matches the active filter.
 * RU: Проверяет, соответствует ли iter из model активному фильтру. */
static gboolean relation_row_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    (void)data;
    const gchar *query = gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry));
    if (!*query)
        return TRUE;
    gboolean match = FALSE;
    const gint fields[] =
    {
        USAGE_SYMBOL, USAGE_DISPLAY, USAGE_SNIPPET
    };
    for (guint i = 0;i<G_N_ELEMENTS(fields) && !match;i++)
    {
        gchar *value = NULL;
        gtk_tree_model_get(model, iter, fields[i], &value, -1);
        match = insight_lookup_text_match(value, query);
        g_free(value);
    }
    return match;
}

/* EN: Update filtering when entry changes.
 * RU: Обновляет фильтрацию при изменении entry. */
static void relation_filter_changed(GtkEditable *entry, gpointer data)
{
    (void)entry;
    (void)data;
    if (wb.relation_filter) gtk_tree_model_filter_refilter(wb.relation_filter);
    if (wb.relation_tree)
        insight_relation_tree_filter(wb.relation_tree, gtk_entry_get_text(GTK_ENTRY(wb.relation_filter_entry)));
}
typedef struct
{
    gchar *query, *filter, *usr, *source;
    gint mode, comment_mode, zoom;
    gboolean buffer, code, transitive, tree, graph, vertical;
}
RelationVisit;
