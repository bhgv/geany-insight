/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "file_patterns.h"
#include <string.h>
typedef struct { GPatternSpec *spec;gint stage; } Pattern;
struct InsightFilePatterns { gchar **tokens;GArray *compiled; };
static gchar *fold(const gchar *text) {
#ifdef G_OS_WIN32
 return g_utf8_casefold(text,-1);
#else
 return g_strdup(text);
#endif
}
void insight_file_patterns_free(InsightFilePatterns *patterns) { if(patterns) { for(guint n=0;n<patterns->compiled->len;n++) g_pattern_spec_free(g_array_index(patterns->compiled,Pattern,n).spec);g_array_unref(patterns->compiled);g_strfreev(patterns->tokens);g_free(patterns); } }
InsightFilePatterns *insight_file_patterns_new(const gchar *text,GError **error) {
 if(!text || strlen(text)>4096 || !g_utf8_validate(text,-1,NULL)) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"File filters must be UTF-8 text up to 4096 bytes");return NULL; }
 InsightFilePatterns *patterns=g_new0(InsightFilePatterns,1);patterns->compiled=g_array_new(FALSE,FALSE,sizeof(Pattern));GPtrArray *tokens=g_ptr_array_new_with_free_func(g_free);gchar **split=g_strsplit_set(text,";, \t\r\n",-1);gboolean valid=TRUE;
 for(guint n=0;split[n];n++) { const gchar *token=split[n];if(!*token) continue;
  if(tokens->len==256 || strlen(token)>255 || strpbrk(token,"/\\:\"")) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Use up to 256 filename filters, without paths or quotes (255 bytes each)");valid=FALSE;break; }
  gchar *name=fold(token);Pattern pattern={g_pattern_spec_new(name),!strcmp(token,"*") ? INSIGHT_FILE_PATTERN_DEFAULT : (strpbrk(token,"*?") ? INSIGHT_FILE_PATTERN_GLOB : INSIGHT_FILE_PATTERN_EXACT)};g_array_append_val(patterns->compiled,pattern);g_ptr_array_add(tokens,g_strdup(token));g_free(name);
 }
 g_strfreev(split);g_ptr_array_add(tokens,NULL);patterns->tokens=(gchar **)g_ptr_array_free(tokens,FALSE);if(!valid) { insight_file_patterns_free(patterns);return NULL; }return patterns;
}
gboolean insight_file_patterns_match(const InsightFilePatterns *patterns,const gchar *path,gint stage) {
 if(!patterns || !path || !g_utf8_validate(path,-1,NULL)) return FALSE;
 gchar *base=g_path_get_basename(path),*name=fold(base);gboolean found=FALSE;
 for(guint n=0;n<patterns->compiled->len && !found;n++) { const Pattern *p=&g_array_index(patterns->compiled,Pattern,n);if(stage==INSIGHT_FILE_PATTERN_ANY || p->stage==stage) found=g_pattern_spec_match_string(p->spec,name); }
 g_free(base);g_free(name);return found;
}
gchar **insight_file_patterns_tokens(const InsightFilePatterns *patterns) { return patterns ? g_strdupv(patterns->tokens) : NULL; }
