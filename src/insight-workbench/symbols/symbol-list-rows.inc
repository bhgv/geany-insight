/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Symbol and file list row preparation.
 * RU: Подготовка строк списков символов и файлов. */

enum
{
    SYMBOL_NAME, SYMBOL_SCOPE, SYMBOL_PATH, SYMBOL_LINE, SYMBOL_KIND, SYMBOL_COLUMNS
};

/* EN: Build a filter from entry. files selects file matching.
 * RU: Создаёт фильтр из entry. files включает поиск файлов. */
static InsightLookupFilter *entry_filter(GtkWidget *entry, gboolean files)
{
    GError *error = NULL;
    InsightLookupFilter *filter = insight_lookup_filter_new(gtk_entry_get_text(GTK_ENTRY(entry)), files, &error);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry), GTK_ENTRY_ICON_SECONDARY, error ? "dialog-warning" : (*gtk_entry_get_text(GTK_ENTRY(entry)) ? "edit-clear-symbolic" : NULL));
    gtk_entry_set_icon_tooltip_text(GTK_ENTRY(entry), GTK_ENTRY_ICON_SECONDARY, error ? error->message : NULL);
    g_clear_error(&error);
    return filter;
}
typedef struct
{
    gchar *name, *path, *directory;
    guint64 size;
    gboolean stat_valid;
} FileListRow;

/* EN: Release the file list row in data.
 * RU: Освобождает строку списка файлов из data. */
static void file_list_row_free(gpointer data)
{
    FileListRow *r = data;
    g_free(r->name);
    g_free(r->path);
    g_free(r->directory);
    g_free(r);
}
typedef struct
{
    guint64 generation;
    GCancellable *cancel;
    guint apply;
    InsightIndex *index;
    const InsightSymbol **rows;
    GPtrArray *files;
    gchar *local_path;
    guint count, position;
    gboolean local, collecting, iter_valid, has_more;
    GtkTreeIter iter;
    GHashTable *existing;
} SymbolListState;
typedef struct
{
    GWeakRef store;
    InsightIndex *index;
    gchar *query, *path;
    const InsightSymbol **rows;
    GPtrArray *paths, *files, *tags, *outlines;
    gint lang, sort_column;
    GtkSortType sort_direction;
    guint type_mask;
    guint count, total, limit;
    guint64 generation, epoch;
    gboolean local, has_more;
    InsightLanguageFilter *language;
} SymbolListJob;

/* EN: Return the view associated with store.
 * RU: Возвращает представление для store. */
static GtkWidget *symbol_list_view(GtkListStore *store)
{
    return store == wb.file_store ? wb.file_view : (store == wb.local_symbols ? wb.local_view : wb.symbol_view);
}

/* EN: Release the queue of row iterators in data.
 * RU: Освобождает очередь итераторов из data. */
static void symbol_iter_queue_free(gpointer data)
{
    GQueue *q = data;
    g_queue_free_full(q, g_free);
}

/* EN: Return a row key for iter in model. files selects the file row format.
 * RU: Возвращает ключ iter в model. files выбирает формат строки файла. */
static gchar *symbol_row_key(GtkTreeModel *model, GtkTreeIter *iter, gboolean files)
{
    gchar *name = NULL, *path = NULL, *scope = NULL, *kind = NULL;
    gulong line = 0;
    if (files) gtk_tree_model_get(model, iter, 0, &name, 1, &path, -1);
    else gtk_tree_model_get(model, iter, SYMBOL_NAME, &name, SYMBOL_SCOPE, &scope, SYMBOL_PATH, &path, SYMBOL_LINE, &line, SYMBOL_KIND, &kind, -1);
    /* Length-prefixed fields avoid collisions for paths containing separators. */
    GString *key = g_string_new(NULL);
    const gchar *fields[] =
    {
        name, path, scope, kind
    };
    for (guint i = 0;i<G_N_ELEMENTS(fields);i++)
    {
        const gchar *v = fields[i] ? fields[i] : "";
        g_string_append_printf(key, "%" G_GSIZE_FORMAT ":%s", strlen(v), v);
    }
    g_string_append_printf(key, ":%lu", line);
    g_free(name);
    g_free(path);
    g_free(scope);
    g_free(kind);
    return g_string_free(key, FALSE);
}

/* EN: Cancel list updates and clear pending state in s.
 * RU: Отменяет обновление списка и очищает состояние s. */
