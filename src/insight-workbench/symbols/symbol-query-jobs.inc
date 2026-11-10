/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Background symbol queries and virtual list snapshots.
 * RU: Фоновые запросы символов и снимки виртуальных списков. */


/* EN: Query the index for data and complete task. cancel stops work. source is unused.
 * RU: Запрашивает индекс для data и завершает task. cancel отменяет работу. source не используется. */
static void symbol_list_job_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    SymbolListJob *j = data;
    InsightLookupFilter *filter = insight_lookup_filter_new(j->query, j->paths != NULL, NULL);
    if (j->paths)
    {
        j->files = g_ptr_array_new_with_free_func(file_list_row_free);
        GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
        for (guint n = 0;filter && n<j->paths->len && !g_cancellable_is_cancelled(cancel);n++)
        {
            const char *path = g_ptr_array_index(j->paths, n);
            if (!insight_language_filter_match(j->language, path)) continue;
            if (!g_utf8_validate(path, -1, NULL) || g_hash_table_contains(seen, path)) continue;
            g_hash_table_add(seen, (gpointer)path);
            if (insight_lookup_filter_match(filter, NULL, NULL, path, NULL))
            {
                FileListRow *r = g_new0(FileListRow, 1);
                r->path = g_strdup(path);
                r->name = g_path_get_basename(path);
                r->directory = g_path_get_dirname(path);
                gchar *native = utils_get_locale_from_utf8(path);
                GStatBuf st;
                r->stat_valid = native && g_stat(native, &st) == 0 && S_ISREG(st.st_mode);
                if (r->stat_valid) r->size = (guint64)st.st_size;
                g_free(native);
                g_ptr_array_add(j->files, r);
                j->count++;
            }
        }
        g_hash_table_destroy(seen);
    }
    else
    {
        if (j->local && !g_cancellable_is_cancelled(cancel))
        {
            j->index = insight_index_new();
            TMSourceFile captured =
            {
                .tags_array = j->tags, .lang = j->lang
            };
            if (!insight_native_outline_import(j->index, &captured, j->path ? j->path : "@untitled"))
            {
                insight_lookup_filter_free(filter);
                g_task_return_boolean(task, FALSE);
                return;
            }
        }
        if (j->outlines)
        {
            j->index = insight_index_new();
            for (guint n = 0;n<j->outlines->len && !g_cancellable_is_cancelled(cancel);n++)
            {
                OutlineFile *o = g_ptr_array_index(j->outlines, n);
                TMSourceFile file =
                {
                    .tags_array = o->tags, .lang = o->lang
                };
                if (!insight_native_outline_import(j->index, &file, o->path))
                {
                    insight_lookup_filter_free(filter);
                    g_task_return_boolean(task, FALSE);
                    return;
                }
            }
        }
        j->rows = g_new(const InsightSymbol *, MAX((gsize)1, insight_index_count(j->index)));
        if (filter) for (gsize n = 0;n<insight_index_count(j->index);n++)
        {
            if (g_cancellable_is_cancelled(cancel)) break;
            const InsightSymbol *tag = insight_index_at(j->index, n);
            if (!insight_language_filter_match(j->language, tag->path)) continue;
            if (j->path && g_strcmp0(j->path, tag->path)) continue;
            if (insight_lookup_filter_match(filter, tag->name, tag->scope, tag->path, g_str_has_prefix(tag->kind, "@decl:") ? strrchr(tag->kind, ':')+1 : tag->kind))
            {
                j->total++;
                if (insight_symbol_kind_matches(tag->kind, j->type_mask)) j->rows[j->count++] = tag;
            }
        }
    }
    if (!g_cancellable_is_cancelled(cancel) && j->count>1)
    {
        if (j->files)
            g_ptr_array_sort_with_data(j->files, virtual_compare, j);
        else g_qsort_with_data(j->rows, j->count, sizeof(*j->rows), virtual_compare, j);
    }
    insight_lookup_filter_free(filter);
    g_task_return_boolean(task, TRUE);
}
typedef struct
{
    InsightIndex *index;
    const InsightSymbol **rows;
    GPtrArray *files;
    gchar *path;
    gboolean local;
} VirtualSnapshot;

/* EN: Release the list snapshot in data.
 * RU: Освобождает снимок списка из data. */
static void virtual_snapshot_free(gpointer data)
{
    VirtualSnapshot *s = data;
    retire_index(s->index);
    g_free(s->rows);
    g_clear_pointer(&s->files, g_ptr_array_unref);
    g_free(s->path);
    g_free(s);
}

/* EN: Read row n from data into values unless cancel is set.
 * RU: Читает строку n из data в values с учётом cancel. */
