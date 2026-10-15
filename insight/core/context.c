/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "context.h"
#include "document.h"
#include "identifier.h"
int insight_word_at(const char *s,size_t length,size_t cursor,size_t *start,size_t *end) {
 if(start) *start=0;
 if(end) *end=0;
 if(!s || !start || !end || cursor>length || !length) return 0;
 size_t at=cursor;uint32_t code=0;
 if(at<length) while(at && ((unsigned char)s[at]&0xc0)==0x80) at--;
 size_t width=insight_identifier_decode(s,length,at,&code);
 if(!width || !(insight_identifier_flags(code)&2)) {
  if(!at) return 0;
  size_t previous=insight_identifier_previous(s,at);
  width=insight_identifier_decode(s,length,previous,&code);
  if(!width || previous+width!=at || !(insight_identifier_flags(code)&2)) return 0;
  at=previous;
 }
 size_t a=at,b=at+width;
 while(a) { size_t previous=insight_identifier_previous(s,a);width=insight_identifier_decode(s,length,previous,&code);if(!width || previous+width!=a || !(insight_identifier_flags(code)&2)) break;a=previous; }
 while(b<length) { width=insight_identifier_decode(s,length,b,&code);if(!width || !(insight_identifier_flags(code)&2)) break;b+=width; }
 if(!insight_identifier_decode(s,length,a,&code) || !(insight_identifier_flags(code)&1)) return 0;
 *start=a;*end=b;return 1;
}

int insight_context_range(const char *s,size_t length,unsigned long target,unsigned int radius,
 size_t *start,size_t *end,unsigned long *first,size_t *target_start,size_t *target_end) {
 if(start) *start=0;
 if(end) *end=0;
 if(first) *first=0;
 if(target_start) *target_start=0;
 if(target_end) *target_end=0;
 if(!s || !start || !end || !first || !target_start || !target_end || !target ||
  length>2u*1024u*1024u || target-1>length || radius>100 || !insight_document_validate_utf8(s,length)) return 0;
 unsigned long begin=target>radius ? target-radius : 1,last=target+radius,line=1;
 size_t offset=0,range_start=0,range_end=0,focus_start=0,focus_end=0;int found=0;
 for(;;) {
  size_t stop=offset;
  while(stop<length && s[stop]!='\n' && s[stop]!='\r') stop++;
  size_t next=stop;
  if(next<length) { char c=s[next++];if(c=='\r' && next<length && s[next]=='\n') next++; }
  if(line==begin) range_start=offset;
  if(line==target) { focus_start=offset;focus_end=stop;found=1; }
  if(line>=last || stop==length) { range_end=next;break; }
  offset=next;line++;
 }
 if(!found) return 0;
 *start=range_start;*end=range_end;*first=begin;*target_start=focus_start;*target_end=focus_end;return 1;
}
