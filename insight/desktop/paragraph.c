/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "paragraph.h"
#include <string.h>
#define LIMIT (8u*1024u*1024u)
typedef struct { gint start,content,end;gboolean blank; } Line;
static gboolean space(gchar c) { return c==' ' || c=='\t'; }
static gint width(const gchar *s,gsize length,gint column,gint tab) {
 const gchar *end=s+length;
 while(s<end) { gunichar c=g_utf8_get_char(s);if(c=='\t') column+=tab-column%tab;else if(!g_unichar_ismark(c) && c!=0x200d && c!=0xfe0f) column+=g_unichar_iswide(c) ? 2 : 1;s=g_utf8_next_char(s); }
 return column;
}
static guint at(GArray *lines,gint pos) {
 /* Upper bound keeps scope computation logarithmic even for large files. */
 guint lo=0,hi=lines->len;
 while(lo<hi) { guint mid=lo+(hi-lo)/2;if(g_array_index(lines,Line,mid).start<=pos) lo=mid+1;else hi=mid; }
 return lo ? lo-1 : 0;
}
gchar *insight_paragraph(const gchar *text,gint anchor,gint caret,gboolean join,gint margin,gint tab,const gchar *eol,gint *start,gint *end,GError **error) {
 if(!text || strlen(text)>LIMIT || !g_utf8_validate(text,-1,NULL) || margin<1 || margin>1000 || tab<1 || tab>32 || !eol || (strcmp(eol,"\n") && strcmp(eol,"\r") && strcmp(eol,"\r\n"))) goto invalid;
 gint length=strlen(text);
 if(anchor<0 || caret<0 || anchor>length || caret>length || !g_utf8_validate(text,anchor,NULL) || !g_utf8_validate(text,caret,NULL)) goto invalid;
 GArray *lines=g_array_new(FALSE,FALSE,sizeof(Line));gint p=0;
 do {
  Line line={p,p,p,TRUE};while(p<length && text[p]!='\r' && text[p]!='\n') { if(!space(text[p])) line.blank=FALSE;p++; }
  line.content=p;if(p<length) { if(text[p]=='\r' && p+1<length && text[p+1]=='\n') p+=2;else p++; }line.end=p;g_array_append_val(lines,line);
  if(lines->len>=500000 && p<length) { g_array_unref(lines);g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Paragraph tools support at most 500000 lines");return NULL; }
 } while(p<length);
 if(length && (text[length-1]=='\r' || text[length-1]=='\n')) { Line last={length,length,length,TRUE};g_array_append_val(lines,last); }
 gint low=MIN(anchor,caret),high=MAX(anchor,caret);guint first=at(lines,low),last=at(lines,high);
 if(high>low && g_array_index(lines,Line,last).start==high) last--;
 if(join) { if(anchor==caret && last+1<lines->len && g_array_index(lines,Line,last+1).start<length) last++; }
 else {
  if(!g_array_index(lines,Line,first).blank) while(first && !g_array_index(lines,Line,first-1).blank) first--;
  if(!g_array_index(lines,Line,last).blank) while(last+1<lines->len && !g_array_index(lines,Line,last+1).blank) last++;
 }
 *start=g_array_index(lines,Line,first).start;*end=g_array_index(lines,Line,last).end;
 GString *out=g_string_new(NULL);guint i=first;
 if(join) {
  for(guint k=first;k<=last;k++) {
   Line line=g_array_index(lines,Line,k);gint a=line.start,b=line.content;
   if(k>first) while(a<b && space(text[a])) a++;
   if(k<last) while(b>a && space(text[b-1])) b--;
   if(k>first && a<b && out->len && !space(out->str[out->len-1])) g_string_append_c(out,' ');
   g_string_append_len(out,text+a,b-a);
  }
  Line tail=g_array_index(lines,Line,last);g_string_append_len(out,text+tail.content,tail.end-tail.content);
  g_array_unref(lines);return g_string_free(out,FALSE);
 }
 while(i<=last) {
  Line line=g_array_index(lines,Line,i);
  if(line.blank) { g_string_append_len(out,text+line.start,line.end-line.start);i++;continue; }
  guint finish=i;while(finish<last && !g_array_index(lines,Line,finish+1).blank) finish++;
  gint indent=line.start;while(indent<line.content && space(text[indent])) indent++;
  g_string_append_len(out,text+line.start,indent-line.start);gint indent_width=width(text+line.start,indent-line.start,0,tab),column=indent_width;gboolean has_word=FALSE;
  /* Prefer this paragraph's first EOL, preserving mixed file conventions. */
  gchar separator[3]={0};if(line.end>line.content) memcpy(separator,text+line.content,line.end-line.content);else g_strlcpy(separator,eol,sizeof(separator));
  for(guint k=i;k<=finish;k++) {
   Line part=g_array_index(lines,Line,k);gint q=part.start;
   while(q<part.content) {
    while(q<part.content && space(text[q])) q++;
    gint word=q;
    while(q<part.content && !space(text[q])) q++;
    if(q==word) break;
    gint word_width=width(text+word,q-word,0,tab);
    if(has_word && column+1+word_width>margin) { g_string_append(out,separator);g_string_append_len(out,text+line.start,indent-line.start);column=indent_width;has_word=FALSE; }
    if(has_word) { g_string_append_c(out,' ');column++; }
    g_string_append_len(out,text+word,q-word);column+=word_width;has_word=TRUE;
    if(out->len>LIMIT) goto too_large;
   }
  }
  Line tail=g_array_index(lines,Line,finish);g_string_append_len(out,text+tail.content,tail.end-tail.content);i=finish+1;
  if(out->len>LIMIT) goto too_large;
 }
 if(out->len>LIMIT) goto too_large;
 g_array_unref(lines);return g_string_free(out,FALSE);
too_large:
 g_array_unref(lines);g_string_free(out,TRUE);g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Paragraph result exceeds 8 MiB");return NULL;
invalid:
 g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Paragraph tools require UTF-8 up to 8 MiB, a margin of 1–1000 and tab width of 1–32");return NULL;
}
