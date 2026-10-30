/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CLANG_BACKEND_H
#define INSIGHT_CLANG_BACKEND_H
#include <gio/gio.h>
#include "../core/index.h"
typedef enum { INSIGHT_CLANG_TYPE,INSIGHT_CLANG_FUNCTION,INSIGHT_CLANG_MEMBER,INSIGHT_CLANG_CONSTANT,INSIGHT_CLANG_VARIABLE,INSIGHT_CLANG_NAMESPACE,INSIGHT_CLANG_MACRO } InsightClangRole;
typedef struct { guint begin,end;InsightClangRole role; } InsightClangToken;
enum { INSIGHT_CLANG_INTERNAL=1, INSIGHT_CLANG_PUBLIC=2, INSIGHT_CLANG_PROTECTED=4, INSIGHT_CLANG_PRIVATE=8, INSIGHT_CLANG_VIRTUAL=16, INSIGHT_CLANG_EXTERN=32, INSIGHT_CLANG_FUNCTION_MACRO=64 };
typedef struct { InsightIndex *symbols,*relations;GArray *tokens;GHashTable *attributes;gchar *digest,*diagnostics,*command_source,*parse_language;gboolean errors,limited,compilation_database,inferred_command,ambiguous_command; } InsightClangResult;
gboolean insight_clang_available(void);
gboolean insight_clang_source(const gchar *);
gboolean insight_clang_dependencies_fresh(const InsightIndex *,const gchar *path);
/* Worker-only: UTF-8 sources, including optional named unsaved overlays. No
 * compilation/linking or execution of compilation database command strings. */
InsightClangResult *insight_clang_parse(const gchar *path,const gchar *text,const gchar *root,const gchar *resources,GHashTable *overlays,GCancellable *,GError **);
static inline gchar *insight_clang_attribute_key(const InsightSymbol *s) {return g_strdup_printf("%lu:%s:%s:%s",s->line,s->name,s->scope,s->signature);}
static inline guint insight_clang_attributes(const InsightClangResult *r,const InsightSymbol *s) {if(!r || !r->attributes) return 0;gchar *key=insight_clang_attribute_key(s);guint flags=GPOINTER_TO_UINT(g_hash_table_lookup(r->attributes,key));g_free(key);return flags;}
gchar *insight_clang_symbol_at(InsightIndex *,const gchar *path,guint offset,guint line,const gchar *name);
typedef struct { gchar *name,*signature,*type,*comment;GPtrArray *parameters;guint priority;gboolean callable; } InsightClangCompletion;
/* Owned candidates, compiler-ranked; byte offset into the UTF-8 unsaved source. */
GPtrArray *insight_clang_complete(const gchar *,const gchar *,guint,const gchar *,const gchar *,GHashTable *,GCancellable *,GError **);
void insight_clang_result_free(InsightClangResult *);
gboolean insight_clang_import(InsightClangResult *,const gchar *path,InsightIndex *symbols,InsightIndex *relations);
#endif
