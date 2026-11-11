/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Browse elaborated HDL instances and navigate to their module definitions.
 * RU: Показывает дерево экземпляров HDL с переходами к определениям модулей. */

static GtkWidget *hdl_instance_page;

/* EN: Open the definition at path in tree. column and data are unused.
 * RU: Открывает определение по path в tree. column и data не используются. */
static void hdl_instance_open(GtkTreeView *tree, GtkTreePath *path,
                               GtkTreeViewColumn *column, gpointer data)
{
    (void)column;
    (void)data;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(model, &iter, path))
        return;
    gchar *file = NULL;
    gulong line = 0;
    gtk_tree_model_get(model, &iter, 2, &file, 3, &line, -1);
    if (file && *file && line)
    {
        gchar *native = utils_get_locale_from_utf8(file);
        diff_source_open(native, (guint)line);
        g_free(native);
    }
    g_free(file);
}

/* EN: Expand or collapse all rows of tree passed as data according to button.
 * RU: Раскрывает либо сворачивает все строки дерева data по действию button. */
static void hdl_instance_expand(GtkButton *button, gpointer data)
{
    if (g_object_get_data(G_OBJECT(button), "expand"))
        gtk_tree_view_expand_all(GTK_TREE_VIEW(data));
    else
        gtk_tree_view_collapse_all(GTK_TREE_VIEW(data));
}

/* EN: Create the instance browser owned by the Relations stack.
 * RU: Создаёт браузер экземпляров, принадлежащий стеку панели связей. */
