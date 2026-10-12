/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "document.h"
#include <stdlib.h>
#include <string.h>
int insight_document_validate_utf8(const char *text,size_t length) {
 if(!text) return 0;
 const unsigned char *s=(const unsigned char *)text;
 for(size_t i=0;i<length;) {
  unsigned char a=s[i++];
  if(a==0) return 0;
  if(a<0x80) continue;
  unsigned need; unsigned code;
  if(a>=0xc2 && a<=0xdf) { need=1; code=a&31; }
  else if(a>=0xe0 && a<=0xef) { need=2; code=a&15; }
  else if(a>=0xf0 && a<=0xf4) { need=3; code=a&7; }
  else return 0;
  if(length-i<need) return 0;
  for(unsigned j=0;j<need;j++) {
   unsigned char b=s[i++]; if((b&0xc0)!=0x80) return 0;
   code=(code<<6)|(b&63);
  }
  if((need==2 && code<0x800) || (need==3 && code<0x10000) ||
    code>0x10ffff || (code>=0xd800 && code<=0xdfff)) return 0;
 }
 return 1;
}
static int boundary(const char *s,size_t length,size_t offset) {
 return offset<=length && (offset==length || (((unsigned char)s[offset]&0xc0)!=0x80));
}
int insight_document_init(InsightDocument *d,const char *s,size_t length,size_t start,size_t end) {
 if(!d || !s || length>INSIGHT_DOCUMENT_LIMIT || start>end ||
  !insight_document_validate_utf8(s,length) || !boundary(s,length,start) || !boundary(s,length,end)) return 0;
 char *copy=malloc(length+1); if(!copy) return 0;
 memcpy(copy,s,length); copy[length]=0;
 d->text=copy; d->length=length; d->start=start; d->end=end; d->selection=NULL;
 return 1;
}
void insight_document_dispose(InsightDocument *d) {
 if(!d) return;
 free(d->text); free(d->selection); memset(d,0,sizeof *d);
}
const char *insight_document_text(void *data) { return data ? ((InsightDocument *)data)->text : NULL; }
const char *insight_document_selection(void *data) {
 InsightDocument *d=data;
 if(!d || !d->text) return NULL;
 size_t length=d->end-d->start; char *copy=malloc(length+1);
 if(!copy) return NULL;
 memcpy(copy,d->text+d->start,length); copy[length]=0;
 free(d->selection); d->selection=copy; return copy;
}
int insight_document_replace(void *data,const char *s) {
 InsightDocument *d=data;
 if(!d || !d->text || !s) return 0;
 size_t length=strlen(s),remaining=d->length-(d->end-d->start);
 if(length>INSIGHT_DOCUMENT_LIMIT || remaining>INSIGHT_DOCUMENT_LIMIT-length || !insight_document_validate_utf8(s,length)) return 0;
 size_t total=remaining+length;
 char *next=malloc(total+1); if(!next) return 0;
 memcpy(next,d->text,d->start); memcpy(next+d->start,s,length);
 memcpy(next+d->start+length,d->text+d->end,d->length-d->end);
 next[total]=0; free(d->text); d->text=next; d->length=total; d->end=d->start+length;
 free(d->selection); d->selection=NULL; return 1;
}
