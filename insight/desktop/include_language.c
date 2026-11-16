/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Propagate C/C++ and HDL ownership through literal include chains.
 * RU: Распространяет принадлежность C/C++ и HDL по цепочкам буквальных include. */
#include "include_language.h"
#include "source_mask.h"
#include "glib_compat.h"
#include <string.h>
enum { FAMILY_C = 1, FAMILY_CPP = 2, FAMILY_HDL = 4 };
typedef struct { gchar *path; guint allowed; } IncludeEdge;
struct InsightIncludeGraph
{
    gchar *root;
    GHashTable *paths, *basenames, *edges, *families;
    GRegex *directive;
    guint edge_count;
    gboolean limited;
};

/* EN: Free edge data and its owned path; no side effects outside the graph.
 * RU: Освобождает ребро data и его путь без изменений вне графа. */
static void edge_free(gpointer data)
{
    IncludeEdge *edge = data;
    g_free(edge->path);
    g_free(edge);
}

/* EN: Return an unambiguous seed family for path, or zero for ambiguous .inc.
 * RU: Возвращает исходное семейство path либо ноль для неоднозначного .inc. */
static guint source_family(const gchar *path)
{
    const gchar *dot = strrchr(path, '.');
    if (!dot) return 0;
    if (!g_ascii_strcasecmp(dot, ".v") || !g_ascii_strcasecmp(dot, ".sv") ||
        !g_ascii_strcasecmp(dot, ".vh") || !g_ascii_strcasecmp(dot, ".svh")) return FAMILY_HDL;
    if (!strcmp(dot, ".C")) return FAMILY_CPP;
    if (!g_ascii_strcasecmp(dot, ".c") || !g_ascii_strcasecmp(dot, ".h")) return FAMILY_C;
    const gchar *native[] = {".C", ".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx"};
    for (guint n = 0; n < G_N_ELEMENTS(native); n++)
        if (!g_ascii_strcasecmp(dot, native[n])) return FAMILY_CPP;
    return 0;
}

/* EN: Return whether path can contain C/C++ or HDL include directives.
 * RU: Сообщает, может ли path содержать подключения C/C++ либо HDL. */
gboolean insight_include_graph_candidate(const gchar *path)
{
    const gchar *dot = path ? strrchr(path, '.') : NULL;
    return path && (source_family(path) || (dot && !g_ascii_strcasecmp(dot, ".inc")));
}

/* EN: Hash project paths using the host filesystem case convention.
 * RU: Хэширует пути value с учётом правил регистра файловой системы платформы. */
static guint path_hash(gconstpointer value)
{
#ifdef G_OS_WIN32
    gchar *folded = g_utf8_casefold(value, -1);
    guint hash = g_str_hash(folded);
    g_free(folded);
    return hash;
#else
    return g_str_hash(value);
#endif
}

/* EN: Compare path keys a/b using host case conventions.
 * RU: Сравнивает ключи путей a/b по правилам регистра платформы. */
static gboolean path_equal(gconstpointer a, gconstpointer b)
{
#ifdef G_OS_WIN32
    gchar *left = g_utf8_casefold(a, -1), *right = g_utf8_casefold(b, -1);
    gboolean equal = !strcmp(left, right);
    g_free(left);
    g_free(right);
    return equal;
#else
    return g_str_equal(a, b);
#endif
}

/* EN: Allocate a graph restricted to project paths under root. No file I/O.
 * The caller frees the graph; duplicate basenames are intentionally unresolved.
 * RU: Создаёт граф только для путей проекта paths с корнем root без чтения файлов.
 * Вызывающий освобождает граф; одинаковые базовые имена считаются неоднозначными. */
