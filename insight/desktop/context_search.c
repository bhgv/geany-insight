/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "context_search.h"
#include <string.h>
gboolean insight_context_find(const char *text,gsize size,const char *query,gsize from,gboolean forward,guint flags,gsize *begin,gsize *end,gboolean *wrapped,GError **error) {
 if(!text || !query || !begin || !end || !wrapped || size>2u*1024u*1024u || from>size || flags>7 || !g_utf8_validate(text,(gssize)size,NULL) || memchr(text,0,size) || !*query || strlen(query)>1024 || !g_utf8_validate(query,-1,NULL)) { g_set_error_literal(error,G_REGEX_ERROR,G_REGEX_ERROR_COMPILE,"Invalid source or search query"); return FALSE; }
 gchar *pattern=flags&INSIGHT_SEARCH_REGEX ? g_strdup(query) : g_regex_escape_string(query,-1);
 if(flags&INSIGHT_SEARCH_WORD) { gchar *bounded=g_strconcat("(?<![\\p{L}\\p{N}\\p{M}_])(?:",pattern,")(?![\\p{L}\\p{N}\\p{M}_])",NULL); g_free(pattern); pattern=bounded; }
 GRegex *regex=g_regex_new(pattern,G_REGEX_MULTILINE|((flags&INSIGHT_SEARCH_CASE) ? 0 : G_REGEX_CASELESS),0,error); g_free(pattern); if(!regex) return FALSE;
 GMatchInfo *matches=NULL; gboolean any=FALSE,selected=FALSE; gint first=0,first_end=0,last=0,last_end=0,chosen=0,chosen_end=0;
 g_regex_match_full(regex,text,(gssize)size,0,0,&matches,error);
 while(matches && g_match_info_matches(matches)) {
  gint a,b; g_match_info_fetch_pos(matches,0,&a,&b);
  if(!any) { first=a; first_end=b; } any=TRUE; last=a; last_end=b;
  if(forward && (gsize)a>=from && !selected) { chosen=a; chosen_end=b; selected=TRUE; break; }
  if(!forward && (gsize)b<=from) { chosen=a; chosen_end=b; selected=TRUE; }
  if(!g_match_info_next(matches,error)) break;
 }
 if(matches) g_match_info_free(matches);
 g_regex_unref(regex);
 if(error && *error) return FALSE;
 if(!any) return FALSE;
 *wrapped=!selected; if(!selected) { chosen=forward ? first : last; chosen_end=forward ? first_end : last_end; }
 *begin=(gsize)chosen; *end=(gsize)chosen_end; return TRUE;
}
