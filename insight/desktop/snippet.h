/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SNIPPET_H
#define INSIGHT_SNIPPET_H
#include <glib.h>
/* Owns unique variable names in first-use order, excluding cursor. $$ is a
 * literal dollar. Names use ASCII identifiers; non-variable dollars are literal. */
GPtrArray *insight_snippet_variables(const gchar *,GError **);
/* Non-recursive substitution. First $cursor$ sets a byte offset; default is
 * the end. Output is normalized to the supplied LF, CRLF or CR line ending. */
typedef struct { gsize start,end; } InsightSnippetField;
/* Optional editable-name set produces ordered UTF-8 byte ranges after EOL
 * normalization, including empty/repeated fields, capped at 1024 ranges.
 * Outputs are published only on success; caller owns text and GArray. */
gchar *insight_snippet_expand_fields(const gchar *,GHashTable *,const gchar *,GHashTable *,gsize *,GArray **,GError **);
gchar *insight_snippet_expand(const gchar *,GHashTable *,const gchar *,gsize *,GError **);
#endif
