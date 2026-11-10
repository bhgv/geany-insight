/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Grouped panel rows, filters and saved tree state.
 * RU: Строки и фильтры групп, сохранение состояния дерева. */


/* EN: Return the settings prefix for panel p.
 * RU: Возвращает префикс настроек панели p. */
static const gchar *grouped_prefix(GroupedPanel *p)
{
    return p->symbols ? "symbol-categories" : "file-types";
}

/* EN: Read name, path and line for iter in model according to p.
 * RU: Читает name, path и line для iter в model с учётом p. */
static gboolean grouped_row(GtkTreeModel *model, GtkTreeIter *iter, GroupedPanel *p, gchar **name, gchar **path, gulong *line)
{
    if (p->symbols)
    {
        gtk_tree_model_get(model, iter, SYMBOL_NAME, name, SYMBOL_PATH, path, SYMBOL_LINE, line, -1);
        return *line != 0;
    }
    gboolean group = FALSE;
    gtk_tree_model_get(model, iter, 0, name, 1, path, 2, &group, -1);
    *line = group ? 0 : 1;
    return !group;
}

/* EN: Build a stable key for iter in model for panel p.
 * RU: Формирует устойчивый ключ iter в model для панели p. */
static gchar *grouped_key(GtkTreeModel *model, GtkTreeIter *iter, GroupedPanel *p)
{
    GString *key = g_string_new(NULL);
    GtkTreeIter current = *iter;
    for (guint depth = 0;depth<64;depth++)
    {
        gchar *name = NULL, *path = NULL, *scope = NULL, *kind = NULL;
        gulong line = 0;
        grouped_row(model, &current, p, &name, &path, &line);
        if (p->symbols) gtk_tree_model_get(model, &current, SYMBOL_SCOPE, &scope, SYMBOL_KIND, &kind, -1);
        gchar *header = p->symbols && !line ? g_strdup_printf("%s:%s", kind ? kind : "", scope ? scope : "") : NULL;
        const gchar *value = header ? header : name;
        g_string_append_printf(key, "%lu:%zu:%s:%zu:%s;", line, value ? strlen(value) : 0, value ? value : "", path ? strlen(path) : 0, path ? path : "");
        g_free(header);
        g_free(name);
        g_free(path);
        g_free(scope);
        g_free(kind);
        GtkTreeIter parent;
        if (!gtk_tree_model_iter_parent(model, &parent, &current)) break;
        current = parent;
    }
    return g_string_free(key, FALSE);
}
typedef struct
{
    GroupedPanel *panel;
    GHashTable *expanded;
    const gchar *wanted;
    GtkTreePath *found;
    gboolean restore;
}
GroupedVisit;

/* EN: Capture or restore expansion of row using iter, model and data.
 * RU: Запоминает или восстанавливает раскрытие row по iter, model и data. */
static gboolean grouped_visit(GtkTreeModel *model, GtkTreePath *row, GtkTreeIter *iter, gpointer data)
{
    GroupedVisit *v = data;
    gchar *key = grouped_key(model, iter, v->panel);
    if (v->expanded)
    {
        if (v->restore && g_hash_table_contains(v->expanded, key))
        {
            gtk_tree_view_expand_to_path(GTK_TREE_VIEW(v->panel->view), row);
            gtk_tree_view_expand_row(GTK_TREE_VIEW(v->panel->view), row, FALSE);
        }
        else if (!v->restore && gtk_tree_view_row_expanded(GTK_TREE_VIEW(v->panel->view), row)) g_hash_table_add(v->expanded, g_strdup(key));
    }
    if (v->wanted && !strcmp(key, v->wanted)) v->found = gtk_tree_path_copy(row);
    g_free(key);
    return v->found != NULL;
}

/* EN: Save expanded rows for p in expanded and return the selection key.
 * RU: Запоминает раскрытые строки p в expanded и возвращает ключ выбора. */