static gboolean virtual_snapshot_read(guint n, GValue *values, gpointer data, GCancellable *cancel)
{
    if (g_cancellable_is_cancelled(cancel))
        return FALSE;
    VirtualSnapshot *s = data;
    if (s->files)
    {
        FileListRow *f = g_ptr_array_index(s->files, n);
        g_value_set_string(&values[0], f->name);
        g_value_set_string(&values[1], f->path);
        g_value_set_string(&values[2], f->directory);
        g_value_set_uint64(&values[3], f->size);
        g_value_set_boolean(&values[4], f->stat_valid);
    }
    else
    {
        const InsightSymbol *t = s->rows[n];
        g_value_set_string(&values[SYMBOL_NAME], t->name);
        g_value_set_string(&values[SYMBOL_SCOPE], t->scope);
        g_value_set_string(&values[SYMBOL_PATH], s->local ? s->path:t->path);
        g_value_set_ulong(&values[SYMBOL_LINE], (gulong)t->line);
        g_value_set_string(&values[SYMBOL_KIND], t->kind);
    }
    return TRUE;
}

/* EN: Reload the list for view. data is unused.
 * RU: Обновляет список view. data не используется. */
static void virtual_reload(GtkTreeView *view, gpointer data)
{
    (void)data;
    if (GTK_WIDGET(view) == wb.file_view)
    {
        g_object_set_data(G_OBJECT(wb.file_store), "insight-request-key", NULL);
        files_filter(NULL, NULL);
    }
    else if (GTK_WIDGET(view) == wb.symbol_view) browser_filter(NULL, NULL);
    else if (GTK_WIDGET(view) == wb.local_view) local_filter(NULL, NULL);
}

/* EN: Publish result if it is still current. source and data are unused.
 * RU: Публикует result, если он ещё актуален. source и data не используются. */
static void symbol_list_job_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)source;
    (void)data;
    GTask *task = G_TASK(result);
    SymbolListJob *j = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    GtkListStore *store = g_weak_ref_get(&j->store);
    if (!store) return;
    SymbolListState *s = symbol_list_state(store);
    if (wb.alive && j->epoch == workbench_epoch && j->generation == s->generation)
    {
        g_clear_object(&s->cancel);
        s->has_more = ok && j->has_more;
        GtkWidget *virtual_view = symbol_list_view(store);
        if (ok && insight_virtual_list_attached(GTK_TREE_VIEW(virtual_view)))
        {
            VirtualSnapshot *snapshot = g_new0(VirtualSnapshot, 1);
            snapshot->index = j->index;
            j->index = NULL;
            snapshot->rows = j->rows;
            j->rows = NULL;
            snapshot->files = j->files;
            j->files = NULL;
            snapshot->local = j->local;
            snapshot->path = g_strdup(j->path);
            g_object_set_data(G_OBJECT(virtual_view), "insight-virtual-total", GUINT_TO_POINTER(snapshot->files ? j->count:j->total));
            insight_virtual_list_replace(GTK_TREE_VIEW(virtual_view), j->count, virtual_snapshot_read, snapshot, virtual_snapshot_free);
            insight_symbol_view_page_state(GTK_TREE_VIEW(virtual_view), FALSE);
            gtk_widget_set_sensitive(virtual_view, TRUE);
            insight_symbol_view_loading(GTK_TREE_VIEW(virtual_view), FALSE);
            if (store == wb.file_store && wb.file_status)
            {
                gchar *text = insight_i18n_printf("Files: %u", j->count);
                gtk_label_set_text(GTK_LABEL(wb.file_status), text);
                g_free(text);
                g_object_set_data(G_OBJECT(wb.file_status), "insight-files-count", GUINT_TO_POINTER(j->count));
                g_object_set_data(G_OBJECT(wb.file_status), "insight-files-more", NULL);
            }
            g_object_unref(store);
            return;
        }
        if (ok)
        {
            s->index = insight_index_ref(j->index);
            s->rows = j->rows;
            j->rows = NULL;
            s->files = j->files;
            j->files = NULL;
            s->count = j->count;
            s->position = 0;
            s->local = j->local;
            s->local_path = g_strdup(j->path);
        }
        else
        {
            if (j->paths) s->files = g_ptr_array_new_with_free_func(file_list_row_free);
            ui_set_statusbar(TRUE, "Symbol snapshot unavailable or exceeds its limit");
        }
        s->existing = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, symbol_iter_queue_free);
        s->collecting = TRUE;
        s->iter_valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &s->iter);
        GtkWidget *view = symbol_list_view(store);
        if (view)
        {
            gtk_widget_set_sensitive(view, FALSE);
            insight_symbol_view_loading(GTK_TREE_VIEW(view), TRUE);
        }
        if (!s->apply) s->apply = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, symbol_list_apply, g_object_ref(store), g_object_unref);
    }
    g_object_unref(store);
}

