/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "text_boundary.h"
#include <string.h>
static gboolean space(gchar c) { return c==' ' || c=='\t'; }
gboolean insight_text_boundary(const gchar *text,gsize length,gint position,gint mode,gint *target,GError **error) {
 if(!text || !target || length>8u*1024u*1024u || position<0 || (gsize)position>length || mode<0 || mode>2 || memchr(text,0,length) || !g_utf8_validate(text,length,NULL) || !g_utf8_validate(text,position,NULL) || (position>0 && (gsize)position<length && text[position-1]=='\r' && text[position]=='\n')) {
  g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Text boundaries require UTF-8 without NUL up to 8 MiB and a valid character/EOL position");return FALSE;
 }
 gint start=position,end=position;
 if(mode==2) { const gchar *period=memchr(text+position,'.',length-position);*target=period ? (gint)(period-text+1) : (gint)length;return TRUE; }
 while(start>0 && text[start-1]!='\r' && text[start-1]!='\n') start--;
 while((gsize)end<length && text[end]!='\r' && text[end]!='\n') end++;
 if(mode==0) {
  gint content=start;while(content<end && space(text[content])) content++;
  *target=position==start ? 0 : position<=content ? start : content;
 } else {
  gint content=end;while(content>start && space(text[content-1])) content--;
  *target=position==end ? (gint)length : position>=content ? end : content;
 }
 return TRUE;
}