InsightIncludeGraph *insight_include_graph_new(GPtrArray *paths, const gchar *root)
{
    InsightIncludeGraph *g = g_new0(InsightIncludeGraph, 1);
    g->root = g_strdup(root);
    g->paths = g_hash_table_new_full(path_hash, path_equal, g_free, NULL);
    g->basenames = g_hash_table_new_full(path_hash, path_equal, g_free, g_free);
    g->edges = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
    g->families = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g->directive = g_regex_new("(?m)^[\\t ]*([#`])[\\t ]*include[\\t ]*[<\"]([^>\"\\r\\n]+)[>\"]", 0, 0, NULL);
    for (guint n = 0; n < paths->len; n++)
    {
        const gchar *path = g_ptr_array_index(paths, n);
        g_hash_table_add(g->paths, g_strdup(path));
        gchar *base = g_path_get_basename(path);
        if (g_hash_table_contains(g->basenames, base))
            g_hash_table_replace(g->basenames, base, g_strdup(""));
        else g_hash_table_insert(g->basenames, base, g_strdup(path));
        guint family = source_family(path);
        if (family) g_hash_table_insert(g->families, g_strdup(path), GUINT_TO_POINTER(family));
    }
    return g;
}

/* EN: Resolve name relative to owner, then root, then a unique project basename.
 * Return an owned member path or NULL; never resolve outside the membership.
 * RU: Разрешает name относительно owner, root, затем уникального имени проекта.
 * Возвращает принадлежащий вызывающему путь либо NULL, только внутри состава проекта. */
static gchar *include_path_normalized(InsightIncludeGraph *g, const gchar *owner, const gchar *name)
{
    gchar *directory = g_path_get_dirname(owner);
    gchar *path = insight_canonicalize_filename(name, directory);
    g_free(directory);
    const gchar *actual = g_hash_table_lookup(g->paths, path);
    if (actual)
    {
        g_free(path);
        return g_strdup(actual);
    }
    g_free(path);
    path = insight_canonicalize_filename(name, g->root);
    actual = g_hash_table_lookup(g->paths, path);
    if (actual)
    {
        g_free(path);
        return g_strdup(actual);
    }
    g_free(path);
    gchar *base = g_path_get_basename(name);
    const gchar *unique = g_hash_table_lookup(g->basenames, base);
    g_free(base);
    if (!unique || !*unique) return NULL;
    /* Preserve an explicit relative directory suffix; never pick a different folder. */
    gchar *suffix = g_strconcat(G_DIR_SEPARATOR_S, name, NULL);
#ifdef G_OS_WIN32
    gchar *folded = g_utf8_casefold(unique, -1), *folded_suffix = g_utf8_casefold(suffix, -1);
    gboolean valid = !strchr(name, G_DIR_SEPARATOR) || g_str_has_suffix(folded, folded_suffix);
    g_free(folded);
    g_free(folded_suffix);
#else
    gboolean valid = !strchr(name, G_DIR_SEPARATOR) || g_str_has_suffix(unique, suffix);
#endif
    g_free(suffix);
    return valid ? g_strdup(unique) : NULL;
}

/* EN: Resolve include name from owner in graph g, normalizing Windows separators.
 * Return an owned member path, or NULL on missing/ambiguous membership.
 * RU: Разрешает name из owner в графе g, нормализуя разделители Windows.
 * Возвращает принадлежащий вызывающему путь либо NULL при отсутствии/неоднозначности. */
static gchar *include_path(InsightIncludeGraph *g, const gchar *owner, const gchar *name)
{
    gchar *normalized = g_strdup(name);
#ifdef G_OS_WIN32
    for (gchar *p = normalized; *p; p++) if (*p == '/') *p = G_DIR_SEPARATOR;
#endif
    gchar *result = include_path_normalized(g, owner, normalized);
    g_free(normalized);
    return result;
}

/* EN: Add literal directives from text belonging to path. Ignore commented
 * directives and macro includes; preserve # versus backtick include families.
 * RU: Добавляет буквальные подключения из text файла path. Пропускает комментарии
 * и макросы; различает семейства #include и подключения с обратной кавычкой. */