static void symbol_list_stop(SymbolListState *s)
{
    s->generation++;
    if (s->cancel)
    {
        g_cancellable_cancel(s->cancel);
        g_clear_object(&s->cancel);
    }
    if (s->apply)
    {
        g_source_remove(s->apply);
        s->apply = 0;
    }
    retire_index(s->index);
    s->index = NULL;
    g_clear_pointer(&s->rows, g_free);
    g_clear_pointer(&s->files, g_ptr_array_unref);
    g_clear_pointer(&s->local_path, g_free);
    s->count = s->position = 0;
    g_clear_pointer(&s->existing, g_hash_table_destroy);
    s->collecting = s->iter_valid = FALSE;
}

/* EN: Stop and release the list state in data.
 * RU: Останавливает обновление и освобождает состояние из data. */
static void symbol_list_state_free(gpointer data)
{
    SymbolListState *s = data;
    symbol_list_stop(s);
    g_free(s);
}

/* EN: Return the update state owned by store.
 * RU: Возвращает состояние обновления для store. */
static SymbolListState *symbol_list_state(GtkListStore *store)
{
    SymbolListState *s = g_object_get_data(G_OBJECT(store), "insight-symbol-worker");
    if (!s)
    {
        s = g_new0(SymbolListState, 1);
        g_object_set_data_full(G_OBJECT(store), "insight-symbol-worker", s, symbol_list_state_free);
    }
    return s;
}

/* EN: Apply a batch of pending rows to the store in data.
 * RU: Добавляет порцию строк в список из data. */
static gboolean symbol_list_apply(gpointer data)
{
    GtkListStore *store = data;
    SymbolListState *s = symbol_list_state(store);
    gint64 deadline = g_get_monotonic_time()+3000;
    if (!wb.alive)
    {
        s->apply = 0;
        return G_SOURCE_REMOVE;
    }
    if (s->collecting)
    {
        while (s->iter_valid)
        {
            gchar *key = symbol_row_key(GTK_TREE_MODEL(store), &s->iter, s->files != NULL);
            GQueue *q = g_hash_table_lookup(s->existing, key);
            if (!q)
            {
                q = g_queue_new();
                g_hash_table_insert(s->existing, key, q);
            }
            else g_free(key);
            GtkTreeIter *copy = g_new(GtkTreeIter, 1);
            *copy = s->iter;
            g_queue_push_tail(q, copy);
            s->iter_valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &s->iter);
            if (g_get_monotonic_time() >= deadline)
                return G_SOURCE_CONTINUE;
        }
        s->collecting = FALSE;
    }
    while (s->position<s->count)
    {
        guint n = s->position++;
        GtkTreeIter iter;
        /* Build the same key without inserting a temporary row into the model. */
        const InsightSymbol *tag = s->files ? NULL : s->rows[n];
        FileListRow *file = s->files ? g_ptr_array_index(s->files, n) : NULL;
        const gchar *fields[] =
        {
            file ? file->name : tag->name, file ? file->path : (s->local ? s->local_path : tag->path), tag ? tag->scope : NULL, tag ? tag->kind : NULL
        };
        GString *key = g_string_new(NULL);
        for (guint k = 0;k<G_N_ELEMENTS(fields);k++)
        {
            const gchar *v = fields[k] ? fields[k] : "";
            g_string_append_printf(key, "%" G_GSIZE_FORMAT ":%s", strlen(v), v);
        }
        g_string_append_printf(key, ":%lu", tag ? tag->line : 0);
        GQueue *q = g_hash_table_lookup(s->existing, key->str);
        GtkTreeIter *old = q ? g_queue_pop_head(q) : NULL;
        if (old)
        {
            iter = *old;
            g_free(old);
            if (file) gtk_list_store_set(store, &iter, 2, file->directory, 3, file->size, 4, file->stat_valid, -1);
        }
        /* Preserve the selected row while refreshing metadata. */
        else
        {
            gtk_list_store_append(store, &iter);
            if (file) gtk_list_store_set(store, &iter, 0, file->name, 1, file->path, 2, file->directory, 3, file->size, 4, file->stat_valid, -1);
            else gtk_list_store_set(store, &iter, SYMBOL_NAME, tag->name, SYMBOL_SCOPE, tag->scope, SYMBOL_PATH, s->local ? s->local_path : tag->path, SYMBOL_LINE, (gulong)tag->line, SYMBOL_KIND, tag->kind, -1);
        }
        g_string_free(key, TRUE);
        if (g_get_monotonic_time() >= deadline)
            return G_SOURCE_CONTINUE;
    }
    /* Delete only obsolete rows. The iter remains valid when the model is sorted. */
    GHashTableIter pending;
    gpointer value;
    g_hash_table_iter_init(&pending, s->existing);
    while (g_hash_table_iter_next(&pending, NULL, &value))
    {
        GQueue *q = value;
        GtkTreeIter *old;
        while ((old = g_queue_pop_head(q)))
        {
            gtk_list_store_remove(store, old);
            g_free(old);
            if (g_get_monotonic_time() >= deadline)
                return G_SOURCE_CONTINUE;
        }
        g_hash_table_iter_remove(&pending);
    }
    g_clear_pointer(&s->existing, g_hash_table_destroy);
    GtkWidget *view = symbol_list_view(store);
    if (view)
    {
        insight_symbol_view_page_state(GTK_TREE_VIEW(view), s->has_more);
        gtk_widget_set_sensitive(view, TRUE);
        insight_symbol_view_loading(GTK_TREE_VIEW(view), FALSE);
    }
    if (store == wb.file_store && wb.file_status)
    {
        gchar *text = insight_i18n_printf(s->has_more ? "Files: %u+" : "Files: %u", s->count);
        gtk_label_set_text(GTK_LABEL(wb.file_status), text);
        g_free(text);
        g_object_set_data(G_OBJECT(wb.file_status), "insight-files-count", GUINT_TO_POINTER(s->count));
        g_object_set_data(G_OBJECT(wb.file_status), "insight-files-more", GINT_TO_POINTER(s->has_more));
    }
    s->apply = 0;
    retire_index(s->index);
    s->index = NULL;
    g_clear_pointer(&s->rows, g_free);
    g_clear_pointer(&s->files, g_ptr_array_unref);
    return G_SOURCE_REMOVE;
}

