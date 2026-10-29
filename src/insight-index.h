/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_INDEX_H
#define GEANY_INSIGHT_INDEX_H
#include <glib.h>
#include "../insight/desktop/clang_backend.h"
#include "tagmanager/tm_source_file.h"
#include "tagmanager/tm_tag.h"
#include "../insight/core/index.h"
#include "../insight/desktop/type_resolve.h"
/* Copy native tag data into the owning UTF-8 index. NULL/empty tags clear path.
 * Reject >250k total symbols without changing the previous file snapshot. */
TMSourceFile *insight_native_clang_tags(const InsightClangResult *,const gchar *native_path,const gchar *language,TMParserType lang);
gboolean insight_native_import(InsightIndex *,TMSourceFile *,const char *);
/* Separate display database from native parser declarations, with exact tag types.
 * Parameters and undefined parser internals stay solely in the navigation index. */
gboolean insight_native_outline_import(InsightIndex *,TMSourceFile *,const char *);
/* Parse validated UTF-8 source and import produced tags. The legacy parser
 * return value is not a reliable success indicator for nonempty buffers. */
gboolean insight_native_parse(InsightIndex *,TMSourceFile *,const char *,const char *,gsize);
/* Local/parameter tags are eligible only in the same file and function,
 * at or before the reference. Negative means outside the reference scope. */
gboolean insight_native_language_compatible(TMParserType,TMParserType);
gint insight_native_definition_rank_at(const InsightSymbol *,const char *,const char *,gulong);
gchar *insight_native_function_scope(const InsightIndex *,const char *,gulong);
/* Nominal receiver navigation: indexed aliases/inheritance/fields, this and
 * qualified names. C-family call results require every visible candidate to
 * resolve to the same owner; argument types and overloads are not selected.
 * Macros, callable objects, indexing and exceeded bounds decline. Template
 * substitution and dependent-type semantics are not implemented.
 * member suppresses broad name lookup for explicit member syntax, masked
 * non-code and invalid lexical source. Call text must start at source byte zero. */
gchar *insight_native_member_owner(const InsightIndex *,const char *,const char *,gulong,const char *,gsize,gboolean *);
gchar *insight_native_member_owner_filtered(const InsightIndex *,const char *,const char *,gulong,const char *,gsize,gboolean *,InsightTypeEligible,void *);
/* Call sites with a captured non-C-family parser disable C-family calls while
 * retaining nominal field/qualified-name navigation. No extension guessing. */
/* Full lexical source from byte zero. mask, when supplied, has identical byte
 * offsets/length and replaces C-family comments/strings with spaces. Borrowed
 * for the duration of this call. A missing mask is computed lazily once.
 * python is an optional borrowed declaration cache built for exactly this
 * source and path. The cache retains its index snapshot and rejects changed
 * index generations or source identities. Source bytes/mask must stay immutable
 * until free; rebuild on edits. Eligibility is evaluated for each query. */
typedef struct InsightNativePythonBindings InsightNativePythonBindings;
typedef struct { const gchar *text,*mask;gsize length;InsightNativePythonBindings *python; } InsightNativeSource;
/* C-family scope cache for distinct functions sharing a parser line. The
 * immutable index is retained; source bytes/mask are borrowed until free.
 * Unambiguous lines preserve legacy scope lookup. Ambiguous groups use bounded
 * complete brace ranges, declining unsupported or conflicting declarations.
 * An uncertain result must suppress outer/global fallback at the call site.
 * Cache instances are private to a caller/thread and borrow source until free. */
typedef struct InsightNativeScopes InsightNativeScopes;
InsightNativeScopes *insight_native_scopes_new(InsightIndex *,const gchar *,const InsightNativeSource *);
gchar *insight_native_scopes_at(InsightNativeScopes *,gulong,gsize,gboolean *);
void insight_native_scopes_free(InsightNativeScopes *);

gchar *insight_native_member_owner_source_filtered(const InsightIndex *,const char *,const char *,gulong,const InsightNativeSource *,gsize,gboolean *,gboolean,InsightTypeEligible,void *);
gchar *insight_native_member_owner_calls_filtered(const InsightIndex *,const char *,const char *,gulong,const char *,gsize,gboolean *,gboolean,InsightTypeEligible,void *);
gboolean insight_native_member_matches(const InsightSymbol *,const char *);
/* Python navigation uses completed indentation suites and lexical visibility.
 * Dynamic receivers/import inference is deliberately not guessed. */
gchar *insight_native_python_scope(const InsightIndex *,const gchar *,const InsightNativeSource *,gsize);
/* Source-aware lexical binding, including global/nonlocal. NULL declines
 * invalid/excessive declarations; an allocated empty string means module scope. */
InsightNativePythonBindings *insight_native_python_bindings_new(const InsightIndex *,const gchar *,const InsightNativeSource *);
void insight_native_python_bindings_free(InsightNativePythonBindings *);
gchar *insight_native_python_binding_scope(const InsightIndex *,const gchar *,const gchar *,const gchar *,const InsightNativeSource *);
/* Resolve explicit source imports only to one indexed module path. imported
 * suppresses ordinary fallback even when that module is absent or ambiguous. */
gsize insight_native_python_import_query(const InsightIndex *,const gchar *,const gchar *,const InsightNativeSource *,gsize,const gchar *,InsightTypeEligible,void *,const InsightSymbol **,gsize,gboolean *);
gint insight_native_python_rank(const InsightIndex *,const InsightSymbol *,const gchar *,const gchar *,gulong);
gchar *insight_native_python_owner(const InsightIndex *,const gchar *,const gchar *,gulong,const InsightNativeSource *,gsize,gboolean *,InsightTypeEligible,void *);
guint insight_native_definition_rank(const InsightSymbol *,const char *);
#endif
