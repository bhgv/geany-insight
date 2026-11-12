/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Search and operate on manual name rules through filtered/sorted GTK models, retaining canonical rule identities.
 * RU: Ищет и обрабатывает ручные правила имён через фильтрованные/сортированные модели GTK с сохранением ключей правил. */

static void native_name_edit(GtkWidget *button, gpointer data);

/* EN: Show row from model if its canonical rule matches session data's search; a closed session leaves rows unchanged.
 * RU: Показывает row модели model, если исходное правило подходит поиску session data; закрытый сеанс не меняет строки. */
static gboolean native_name_visible(GtkTreeModel *model, GtkTreeIter *row, gpointer data)
{
    NativeDebug *session = data;
    if (session->closed)
        return TRUE;
    guint index = 0;
    gtk_tree_model_get(model, row, 5, &index, -1);
    NativeAssembly *assembly = session->assembly;
    return index < assembly->rules->len && native_name_rule_matches(assembly->rules->pdata[index],
        gtk_entry_get_text(GTK_ENTRY(assembly->rule_search)));
}

/* EN: Refilter session data after entry changes without querying GDB; ignore entry and closed sessions.
 * RU: Обновляет фильтр session data при изменении entry без запросов GDB; entry и закрытые сеансы не используются. */
static void native_name_filter_changed(GtkWidget *entry, gpointer data)
{
    (void)entry;
    NativeDebug *session = data;
    if (!session->closed && session->assembly->rule_filter)
        gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(session->assembly->rule_filter));
}

/* EN: Select activated path in view and edit its canonical rule in session data; column is unused and closed sessions are ignored.
 * RU: Выбирает активированный path в view и редактирует исходное правило session data; column не используется, закрытый сеанс пропускается. */
static void native_name_row_activated(GtkTreeView *view, GtkTreePath *path,
                                      GtkTreeViewColumn *column, gpointer data)
{
    (void)column;
    NativeDebug *session = data;
    if (session->closed)
        return;
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(view), path);
    g_object_set_data(G_OBJECT(view), "name-action", GINT_TO_POINTER(-1));
    native_name_edit(GTK_WIDGET(view), session);
}

/* EN: Copy the selected rule's anchor from session data, using model column 1 rather than its visible position; button is unused.
 * RU: Копирует адрес выбранного правила session data из столбца 1 модели, а не по позиции строки; button не используется. */
static void native_name_copy_anchor(GtkWidget *button, gpointer data)
{
    (void)button;
    NativeDebug *session = data;
    if (session->closed)
        return;
    GtkTreeModel *model = NULL;
    GtkTreeIter row;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(session->assembly->rules_view));
    if (!gtk_tree_selection_get_selected(selection, &model, &row))
        return;
    gchar *anchor = NULL;
    gtk_tree_model_get(model, &row, 1, &anchor, -1);
    if (anchor)
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), anchor, -1);
    g_free(anchor);
}

/* EN: Add search/copy controls for session to box before constructing its filter; widgets remain owned by GTK.
 * RU: Добавляет поиск и копирование session в box до создания фильтра; виджеты остаются во владении GTK. */
static void native_name_search_widget(NativeDebug *session, GtkWidget *box)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *search = gtk_search_entry_new();
    session->assembly->rule_search = search;
    gtk_widget_set_name(search, "insight-gdb-name-search");
    gtk_entry_set_max_length(GTK_ENTRY(search), 256);
    gtk_entry_set_placeholder_text(GTK_ENTRY(search), insight_tr("Find address, name, type or function"));
    g_signal_connect(search, "changed", G_CALLBACK(native_name_filter_changed), session);
    gtk_box_pack_start(GTK_BOX(bar), search, TRUE, TRUE, 0);
    GtkWidget *copy = gtk_button_new_with_label(insight_tr("Copy name anchor"));
    g_signal_connect(copy, "clicked", G_CALLBACK(native_name_copy_anchor), session);
    gtk_box_pack_start(GTK_BOX(bar), copy, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
}
