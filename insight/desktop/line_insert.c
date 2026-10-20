/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "line_insert.h"
#include <string.h>
#define LIMIT (8u*1024u*1024u)
static gboolean boundary(const gchar *text,gint position,gint length) {
 return position>=0 && position<=length && g_utf8_validate(text,position,NULL) &&
        !(position>0 && position<length && text[position-1]=='\r' && text[position]=='\n');
}
void insight_line_insert_clear(InsightLineInsert *plan) { if(plan) { g_free(plan->text);*plan=(InsightLineInsert){0}; } }
gboolean insight_line_insert(const gchar *source,gint anchor,gint caret,gint mode,gboolean indent,const gchar *eol,InsightLineInsert *plan,GError **error) {
 if(!plan || !source || strlen(source)>LIMIT || !g_utf8_validate(source,-1,NULL) || mode<0 || mode>2 || !eol || (strcmp(eol,"\n") && strcmp(eol,"\r") && strcmp(eol,"\r\n"))) goto invalid;
 gint length=strlen(source);
 if(!boundary(source,anchor,length) || !boundary(source,caret,length)) goto invalid;
 gint point=mode==2 ? MIN(anchor,caret) : caret,line=point;
 while(line>0 && source[line-1]!='\r' && source[line-1]!='\n') line--;
 gint prefix=line;while(prefix<length && (source[prefix]==' ' || source[prefix]=='\t')) prefix++;
 if(mode==2) prefix=MIN(prefix,point);
 gint spaces=indent ? prefix-line : 0;
 InsightLineInsert out={0};gchar *padding=g_strndup(source+line,spaces);
 if(mode==0) {
  out.start=out.end=line;out.text=g_strconcat(padding,eol,NULL);out.caret=line+spaces;
 } else if(mode==1) {
  gint content=point;while(content<length && source[content]!='\r' && source[content]!='\n') content++;
  gint end=content;if(end<length) { if(source[end]=='\r' && end+1<length && source[end+1]=='\n') end+=2;else end++; }
  out.start=out.end=end;
  if(end>content) { out.text=g_strconcat(padding,eol,NULL);out.caret=end+spaces; }
  else { out.text=g_strconcat(eol,padding,NULL);out.caret=end+strlen(out.text); }
 } else {
  out.start=MIN(anchor,caret);out.end=MAX(anchor,caret);out.text=g_strconcat(eol,padding,NULL);out.caret=out.start;
 }
 g_free(padding);
 if((gsize)length-(out.end-out.start)+strlen(out.text)>LIMIT) { insight_line_insert_clear(&out);g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Line insertion would exceed 8 MiB");return FALSE; }
 *plan=out;return TRUE;
invalid:
 g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Line insertion requires UTF-8 up to 8 MiB and positions outside UTF-8 characters and CRLF pairs");return FALSE;
}
