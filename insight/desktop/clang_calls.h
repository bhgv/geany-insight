/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CLANG_CALLS_H
#define INSIGHT_CLANG_CALLS_H
#include "clang_backend.h"
G_BEGIN_DECLS
/* Direct Clang C++ AST adapter. Replaces compiler calls, resolved expression/type references, destinations and
 * explicit function definitions/body spans atomically;
 * owns no AST after returning. Arguments are already sanitized by the frontend.
 * On failure/cancellation, rows and owned strings are unchanged. */
gboolean insight_clang_cpp_calls(const gchar *path,const gchar *text,const gchar *const *args,guint argc,GHashTable *overlays,GArray *rows,GPtrArray *owned,GCancellable *cancel);
InsightClangResult *insight_clang_cpp_parse(const gchar *,const gchar *,const gchar *,const gchar *,GHashTable *,GCancellable *,GError **);
GPtrArray *insight_clang_cpp_complete(const gchar *,const gchar *,guint,const gchar *,const gchar *,GHashTable *,GCancellable *,GError **);
G_END_DECLS
#endif
