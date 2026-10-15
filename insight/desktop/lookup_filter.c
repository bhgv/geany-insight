/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "lookup_filter.h"
#include <string.h>
struct InsightLookupFilter { gchar **terms,*exact; GRegex *regex; gboolean files,functions; guint exact_field; };
void insight_lookup_filter_free(InsightLookupFilter *f) { if(!f) return; g_strfreev(f->terms); g_free(f->exact); if(f->regex) g_regex_unref(f->regex); g_free(f); }
InsightLookupFilter *insight_lookup_filter_new(const char *query,gboolean files,GError **error) {
 if(!query || !g_utf8_validate(query,-1,NULL) || strlen(query)>1024) { g_set_error_literal(error,G_REGEX_ERROR,G_REGEX_ERROR_COMPILE,"Search must be valid UTF-8, up to 1024 bytes"); return NULL; }
 InsightLookupFilter *f=g_new0(InsightLookupFilter,1); f->files=files; gchar *text=g_strdup(query); g_strstrip(text); gsize n=strlen(text);
 if(!files && n && text[n-1]=='(') { f->functions=TRUE; text[--n]=0; g_strchomp(text); }
 n=strlen(text);
 if(g_str_has_prefix(text,"file:\"") || g_str_has_prefix(text,"scope:\"")) {
  guint prefix=g_str_has_prefix(text,"file:") ? 6 : 7;f->exact_field=prefix==6 ? 2 : 1;
  if(n<=prefix || text[n-1]!='"') { g_set_error_literal(error,G_REGEX_ERROR,G_REGEX_ERROR_COMPILE,"Close the quoted field filter");g_free(text);insight_lookup_filter_free(f);return NULL; }
  text[n-1]=0;f->exact=g_utf8_casefold(text+prefix,-1);
 }
 else if(text[0]=='?') { f->regex=g_regex_new(text+1,G_REGEX_CASELESS|G_REGEX_OPTIMIZE,0,error); if(!f->regex) { g_free(text); insight_lookup_filter_free(f); return NULL; } }
 else if(n>=2 && text[0]=='"' && text[n-1]=='"') { text[n-1]=0; f->exact=g_utf8_casefold(text+1,-1); }
 else { gchar *folded=g_utf8_casefold(text,-1); f->terms=g_strsplit_set(folded," \t\r\n",-1); g_free(folded); }
 g_free(text); return f;
}
/* Inputs have already been case-folded. Compare Unicode characters, not bytes. */
static gboolean fuzzy_match(const char *hay,const char *term) {
 if(!*term) return FALSE;
 while(*hay && *term) {
  if(g_utf8_get_char(hay)==g_utf8_get_char(term)) term=g_utf8_next_char(term);
  hay=g_utf8_next_char(hay);
 }
 return !*term;
}
gboolean insight_lookup_text_match(const char *text,const char *query) {
 if(!text || !query || !g_utf8_validate(text,-1,NULL) || !g_utf8_validate(query,-1,NULL)) return FALSE;
 gchar *hay=g_utf8_casefold(text,-1),*term=g_utf8_casefold(query,-1);
 gboolean match=term[0]=='~' ? fuzzy_match(hay,term+1) : strstr(hay,term)!=NULL;
 g_free(hay);g_free(term);return match;
}
gboolean insight_lookup_filter_match(const InsightLookupFilter *f,const char *name,const char *scope,const char *path,const char *kind) {
 if(!f) return FALSE;
 if(f->functions && (!kind || !(kind[0]=='f' || kind[0]=='p' || kind[0]=='m') || kind[1])) return FALSE;
 name=name ? name : ""; scope=scope ? scope : ""; path=path ? path : "";
 if(f->regex) return g_regex_match(f->regex,f->files ? path : name,0,NULL);
 gchar *n=g_utf8_casefold(name,-1),*s=g_utf8_casefold(scope,-1),*p=g_utf8_casefold(path,-1); gboolean match=TRUE;
 if(f->exact) match=!strcmp(f->exact_field==1 ? s : (f->exact_field==2 || f->files ? p : n),f->exact);
 else for(guint i=0;f->terms && f->terms[i];i++) {
  const char *term=f->terms[i]; if(!*term) continue;
  const char *hay=f->files ? p : n;
  if(term[0]=='@') { hay=p; term++; }
  else if(g_str_has_prefix(term,"scope=")) { hay=s; term+=6; }
  if(term[0]=='~') { if(!fuzzy_match(hay,term+1)) { match=FALSE; break; } }
  else if(f->files && (strchr(term,'*') || strchr(term,'?'))) { if(!g_pattern_match_simple(term,hay)) { match=FALSE; break; } }
  else if(!strstr(hay,term)) { match=FALSE; break; }
 }
 g_free(n); g_free(s); g_free(p); return match;
}
