/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CLANG_BACKEND_H
#define INSIGHT_CLANG_BACKEND_H
#include <gio/gio.h>
#include "../core/index.h"
typedef enum { INSIGHT_CLANG_TYPE,INSIGHT_CLANG_FUNCTION,INSIGHT_CLANG_MEMBER,INSIGHT_CLANG_CONSTANT,INSIGHT_CLANG_VARIABLE,INSIGHT_CLANG_NAMESPACE,INSIGHT_CLANG_MACRO } InsightClangRole;
typedef struct { guint begin,end;InsightClangRole role; } InsightClangToken;
typedef struct { InsightIndex *symbols,*relations;GArray *tokens;gchar *digest,*diagnostics;gboolean errors,limited,compilation_database; } InsightClangResult;
gboolean insight_clang_available(void);
gboolean insight_clang_source(const gchar *);
gboolean insight_clang_dependencies_fresh(InsightIndex *,const gchar *path);
/* Worker-only: UTF-8 sources, including optional named unsaved overlays. No
 * compilation/linking or execution of compilation database command strings. */
InsightClangResult *insight_clang_parse(const gchar *path,const gchar *text,const gchar *root,const gchar *resources,GHashTable *overlays,GCancellable *,GError **);
void insight_clang_result_free(InsightClangResult *);
gboolean insight_clang_import(InsightClangResult *,const gchar *path,InsightIndex *symbols,InsightIndex *relations);
#endif
