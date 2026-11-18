/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Resolve difference labels only against owned definition ranges from the compared snapshots.
 * RU: Определяет подписи различий только по собственным диапазонам сравниваемых снимков. */

/* EN: Collect immutable per-side ranges for m after a successful comparison; unsupported snapshots keep NULL.
 * RU: Собирает диапазоны сторон m после успешного сравнения; неподдерживаемые снимки сохраняют NULL. */
static void diff_symbols_collect(Merge *m)
{
    for (guint side = 0; side < 2; side++)
    {
        g_clear_pointer(&m->symbols[side], g_ptr_array_unref);
        if (m->file_layout && symbol_collector && m->sources[side])
            m->symbols[side] = symbol_collector(m->loaded_paths[side], m->sources[side]);
    }
}

/* EN: Return a borrowed smallest unambiguous definition containing [begin,end) in side of m, or NULL.
 * Empty insertions, invalid metadata and cross-definition blocks never inherit a guessed preceding symbol.
 * RU: Возвращает заимствованное имя наименьшего однозначного определения [begin,end) стороны side в m либо NULL.
 * Пустые вставки, неверные данные и межсимвольные блоки не получают имя предыдущего символа по догадке. */
static const gchar *diff_symbols_owner(Merge *m, guint side, gsize begin, gsize end)
{
    if (!m->symbols[side] || begin >= end)
        return NULL;
    gsize size = strlen(m->sources[side]);
    gsize best = G_MAXSIZE;
    const gchar *name = NULL;
    gboolean ambiguous = FALSE;
    for (guint i = 0; i < MIN(m->symbols[side]->len, 4096u); i++)
    {
        InsightDiffSymbol *symbol = m->symbols[side]->pdata[i];
        if (!symbol || !symbol->name || !*symbol->name || !g_utf8_validate(symbol->name, -1, NULL) ||
            symbol->begin >= symbol->end || symbol->end > size ||
            begin < symbol->begin || end > symbol->end)
            continue;
        gsize span = symbol->end - symbol->begin;
        if (span < best)
        {
            best = span;
            name = symbol->name;
            ambiguous = FALSE;
        }
        else if (span == best && g_strcmp0(name, symbol->name))
            ambiguous = TRUE;
    }
    return ambiguous ? NULL : name;
}

/* EN: Return an owned two-version context label for hunk in m, with an explicit unresolved fallback.
 * RU: Возвращает собственную подпись контекста обеих версий hunk в m с явным неразрешённым вариантом. */
static gchar *diff_symbols_hunk(Merge *m, const InsightDiffHunk *hunk)
{
    const gchar *left = diff_symbols_owner(m, 0, hunk->a_begin, hunk->a_end);
    const gchar *right = diff_symbols_owner(m, 1, hunk->b_begin, hunk->b_end);
    if (left && right)
        return !strcmp(left, right) ? g_strdup(left) : g_strdup_printf("%s → %s", left, right);
    if (left)
        return g_strdup_printf(insight_tr("Left: %s"), left);
    if (right)
        return g_strdup_printf(insight_tr("Right: %s"), right);
    return g_strdup(insight_tr("No symbol context"));
}

/* EN: Relabel m's retained hunks after a locale change without reparsing either snapshot.
 * RU: Обновляет подписи сохранённых блоков m при смене языка без повторного разбора снимков. */
static void diff_symbols_refresh(Merge *m)
{
    for (guint i = 0; i < m->diff.count; i++)
    {
        GtkTreeIter row;
        if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(m->list), &row, NULL, i))
            break;
        gchar *label = diff_symbols_hunk(m, &m->diff.hunks[i]);
        gtk_list_store_set(m->list, &row, 5, label, -1);
        g_free(label);
    }
}
