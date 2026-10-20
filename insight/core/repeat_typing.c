/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "repeat_typing.h"
#include <stdlib.h>
#include <string.h>
static size_t length_limit(const char *text,size_t limit) { size_t length=0;while(length<=limit && text[length]) length++;return length; }
static int boundary(const char *text,size_t length,size_t pos)
{
    return pos<=length && (pos==length || ((unsigned char)text[pos]&0xc0)!=0x80) &&
        !(pos && pos<length && text[pos-1]=='\r' && text[pos]=='\n');
}
void insight_repeat_clear(InsightRepeatPlan *plan) { free(plan->replacement);memset(plan,0,sizeof *plan); }
int insight_repeat_plan(const char *text,size_t anchor,size_t caret,const char *typed,unsigned count,InsightRepeatPlan *out)
{
    if(!out) return -4;
    memset(out,0,sizeof *out);if(!text || !typed) return -4;
    size_t length=length_limit(text,INSIGHT_REPEAT_DOCUMENT),payload=length_limit(typed,INSIGHT_REPEAT_TEXT);
    if(length>INSIGHT_REPEAT_DOCUMENT || payload>INSIGHT_REPEAT_TEXT || !count || count>INSIGHT_REPEAT_COUNT) return -2;
    if(!payload || !boundary(text,length,anchor) || !boundary(text,length,caret)) return -4;
    for(size_t i=0;i<payload;i++) if(((unsigned char)typed[i]<32 && typed[i]!='\t') || typed[i]==127) return -4;
    size_t begin=anchor<caret ? anchor:caret,end=anchor>caret ? anchor:caret,repeated=payload*count;
    if(repeated>INSIGHT_REPEAT_DOCUMENT-(length-(end-begin))) return -2;
    char *replacement=malloc(repeated+1);if(!replacement) return -1;
    for(unsigned i=0;i<count;i++) memcpy(replacement+i*payload,typed,payload);
    replacement[repeated]=0;out->replacement=replacement;out->length=repeated;out->begin=begin;out->end=end;out->caret=begin+repeated;
    out->changed=end-begin!=repeated || memcmp(text+begin,replacement,repeated);return 0;
}
