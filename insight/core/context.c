/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "context.h"
static int word(unsigned char c) { return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c>=128; }
int insight_word_at(const char *s,size_t length,size_t cursor,size_t *start,size_t *end) {
 if(start) *start=0;
 if(end) *end=0;
 if(!s || !start || !end || cursor>length || !length) return 0;
 size_t at=cursor;
 if(at==length || !word((unsigned char)s[at])) { if(!at || !word((unsigned char)s[at-1])) return 0; at--; }
 size_t a=at,b=at+1;
 while(a && word((unsigned char)s[a-1])) a--;
 while(b<length && word((unsigned char)s[b])) b++;
 if(s[a]>='0' && s[a]<='9') return 0;
 *start=a; *end=b; return 1;
}

int insight_context_range(const char *s,size_t length,unsigned long target,unsigned int radius,
 size_t *start,size_t *end,unsigned long *first,size_t *target_start,size_t *target_end) {
 if(!s || !start || !end || !first || !target_start || !target_end || !target ||
  length>2u*1024u*1024u || target-1>length || radius>100) return 0;
 unsigned long begin=target>radius ? target-radius : 1,last=target+radius,line=1;
 size_t offset=0; int found=0;
 for(;;) {
  size_t stop=offset;
  while(stop<length && s[stop]!='\n' && s[stop]!='\r') stop++;
  size_t next=stop;
  if(next<length) { char c=s[next++]; if(c=='\r' && next<length && s[next]=='\n') next++; }
  if(line==begin) { *start=offset; *first=line; }
  if(line==target) { *target_start=offset; *target_end=stop; found=1; }
  if(line>=last || stop==length) { *end=next; break; }
  offset=next; line++;
 }
 return found;
}
