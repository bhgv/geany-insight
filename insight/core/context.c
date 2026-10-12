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
