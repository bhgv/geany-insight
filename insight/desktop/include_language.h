/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Infer ambiguous include languages from literal include edges.
 * RU: Определяет язык неоднозначных подключений по буквальным связям include. */
#ifndef INSIGHT_INCLUDE_LANGUAGE_H
#define INSIGHT_INCLUDE_LANGUAGE_H
#include <glib.h>
typedef struct InsightIncludeGraph InsightIncludeGraph;
gboolean insight_include_graph_candidate(const gchar *path);
InsightIncludeGraph *insight_include_graph_new(GPtrArray *paths, const gchar *root);
void insight_include_graph_add(InsightIncludeGraph *graph, const gchar *path, const gchar *text);
GHashTable *insight_include_graph_resolve(InsightIncludeGraph *graph);
void insight_include_graph_free(InsightIncludeGraph *graph);
#endif
