/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Folder tree ordering, expansion and selection state.
 * RU: Порядок, раскрытие и выбор строк дерева каталогов. */


/* EN: Compare rows a and b in model. data is unused.
 * RU: Сравнивает строки a и b в model. data не используется. */
static gint folder_compare(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
    (void)data;
    gchar *an, *bn;
    gboolean ad, bd;
    gtk_tree_model_get(model, a, 0, &an, 2, &ad, -1);
    gtk_tree_model_get(model, b, 0, &bn, 2, &bd, -1);
    gint result = ad != bd ? (ad ? -1 : 1) : g_utf8_collate(an, bn);
    g_free(an);
    g_free(bn);
    return result;
}

/* EN: Find or create path in nodes relative to root at depth.
 * RU: Находит или создаёт path в nodes относительно root на уровне depth. */
static GtkTreeIter *tree_directory(GHashTable *nodes, const char *path, const char *root, guint depth)
{
    GtkTreeIter *iter = g_hash_table_lookup(nodes, path);
    if (iter)
        return iter;
    if (depth >= 64 || !strcmp(path, root))
        return NULL;
    gchar *parent = g_path_get_dirname(path);
    GtkTreeIter *above = strcmp(parent, path) ? tree_directory(nodes, parent, root, depth+1) : NULL;
    iter = g_new(GtkTreeIter, 1);
    gtk_tree_store_append(wb.project_tree, iter, above);
    gchar *name = g_path_get_basename(path);
    gtk_tree_store_set(wb.project_tree, iter, 0, name, 1, path, 2, TRUE, -1);
    g_hash_table_insert(nodes, g_strdup(path), iter);
    g_free(name);
    g_free(parent);
    return iter;
}
/* Keep the selected source and expanded folders stable across manifest/filter
 * rebuilds. Model paths alone are not stable when siblings are sorted. */
typedef struct
{
    const gchar *wanted;
    GtkTreePath *found;
    GHashTable *expanded;
}
FolderVisit;

/* EN: Collect expanded row from model and iter into data.
 * RU: Запоминает раскрытую строку row из model и iter в data. */
static gboolean folder_visit(GtkTreeModel *model, GtkTreePath *row, GtkTreeIter *iter, gpointer data)
{
    FolderVisit *visit = data;
    gchar *path = NULL;
    gtk_tree_model_get(model, iter, 1, &path, -1);
    if (visit->expanded && gtk_tree_view_row_expanded(GTK_TREE_VIEW(wb.tree_view), row))
        g_hash_table_add(visit->expanded, g_strdup(path));
    if (visit->wanted && !g_strcmp0(path, visit->wanted))
    {
        visit->found = gtk_tree_path_copy(row);
        g_free(path);
        return TRUE;
    }
    g_free(path);
    return FALSE;
}

/* EN: Restore expansion of row in model from data.
 * RU: Восстанавливает раскрытие row из model по данным data. */
static gboolean folder_restore_expanded(GtkTreeModel *model, GtkTreePath *row, GtkTreeIter *iter, gpointer data)
{
    gchar *path = NULL;
    gtk_tree_model_get(model, iter, 1, &path, -1);
    if (g_hash_table_contains(data, path)) gtk_tree_view_expand_row(GTK_TREE_VIEW(wb.tree_view), row, FALSE);
    g_free(path);
    return FALSE;
}

/* EN: Return the selected path and report whether it is a directory.
 * RU: Возвращает выбранный путь и признак каталога directory. */
static gchar *folder_selected_path(gboolean *directory)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.tree_view)), &model, &iter))
        return NULL;
    gchar *path = NULL;
    gtk_tree_model_get(model, &iter, 1, &path, 2, directory, -1);
    return path;
}