void insight_include_graph_add(InsightIncludeGraph *g, const gchar *path, const gchar *text)
{
    if (!text || !g_hash_table_contains(g->paths, path)) return;
    InsightCodeDialect dialect = source_family(path) == FAMILY_HDL ? INSIGHT_CODE_VERILOG : INSIGHT_CODE_C;
    /* Unclassified include fragments can contain HDL numeric apostrophes. */
    if (!source_family(path)) dialect = INSIGHT_CODE_VERILOG;
    gchar *mask = insight_source_region_mask(text, strlen(text), dialect, INSIGHT_REGION_SKIP_COMMENTS, NULL);
    if (!mask) return;
    guchar *code = insight_source_region_membership(text, strlen(text), dialect, INSIGHT_REGION_CODE, NULL);
    GPtrArray *edges = g_ptr_array_new_with_free_func(edge_free);
    GMatchInfo *match = NULL;
    g_regex_match(g->directive, mask, 0, &match);
    while (g_match_info_matches(match))
    {
        gchar *marker = g_match_info_fetch(match, 1);
        gchar *name = g_match_info_fetch(match, 2);
        gint marker_start = 0;
        g_match_info_fetch_pos(match, 1, &marker_start, NULL);
        gchar *target = code && code[marker_start] ? include_path(g, path, name) : NULL;
        if (target && (edges->len >= 4096 || g->edge_count >= 65536))
        {
            g->limited = TRUE;
            g_free(target);
            target = NULL;
        }
        if (target)
        {
            g->edge_count++;
            IncludeEdge *edge = g_new0(IncludeEdge, 1);
            edge->path = target;
            edge->allowed = *marker == '`' ? FAMILY_HDL : FAMILY_C | FAMILY_CPP;
            g_ptr_array_add(edges, edge);
        }
        g_free(marker);
        g_free(name);
        if (!g_match_info_next(match, NULL)) break;
    }
    g_match_info_free(match);
    g_free(code);
    g_free(mask);
    g_hash_table_replace(g->edges, g_strdup(path), edges);
}

/* EN: Resolve ownership and return an owned path->language table. Mixed HDL/native
 * owners yield no automatic assignment. Return NULL on the edge limit; caller
 * unrefs a returned table.
 * RU: Разрешает принадлежность и возвращает таблицу путь->язык. Общие HDL/C-файлы
 * автоматически не назначаются. При лимите рёбер возвращает NULL; вызывающий
 * освобождает полученную таблицу через unref. */
GHashTable *insight_include_graph_resolve(InsightIncludeGraph *g)
{
    if (g->limited) return NULL;
    GQueue queue = G_QUEUE_INIT;
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, g->families);
    while (g_hash_table_iter_next(&iter, &key, NULL)) g_queue_push_tail(&queue, g_strdup(key));
    while (!g_queue_is_empty(&queue))
    {
        const gchar *owner = g_queue_pop_head(&queue);
        guint family = GPOINTER_TO_UINT(g_hash_table_lookup(g->families, owner));
        GPtrArray *edges = g_hash_table_lookup(g->edges, owner);
        for (guint n = 0; edges && n < edges->len; n++)
        {
            IncludeEdge *edge = g_ptr_array_index(edges, n);
            guint before = GPOINTER_TO_UINT(g_hash_table_lookup(g->families, edge->path));
            guint after = before | (family & edge->allowed);
            if (after == before) continue;
            gchar *copy = g_strdup(edge->path);
            g_hash_table_replace(g->families, copy, GUINT_TO_POINTER(after));
            /* Queue owns a separate key so later hash replacements cannot invalidate it. */
            g_queue_push_tail(&queue, g_strdup(copy));
        }
        g_free((gpointer)owner);
    }
    GHashTable *result = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_iter_init(&iter, g->families);
    while (g_hash_table_iter_next(&iter, &key, &value))
    {
        const gchar *path = key, *dot = strrchr(path, '.');
        guint family = GPOINTER_TO_UINT(value);
        if (!dot || g_ascii_strcasecmp(dot, ".inc") ||
            ((family & FAMILY_HDL) && (family & (FAMILY_C | FAMILY_CPP)))) continue;
        const gchar *language = family & FAMILY_HDL ? "SystemVerilog" :
            family & FAMILY_CPP ? "C++" : family & FAMILY_C ? "C" : NULL;
        if (language) g_hash_table_insert(result, g_strdup(path), g_strdup(language));
    }
    return result;
}

/* EN: Release graph g and every owned node/edge; NULL is accepted.
 * RU: Освобождает граф g и все его узлы/рёбра; допускает NULL. */
void insight_include_graph_free(InsightIncludeGraph *g)
{
    if (!g) return;
    g_free(g->root);
    g_hash_table_unref(g->paths);
    g_hash_table_unref(g->basenames);
    g_hash_table_unref(g->edges);
    g_hash_table_unref(g->families);
    g_regex_unref(g->directive);
    g_free(g);
}
