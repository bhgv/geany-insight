/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "repeat_typing.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void check(const char *text,size_t anchor,size_t caret,const char *typed,unsigned count,const char *replacement,size_t first,size_t last,int changed)
{
    InsightRepeatPlan plan={0};assert(!insight_repeat_plan(text,anchor,caret,typed,count,&plan));assert(!strcmp(plan.replacement,replacement));assert(plan.length==strlen(replacement));assert(plan.begin==first && plan.end==last && plan.caret==first+plan.length);assert(!!plan.changed==!!changed);insight_repeat_clear(&plan);assert(!plan.replacement && !plan.length);
}
int main(void)
{
    check("abcdef",2,4,"Ж",3,"ЖЖЖ",2,4,1);check("abcdef",4,2,"x",2,"xx",2,4,1);
    check("",0,0,"\t",1,"\t",0,0,1);check("a\r\nb",1,3,"z",1,"z",1,3,1);
    check("αxyz",5,2,"xyz",1,"xyz",2,5,0);check("abc",1,1,"abc",1,"abc",1,1,1);
    InsightRepeatPlan plan={0};assert(insight_repeat_plan("α",1,1,"x",1,&plan)==-4);
    assert(insight_repeat_plan("a\r\nb",2,2,"x",1,&plan)==-4);assert(insight_repeat_plan("",0,0,"",1,&plan)==-4);
    assert(insight_repeat_plan("",0,1,"x",1,&plan)==-4);assert(insight_repeat_plan("",0,0,"a\nb",1,&plan)==-4);
    assert(insight_repeat_plan("",0,0,"\1",1,&plan)==-4);assert(insight_repeat_plan("",0,0,"x",0,&plan)==-2);assert(insight_repeat_plan("",0,0,"x",101,&plan)==-2);
    char *payload=malloc(4098);memset(payload,'x',4097);payload[4097]=0;assert(insight_repeat_plan("",0,0,payload,1,&plan)==-2);payload[4096]=0;assert(!insight_repeat_plan("",0,0,payload,100,&plan));assert(plan.length==409600);insight_repeat_clear(&plan);free(payload);
    char *large=malloc(INSIGHT_REPEAT_DOCUMENT+2);memset(large,'x',INSIGHT_REPEAT_DOCUMENT+1);large[INSIGHT_REPEAT_DOCUMENT+1]=0;assert(insight_repeat_plan(large,0,0,"a",1,&plan)==-2);large[INSIGHT_REPEAT_DOCUMENT]=0;assert(insight_repeat_plan(large,0,0,"a",1,&plan)==-2);assert(!insight_repeat_plan(large,0,1,"a",1,&plan));insight_repeat_clear(&plan);free(large);
    for(size_t n=0;n<100;n++) for(unsigned count=1;count<=100;count++) { char text[101];memset(text,'x',n);text[n]=0;assert(!insight_repeat_plan(text,n/3,n/2,"α\t",count,&plan));assert(plan.length==3*count && plan.begin==n/3 && plan.end==n/2);insight_repeat_clear(&plan); }
    puts("Repeat typing plans: exact UTF-8/tab/reverse replacements, no-op, CRLF boundaries, controls and size/count properties passed");return 0;
}