static gchar *grouped_capture(GroupedPanel *p, GHashTable *expanded)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    gchar *selected = NULL;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)), &model, &iter)) selected = grouped_key(model, &iter, p);
    GroupedVisit v =
    {
        .panel = p, .expanded = expanded
    };
    gtk_tree_model_foreach(GTK_TREE_MODEL(p->store), grouped_visit, &v);
    p->setting = TRUE;
    return selected;
}

/* EN: Restore expanded rows and selected key in p.
 * RU: Восстанавливает раскрытие expanded и выбор selected в p. */
static void grouped_restore(GroupedPanel *p, GHashTable *expanded, gchar *selected)
{
    GroupedVisit v =
    {
        .panel = p, .expanded = expanded, .restore = TRUE
    };
    gtk_tree_model_foreach(GTK_TREE_MODEL(p->store), grouped_visit, &v);
    if (*gtk_entry_get_text(GTK_ENTRY(p->entry))) gtk_tree_view_expand_all(GTK_TREE_VIEW(p->view));
    if (selected)
    {
        v = (GroupedVisit)
        {
            .panel = p, .wanted = selected
        };
        gtk_tree_model_foreach(GTK_TREE_MODEL(p->store), grouped_visit, &v);
        if (v.found)
        {
            gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)), v.found);
            gtk_tree_path_free(v.found);
        }
    }
    p->setting = FALSE;
    g_free(selected);
    g_hash_table_destroy(expanded);
}

/* EN: Display shown and total row counts in p.
 * RU: Показывает в p число видимых shown и всех total строк. */
static void grouped_count(GroupedPanel *p, guint shown, guint total)
{
    p->shown = shown;
    p->total = total;
    gchar *text = insight_i18n_printf(p->symbols ? "Category symbols: %u / %u" : "Type files: %u / %u", shown, total);
    gtk_label_set_text(GTK_LABEL(p->status), text);
    g_free(text);
}

/* EN: Refresh data for panel p.
 * RU: Обновляет данные панели p. */
static void grouped_refresh(GroupedPanel *p)
{
    if (p->symbols) categories_filter(NULL, NULL);
    else file_types_refresh();
}

/* EN: Run the deferred refresh for the panel in data.
 * RU: Выполняет отложенное обновление панели из data. */
static gboolean grouped_idle(gpointer data)
{
    GroupedPanel *p = data;
    p->timer = 0;
    if (wb.alive) grouped_refresh(p);
    return G_SOURCE_REMOVE;
}

/* EN: Schedule a refresh for the panel in data. entry is unused.
 * RU: Планирует обновление панели из data. entry не используется. */
static void grouped_changed(GtkEditable *entry, gpointer data)
{
    (void)entry;
    GroupedPanel *p = data;
    p->limit = p->symbols ? 5000 : 10000;
    if (p->timer)
        g_source_remove(p->timer);
    p->timer = g_timeout_add(120, grouped_idle, p);
}

/* EN: Run any queued refresh for p immediately.
 * RU: Немедленно выполняет отложенное обновление p. */
static void grouped_flush(GroupedPanel *p)
{
    if (p->timer)
    {
        g_source_remove(p->timer);
        p->timer = 0;
        grouped_refresh(p);
    }
}

/* EN: Dispatch selection changes for the panel in data.
 * RU: Обрабатывает изменения selection в панели из data. */
static void grouped_selected(GtkTreeSelection *selection, gpointer data)
{
    GroupedPanel *p = data;
    if (p->setting) return;
    if (p->symbols) browser_selected(selection, NULL);
    else tree_selected(selection, NULL);
}
enum
{
    GP_OPEN, GP_NAME, GP_PATH, GP_LOCATION, GP_INFO, GP_REFERENCES, GP_FILTER, GP_CLEAR, GP_EXPAND, GP_COLLAPSE, GP_EXPAND_ALL, GP_COLLAPSE_ALL, GP_COLUMNS, GP_RESET, GP_MORE, GP_HELP
};
