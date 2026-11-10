/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: HDL result cells and background analysis.
 * RU: Строки результатов HDL и фоновый анализ. */


/* EN: Display field from iter in model using renderer. column is unused.
 * RU: Показывает поле field строки iter в model через renderer. column не используется. */
static void hdl_text_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer field)
{
    (void)column;
    gchar *value = NULL, *path = NULL;
    gulong line = 0;
    gint which = GPOINTER_TO_INT(field);
    gtk_tree_model_get(model, iter, which, &value, USAGE_PATH, &path, USAGE_LINE, &line, -1);
    const gchar *text = value ? value:"";
    if (wb.usage_mode == 12)
    {
        if (which == USAGE_SYMBOL) text = insight_tr(text);
        else if (!path && !line) text = insight_tr("Project diagnostic");
    }
    g_object_set(renderer, "text", text, NULL);
    g_free(value);
    g_free(path);
}

/* EN: Display a source coordinate from field at iter in model. column is unused.
 * RU: Показывает координату field строки iter из model через renderer. column не используется. */
static void hdl_position_cell(GtkTreeViewColumn *column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer field)
{
    (void)column;
    gulong value = 0;
    gtk_tree_model_get(model, iter, GPOINTER_TO_INT(field), &value, -1);
    gchar *text = !value && wb.usage_mode == 12 ? g_strdup("—"):g_strdup_printf("%lu", value);
    g_object_set(renderer, "text", text, NULL);
    g_free(text);
}

/* EN: Release the HDL analysis job in data.
 * RU: Освобождает задачу анализа HDL из data. */
static void hdl_job_free(gpointer data)
{
    HdlJob *j = data;
    if (j->result)
    {
        retire_index(j->result->index);
        j->result->index = NULL;
        insight_slang_project_free(j->result);
    }
    g_ptr_array_unref(j->paths);
    g_ptr_array_unref(j->rows);
    g_array_unref(j->colors);
    g_free(j->paint_path);
    g_hash_table_unref(j->buffers);
    g_free(j->service);
    g_free(j->vivado);
    g_free(j->root);
    g_free(j->cache);
    g_free(j->project);
    g_free(j->query);
    g_free(j);
}
/* Category zero deliberately leaves unsupported facts to the lexer. */

/* EN: Return the highlight role of symbol row.
 * RU: Возвращает роль подсветки для символа row. */
static guint hdl_color_role(const InsightSymbol *row)
{
    if (!strcmp(row->kind, "@hdldef"))
    {
        if (!strcmp(row->type, "Module") || !strcmp(row->type, "Instance"))
            return 1;
        if (!strcmp(row->type, "Port"))
            return 2;
        if (!strcmp(row->type, "Subroutine"))
            return 3;
        if (!strcmp(row->type, "Parameter") || !strcmp(row->type, "EnumValue"))
            return 4;
        if (!strcmp(row->type, "Net") || !strcmp(row->type, "Variable"))
            return 5;
    }
    else if (!strcmp(row->kind, "@hdlref"))
    {
        if (!strcmp(row->inheritance, "module") || !strcmp(row->inheritance, "instance"))
            return 1;
        if (!strcmp(row->inheritance, "port"))
            return 2;
        if (!strcmp(row->inheritance, "call"))
            return 3;
        if (!strcmp(row->inheritance, "value") || !strcmp(row->inheritance, "member") ||
        !strcmp(row->inheritance, "read") || !strcmp(row->inheritance, "write") ||
        !strcmp(row->inheritance, "read/write")) return 5;
    }
    return 0;
}
typedef struct
{
    guint line, start, length, role;
    gboolean declaration, painted;
}
HdlSpan;

/* EN: Check whether c can be part of an HDL identifier.
 * RU: Проверяет, может ли c входить в идентификатор HDL. */
static gboolean hdl_identifier_byte(guchar c)
{
    return g_ascii_isalnum(c) || c == '_' || c == '$' || c >= 128;
}

/* EN: Order highlight spans a and b by source position.
 * RU: Сравнивает диапазоны подсветки a и b по позиции. */
static gint hdl_span_order(gconstpointer a, gconstpointer b)
{
    const HdlSpan *x = a, *y = b;
    if (x->start != y->start)
        return x->start<y->start ? -1:1;
    return (gint)x->declaration-(gint)y->declaration;
}

/* EN: Analyze HDL from data and complete task. cancel stops work. source is unused.
 * RU: Анализирует HDL из data и завершает task. cancel отменяет работу. source не используется. */
static void hdl_job_run(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    HdlJob *j = data;
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, j->buffers);
    while (j->valid && g_hash_table_iter_next(&iter, NULL, &value))
    j->valid = g_utf8_validate(value, -1, NULL);
    if (j->valid) j->result = insight_slang_project_configured(j->service, j->paths, j->buffers, j->root, j->cache, j->vivado, cancel);
    const gchar *text = j->paint_path ? g_hash_table_lookup(j->buffers, j->paint_path):NULL;
    GArray *lines = g_array_new(FALSE, FALSE, sizeof(guint));
    if (text)
    {
        guint offset = 0;
        g_array_append_val(lines, offset);
        for (;text[offset];offset++) if (text[offset] == '\n' || (text[offset] == '\r' && text[offset+1] != '\n'))
        {
            guint next = offset+1;
            g_array_append_val(lines, next);
        }
        g_array_append_val(lines, offset);
    }
    if (j->result) for (gsize i = 0;i<insight_index_count(j->result->index) && !g_cancellable_is_cancelled(cancel);i++)
    {
        const InsightSymbol *row = insight_index_at(j->result->index, i);
        if (!j->colors_only && j->rows->len<2000 && !strcmp(row->kind, j->diagnostics ? "@hdldiag":"@hdlref") && (j->diagnostics || !*j->query || !strcmp(row->name, j->query)))
            g_ptr_array_add(j->rows, (gpointer)row);
        if (!text || !row->line || row->line >= lines->len || strcmp(row->path, j->paint_path)) continue;
        guint role = hdl_color_role(row);
        if (!role) continue;
        guint64 column = g_ascii_strtoull(row->signature, NULL, 10);
        guint start = g_array_index(lines, guint, row->line-1), end = g_array_index(lines, guint, row->line);
        if (!column || column-1 >= end-start) continue;
        guint pos = start+column-1;
        gsize n = strlen(row->name);
        if (!n || n>end-pos || memcmp(text+pos, row->name, n) ||
        (pos>start && (hdl_identifier_byte(text[pos-1]) || text[pos-1] == '\\')) ||
        (pos+n<end && hdl_identifier_byte(text[pos+n]))) continue;
        HdlSpan span =
        {
            .line = row->line-1, .start = pos, .length = n, .role = role, .declaration = !strcmp(row->kind, "@hdldef")
        };
        g_array_append_val(j->colors, span);
    }
    g_array_unref(lines);
    g_array_sort(j->colors, hdl_span_order);
    g_task_return_boolean(task, j->result != NULL);
}
