/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "occurrences.h"
#include "document.h"
#include <string.h>
#include "identifier.h"
int insight_occurrences_find(const char *s,size_t length,const char *name,size_t n,
 size_t offset,InsightOccurrence *out,size_t capacity,size_t *total) {
 if(total) *total=0;
 if(!total || !s || !name || !n || n>1024 || length>INSIGHT_DOCUMENT_LIMIT ||
  (capacity && !out) || !insight_document_validate_utf8(s,length) ||
  !insight_document_validate_utf8(name,n)) return 0;
 for(size_t i=0;i<n;) { uint32_t code=0;size_t width=insight_identifier_decode(name,n,i,&code);if(!width || !(insight_identifier_flags(code)&(i ? 2 : 1))) return 0;i+=width; }
 size_t line=1,column=0,shown=0;unsigned previous=0;
 for(size_t i=0;i<length;) {
  uint32_t code=0;size_t width=insight_identifier_decode(s,length,i,&code);unsigned flags=insight_identifier_flags(code);
  uint32_t after=0;
  if(!(previous&2) && (flags&1) && n<=length-i && !memcmp(s+i,name,n) &&
   (i+n==length || (insight_identifier_decode(s,length,i+n,&after) && !(insight_identifier_flags(after)&2)))) {
   if(*total>=offset && shown<capacity)
    out[shown++]=(InsightOccurrence){i,i+n,(unsigned long)line,(unsigned long)column};
   (*total)++;
  }
  if(code=='\r' || (code=='\n' && (!i || s[i-1]!='\r'))) { line++;column=0; }
  else if(code!='\n') column+=width;
  previous=flags;i+=width;
 }
 return 1;
}
