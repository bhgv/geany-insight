/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "occurrences.h"
#include "document.h"
#include <string.h>
static int word(unsigned char c) {
 return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
  (c>='0' && c<='9') || c=='_' || c>=128;
}
int insight_occurrences_find(const char *s,size_t length,const char *name,size_t n,
 size_t offset,InsightOccurrence *out,size_t capacity,size_t *total) {
 if(total) *total=0;
 if(!total || !s || !name || !n || n>1024 || length>INSIGHT_DOCUMENT_LIMIT ||
  (capacity && !out) || !insight_document_validate_utf8(s,length) ||
  !insight_document_validate_utf8(name,n) || (name[0]>='0' && name[0]<='9')) return 0;
 for(size_t i=0;i<n;i++) if(!word((unsigned char)name[i])) return 0;
 size_t line=1,column=0,shown=0;
 for(size_t i=0;i<length;i++) {
  if((!i || !word((unsigned char)s[i-1])) && n<=length-i &&
   (i+n==length || !word((unsigned char)s[i+n])) && !memcmp(s+i,name,n)) {
   if(*total>=offset && shown<capacity)
    out[shown++]=(InsightOccurrence){i,i+n,(unsigned long)line,(unsigned long)column};
   (*total)++;
  }
  if(s[i]=='\r' || (s[i]=='\n' && (!i || s[i-1]!='\r'))) { line++; column=0; }
  else if(s[i]!='\n') column++;
 }
 return 1;
}