static GtkWidget *hdl_instance_widget(void)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    hdl_instance_page = page;
    g_object_add_weak_pointer(G_OBJECT(page), (gpointer *)&hdl_instance_page);
    GtkWidget *status = gtk_label_new(insight_tr("Place the cursor inside a module and press Search."));
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_box_pack_start(GTK_BOX(page), status, FALSE, FALSE, 0);
    GtkTreeStore *store = gtk_tree_store_new(
        4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_ULONG);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_widget_set_name(tree, "insight-hdl-module-instances");
    gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(tree), TRUE);
    const gchar *headings[] = {"Instance", "Module"};
    for (guint i = 0; i < G_N_ELEMENTS(headings); i++)
    {
        GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
            insight_tr(headings[i]), gtk_cell_renderer_text_new(), "text", i, NULL);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
    }
    GtkTreeViewColumn *file_column = gtk_tree_view_column_new_with_attributes(
        insight_tr("Definition file"), gtk_cell_renderer_text_new(), "text", 2, NULL);
    gtk_tree_view_column_set_resizable(file_column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), file_column);
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(tree), 2);
    g_signal_connect(tree, "row-activated", G_CALLBACK(hdl_instance_open), NULL);
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    const gchar *labels[] = {"Expand all", "Collapse all"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        g_object_set_data(G_OBJECT(button), "expand", GINT_TO_POINTER(i == 0));
        g_signal_connect(button, "clicked", G_CALLBACK(hdl_instance_expand), tree);
        gtk_box_pack_start(GTK_BOX(actions), button, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(page), actions, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    g_object_set_data(G_OBJECT(page), "tree", tree);
    g_object_set_data(G_OBJECT(page), "status", status);
    return page;
}

/* EN: Return a source-coordinate key for file and line, optionally including name.
 * RU: Возвращает ключ позиции file и line с необязательным именем name. */
static gchar *hdl_instance_key(const gchar *file, gulong line, const gchar *name)
{
    return g_strdup_printf("%s\t%lu\t%s", file, line, name ? name : "");
}

/* EN: Resolve an instance's module through declaration references and module locations.
 * RU: Находит модуль экземпляра instance по ссылкам объявления и позициям модулей. */
static const InsightSymbol *hdl_instance_definition(const InsightSymbol *instance,
                                                   GHashTable *references, GHashTable *modules)
{
    gchar *key = hdl_instance_key(instance->path, instance->line, instance->name);
    const InsightSymbol *reference = g_hash_table_lookup(references, key);
    g_free(key);
    const gchar *path = reference ? reference->scope : instance->path;
    gulong line = reference ? g_ascii_strtoull(reference->type, NULL, 10) : instance->line;
    key = hdl_instance_key(path, line, NULL);
    const InsightSymbol *definition = g_hash_table_lookup(modules, key);
    g_free(key);
    return definition;
}

/* EN: Return whether hierarchy belongs to one of roots, allowing generated scope segments.
 * RU: Проверяет принадлежность hierarchy одному из roots с учётом генерируемых областей. */
static gboolean hdl_instance_under_root(const gchar *hierarchy, GHashTable *roots)
{
    gchar *candidate = g_strdup(hierarchy);
    gboolean found = FALSE;
    for (;;)
    {
        if (g_hash_table_contains(roots, candidate))
        {
            found = TRUE;
            break;
        }
        gchar *dot = strrchr(candidate, '.');
        if (!dot)
            break;
        *dot = 0;
    }
    g_free(candidate);
    return found;
}

/* EN: Ensure the ancestry of hierarchy exists in store; return its borrowed iterator.
 * RU: Создаёт предков hierarchy в store и возвращает заимствованный итератор узла. */
static GtkTreeIter *hdl_instance_ensure(GtkTreeStore *store, GHashTable *nodes,
                                        GHashTable *roots, const gchar *hierarchy, guint depth)
{
    GtkTreeIter *node = g_hash_table_lookup(nodes, hierarchy);
    if (node || depth >= 64 || g_hash_table_size(nodes) >= 4000)
        return node;
    GtkTreeIter *parent = NULL;
    const gchar *dot = strrchr(hierarchy, '.');
    if (dot && !g_hash_table_contains(roots, hierarchy))
    {
        gchar *prefix = g_strndup(hierarchy, dot - hierarchy);
        parent = hdl_instance_ensure(store, nodes, roots, prefix, depth + 1);
        g_free(prefix);
        if (!parent)
            return NULL;
    }
    node = g_new(GtkTreeIter, 1);
    gtk_tree_store_insert_with_values(store, node, parent, -1,
                                      0, dot ? dot + 1 : hierarchy, -1);
    g_hash_table_insert(nodes, g_strdup(hierarchy), node);
    return node;
}

/* EN: Fill store from index below the module enclosing file/cursor_line; return a status key.
 * RU: Заполняет store из index от модуля в file/cursor_line, возвращая ключ сообщения состояния. */
static const gchar *hdl_instance_fill(GtkTreeStore *store, const InsightIndex *index,
                                      const gchar *file, guint cursor_line)
{
    gtk_tree_store_clear(store);
    GHashTable *modules = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *references = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *roots = g_hash_table_new(g_str_hash, g_str_equal);
    GHashTable *nodes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    GPtrArray *instances = g_ptr_array_new();
    const InsightSymbol *current = NULL;
    for (gsize i = 0; i < insight_index_count(index); i++)
    {
        const InsightSymbol *row = insight_index_at(index, i);
        if (!strcmp(row->kind, "@hdldef") && !strcmp(row->type, "Module"))
        {
            g_hash_table_replace(modules, hdl_instance_key(row->path, row->line, NULL), (gpointer)row);
            if (!g_strcmp0(row->path, file) && row->line <= cursor_line &&
                (!current || row->line > current->line))
                current = row;
        }
        else if (!strcmp(row->kind, "@hdldef") && !strcmp(row->type, "Instance"))
            g_ptr_array_add(instances, (gpointer)row);
        else if (!strcmp(row->kind, "@hdlref") && !strcmp(row->inheritance, "instance"))
            g_hash_table_replace(references, hdl_instance_key(row->path, row->line, row->name),
                                  (gpointer)row);
    }
    for (guint i = 0; current && i < instances->len; i++)
    {
        const InsightSymbol *instance = instances->pdata[i];
        const InsightSymbol *definition = hdl_instance_definition(instance, references, modules);
        if (definition && definition->line == current->line && !strcmp(definition->path, current->path))
            g_hash_table_add(roots, (gpointer)instance->scope);
    }
    for (guint i = 0; i < instances->len && g_hash_table_size(nodes) < 4000; i++)
    {
        const InsightSymbol *instance = instances->pdata[i];
        if (!hdl_instance_under_root(instance->scope, roots))
            continue;
        GtkTreeIter *node = hdl_instance_ensure(store, nodes, roots, instance->scope, 0);
        const InsightSymbol *definition = hdl_instance_definition(instance, references, modules);
        if (node)
            gtk_tree_store_set(store, node, 1, definition ? definition->name : "",
                                2, definition ? definition->path : NULL,
                                3, definition ? definition->line : 0ul, -1);
    }
    const gchar *message = !current ? "Place the cursor inside a module and press Search."
                           : !g_hash_table_size(roots) ? "No elaborated instances for the current module."
                           : g_hash_table_size(nodes) >= 4000 ? "Instance tree limit reached."
                           : "Double-click an instance to open its module definition.";
    g_hash_table_unref(nodes);
    g_hash_table_unref(roots);
    g_hash_table_unref(references);
    g_hash_table_unref(modules);
    g_ptr_array_unref(instances);
    return message;
}

/* EN: Publish the hierarchy for the module enclosing the cursor captured in job.
 * RU: Показывает иерархию модуля, содержащего позицию курсора из job. */
static void hdl_instance_publish(const HdlJob *job)
{
    if (!hdl_instance_page)
        return;
    GtkTreeView *tree = g_object_get_data(G_OBJECT(hdl_instance_page), "tree");
    GtkTreeStore *store = GTK_TREE_STORE(gtk_tree_view_get_model(tree));
    GtkWidget *status = g_object_get_data(G_OBJECT(hdl_instance_page), "status");
    const gchar *message = hdl_instance_fill(store, job->result->index,
                                            job->paint_path, job->cursor_line);
    GtkTreePath *root = gtk_tree_path_new_first();
    gtk_tree_view_expand_row(tree, root, FALSE);
    gtk_tree_path_free(root);
    gtk_label_set_text(GTK_LABEL(status), insight_tr(message));
    gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack), "hdl-instances");
    set_usage_status(message);
}
