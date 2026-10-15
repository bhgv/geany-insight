/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "source_mask.h"
#include <string.h>
static void erase(char *out,gsize start,gsize end) {
 for(gsize i=start;i<end;i++) if(out[i]!='\r' && out[i]!='\n') out[i]=' ';
}
static gsize lua_long(const char *text,gsize size,gsize start) {
 if(start>=size || text[start]!='[') return start;
 gsize i=start+1; while(i<size && text[i]=='=') i++;
 if(i>=size || text[i]!='[') return start;
 gsize equals=i-start-1;
 for(i++;i<size;i++) if(text[i]==']') {
  gsize j=i+1; while(j<size && text[j]=='=') j++;
  if(j-i-1==equals && j<size && text[j]==']') return j+1;
 }
 return size;
}
static gsize raw_string(const char *text,gsize size,gsize start) {
 if(start+2>=size || text[start]!='R' || text[start+1]!='"') return start;
 gsize body=start+2;
 while(body<size && body-start-2<=16 && text[body]!='(') {
  if(g_ascii_isspace(text[body]) || text[body]=='\\' || text[body]==')') return start;
  body++;
 }
 if(body>=size || text[body]!='(' || body-start-2>16) return start;
 gsize delimiter=body-start-2;
 for(gsize i=body+1;i<size;i++) if(text[i]==')' && i+delimiter+1<size && !memcmp(text+i+1,text+start+2,delimiter) && text[i+delimiter+1]=='"') return i+delimiter+2;
 return size;
}
gchar *insight_source_region_mask(const char *text,gsize size,InsightCodeDialect dialect,InsightSourceRegion region,GError **error) {
 if(!text || size>2u*1024u*1024u || memchr(text,0,size) || !g_utf8_validate(text,(gssize)size,NULL) || dialect<INSIGHT_CODE_C || dialect>INSIGHT_CODE_SHELL || region<INSIGHT_REGION_ALL || region>INSIGHT_REGION_COMMENTS) {
  g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Invalid source or unsupported lexical dialect"); return NULL;
 }
 gchar *out=g_strndup(text,size);if(region==INSIGHT_REGION_ALL) return out;if(region==INSIGHT_REGION_COMMENTS) erase(out,0,size);
 for(gsize i=0;i<size;) {
  gsize start=i,end=i;gboolean comment=FALSE;
  /* C++ numeric separators are not character literal delimiters. */
  if(dialect==INSIGHT_CODE_C && text[i]=='\'' && i>0 && i+1<size && g_ascii_isxdigit(text[i-1]) && g_ascii_isxdigit(text[i+1])) { i++; continue; }
  if(dialect==INSIGHT_CODE_C && text[i]=='R') end=raw_string(text,size,i);
  if(end==i && dialect==INSIGHT_CODE_LUA && text[i]=='[') end=lua_long(text,size,i);
  gboolean slash=(dialect==INSIGHT_CODE_C || dialect==INSIGHT_CODE_JS) && i+1<size && text[i]=='/';
  gboolean lua=dialect==INSIGHT_CODE_LUA && i+1<size && text[i]=='-' && text[i+1]=='-';
  gboolean hash=(dialect==INSIGHT_CODE_PYTHON || dialect==INSIGHT_CODE_SHELL) && text[i]=='#';
  if(end==i && slash && text[i+1]=='*') { comment=TRUE;end=i+2; while(end+1<size && !(text[end]=='*' && text[end+1]=='/')) end++; end=end+1<size ? end+2 : size; }
  else if(end==i && (hash || lua || (slash && text[i+1]=='/'))) {
   comment=TRUE;
   if(lua) end=lua_long(text,size,i+2);
   if(!lua || end==i+2) {
    end=i;
    while(end<size) {
     if(text[end]=='\n' || text[end]=='\r') {
      if(dialect==INSIGHT_CODE_C && slash && text[i+1]=='/' && end>i && text[end-1]=='\\') { if(text[end]=='\r' && end+1<size && text[end+1]=='\n') end++; end++; continue; }
      break;
     }
     end++;
    }
   }
  }
  else if(end==i && (text[i]=='\'' || text[i]=='"' || (dialect==INSIGHT_CODE_JS && text[i]=='`'))) {
   char quote=text[i]; gboolean triple=dialect==INSIGHT_CODE_PYTHON && i+2<size && text[i+1]==quote && text[i+2]==quote;
   gsize width=triple ? 3 : 1; end=i+width;
   while(end<size) {
    if(text[end]=='\\') { end=MIN(size,end+2); continue; }
    if(text[end]==quote && (!triple || (end+2<size && text[end+1]==quote && text[end+2]==quote))) { end+=width; break; }
    end++;
   }
  }
  if(end>start) { if(region==INSIGHT_REGION_COMMENTS) { if(comment) memcpy(out+start,text+start,end-start); }else if(comment || region==INSIGHT_REGION_CODE) erase(out,start,end); i=end; } else i++;
 }
 return out;
}

gchar *insight_source_code_mask(const char *text,gsize size,InsightCodeDialect dialect,GError **error) { return insight_source_region_mask(text,size,dialect,INSIGHT_REGION_CODE,error); }