/* EN: Create a query for store from entry with limit and files mode.
 * RU: Создаёт запрос для store из entry с limit и режимом files. */
static SymbolListJob *symbol_list_begin(GtkListStore *store, GtkWidget *entry, guint limit, gboolean files)
{
    SymbolListState *s = symbol_list_state(store);
    symbol_list_stop(s);
    GtkWidget *view = symbol_list_view(store);
    if (view)
    {
        gtk_widget_set_sensitive(view, TRUE);
        insight_symbol_view_loading(GTK_TREE_VIEW(view), FALSE);
    }
    InsightLookupFilter *filter = entry_filter(entry, files);
    if (!filter)
    {
        if (view && insight_virtual_list_attached(GTK_TREE_VIEW(view)))
        {
            g_object_set_data(G_OBJECT(view), "insight-virtual-total", NULL);
            insight_virtual_list_clear(GTK_TREE_VIEW(view));
        }
        s->has_more = FALSE;
        if (view)
        {
            gtk_widget_set_sensitive(view, FALSE);
            insight_symbol_view_loading(GTK_TREE_VIEW(view), TRUE);
        }
        s->files = files ? g_ptr_array_new_with_free_func(file_list_row_free) : NULL;
        s->existing = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, symbol_iter_queue_free);
        s->collecting = TRUE;
        s->iter_valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &s->iter);
        s->apply = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, symbol_list_apply, g_object_ref(store), g_object_unref);
        return NULL;
    }
    insight_lookup_filter_free(filter);
    SymbolListJob *j = g_new0(SymbolListJob, 1);
    g_weak_ref_init(&j->store, store);
    j->query = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    j->limit = limit;
    j->type_mask = store == wb.file_store ? G_MAXUINT:insight_symbol_view_types_mask(GTK_TREE_VIEW(view));
    j->sort_column = 0;
    j->sort_direction = GTK_SORT_ASCENDING;
    gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(store), &j->sort_column, &j->sort_direction);
    j->generation = s->generation;
    j->epoch = workbench_epoch;
    GeanyDocument *active = document_get_current();
    if (active && (store == wb.symbols || store == wb.file_store)) j->language = insight_language_filter_new(active->file_name, active->file_type);
    return j;
}

/* EN: Queue query j for store.
 * RU: Запускает запрос j для store. */
static void symbol_list_launch(GtkListStore *store, SymbolListJob *j)
{
    GtkWidget *view = symbol_list_view(store);
    if (view)
    {
        if (!insight_virtual_list_attached(GTK_TREE_VIEW(view))) gtk_widget_set_sensitive(view, FALSE);
        insight_symbol_view_loading(GTK_TREE_VIEW(view), TRUE);
    }
    SymbolListState *s = symbol_list_state(store);
    s->cancel = g_cancellable_new();
    GTask *task = g_task_new(NULL, s->cancel, symbol_list_job_done, NULL);
    g_task_set_task_data(task, j, symbol_list_job_free);
    g_task_run_in_thread(task, symbol_list_job_run);
    g_object_unref(task);
}

/* EN: Populate store using entry and optional path, up to limit rows.
 * RU: Заполняет store по entry и необязательному path до limit строк. */
static void fill_symbol_list(GtkListStore *store, GtkWidget *entry, const char *path, guint limit)
{
    refresh_index();
    SymbolListJob *j = symbol_list_begin(store, entry, limit, FALSE);
    if (!j) return;
    if (store == wb.symbols)
    {
        j->outlines = g_ptr_array_new_with_free_func(outline_free);
        GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
        gchar *root = wb.root ? utils_get_utf8_from_locale(wb.root) : NULL;
        for (guint n = 0;n<documents_array->len;n++)
        {
            GeanyDocument *doc = g_ptr_array_index(documents_array, n);
            if (!doc->is_valid || !doc->file_name || (wb.workspace_from_project && root && !insight_project_files_contains(wb.membership, root, doc->file_name))) continue;
            g_ptr_array_add(j->outlines, outline_capture(doc->tm_file, doc->file_name));
            g_hash_table_add(seen, doc->file_name);
        }
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, wb.declarations);
        while (g_hash_table_iter_next(&iter, &key, &value)) if (!g_hash_table_contains(seen, key))
            g_ptr_array_add(j->outlines, outline_copy(value));
        g_hash_table_unref(seen);
        g_free(root);
    }
    else j->index = insight_index_ref(wb.index);
    j->path = g_strdup(path);
    symbol_list_launch(store, j);
}