/* EN: Release the list job and its resources in data.
 * RU: Освобождает задачу списка и ресурсы из data. */
static void symbol_list_job_free(gpointer data)
{
    SymbolListJob *j = data;
    insight_language_filter_free(j->language);
    g_weak_ref_clear(&j->store);
    retire_index(j->index);
    g_free(j->query);
    g_free(j->path);
    g_free(j->rows);
    g_clear_pointer(&j->paths, g_ptr_array_unref);
    g_clear_pointer(&j->files, g_ptr_array_unref);
    g_clear_pointer(&j->tags, g_ptr_array_unref);
    g_clear_pointer(&j->outlines, g_ptr_array_unref);
    g_free(j);
}

/* EN: Compare rows aa and bb using sort settings in data.
 * RU: Сравнивает строки aa и bb по настройкам из data. */
static gint virtual_compare(gconstpointer aa, gconstpointer bb, gpointer data)
{
    SymbolListJob *j = data;
    gint result = 0;
    if (j->paths)
    {
        const FileListRow *a = *(FileListRow *const *)aa, *b = *(FileListRow *const *)bb;
        if (j->sort_column == 3) result = (a->size>b->size)-(a->size<b->size);
        else result = g_utf8_collate(j->sort_column == 1 ? a->path:j->sort_column == 2 ? a->directory:a->name, j->sort_column == 1 ? b->path:j->sort_column == 2 ? b->directory:b->name);
        if (!result) result = g_strcmp0(a->path, b->path);
    }
    else
    {
        const InsightSymbol *a = *(const InsightSymbol *const *)aa, *b = *(const InsightSymbol *const *)bb;
        if (j->sort_column == SYMBOL_LINE) result = (a->line>b->line)-(a->line<b->line);
        else
        {
            const gchar *av = j->sort_column == SYMBOL_SCOPE ? a->scope:j->sort_column == SYMBOL_PATH ? a->path:j->sort_column == SYMBOL_KIND ? a->kind:a->name;
            const gchar *bv = j->sort_column == SYMBOL_SCOPE ? b->scope:j->sort_column == SYMBOL_PATH ? b->path:j->sort_column == SYMBOL_KIND ? b->kind:b->name;
            result = g_utf8_collate(av ? av:"", bv ? bv:"");
        }
        if (!result) result = g_strcmp0(a->path, b->path);
        if (!result) result = (a->line>b->line)-(a->line<b->line);
    }
    return j->sort_direction == GTK_SORT_DESCENDING ? -result:result;
}
