/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "restore_lines.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void check(const char *a,const char *b,size_t anchor,size_t caret,const char *result,size_t blocks)
{
 InsightRestorePlan p={0};assert(insight_restore_lines_plan(a,b,anchor,caret,&p)==0);assert(p.blocks==blocks);
 if(blocks) { size_t n=strlen(b)-(p.current_end-p.current_begin)+p.length;char *text=malloc(n+1);assert(text);memcpy(text,b,p.current_begin);memcpy(text+p.current_begin,p.replacement,p.length);strcpy(text+p.current_begin+p.length,b+p.current_end);assert(!strcmp(text,result));free(text); }
 else assert(!p.replacement);
 insight_restore_lines_clear(&p);assert(!p.replacement && !p.blocks);
}
int main(void)
{
 check("one\ntwo\nthree\n","one\nTWO\nthree\n",4,4,"one\ntwo\nthree\n",1);
 check("a\nb\nc\nd\ne\n","A\nb\nc\nD\ne\n",2,4,"A\nb\nc\nD\ne\n",0);
 check("a\nb\nc\nd\ne\n","A\nb\nc\nD\ne\n",0,8,"a\nb\nc\nd\ne\n",2);
 check("a\nb\nc\nd\ne\n","A\nb\nc\nD\ne\n",8,0,"a\nb\nc\nd\ne\n",2);
 check("a\nb\nc\n","a\ninsert\nb\nc\n",2,2,"a\nb\nc\n",1);
 check("a\nb\nc\n","a\nc\n",2,2,"a\nb\nc\n",1);
 check("a\nb\n","a\n",2,2,"a\nb\n",1);
 check("a\r\nβ\r\nlast","a\r\nchanged\r\nlast",4,4,"a\r\nβ\r\nlast",1);
 check("a\rb\rc","a\rB\rc",2,2,"a\rb\rc",1);
 check("abc","ABC",0,3,"abc",1);
 check("","",0,0,"",0);check("original","",0,0,"original",1);check("","new",0,0,"",1);
 check("one\ntwo\n","ONE\ntwo\n",4,4,"ONE\ntwo\n",0);
 check("a\nb\nc\n","a\nB\nC\n",2,3,"a\nb\nc\n",1); /* whole contiguous changed block */
 InsightRestorePlan p={0};assert(insight_restore_lines_plan("a\r\nb","a\r\nb",2,2,&p)==-4);assert(!p.replacement);assert(insight_restore_lines_plan("a","a",2,0,&p)==-4);
 char *large=malloc(INSIGHT_RESTORE_LIMIT+2);assert(large);memset(large,'x',INSIGHT_RESTORE_LIMIT+1);large[INSIGHT_RESTORE_LIMIT+1]=0;assert(insight_restore_lines_plan("",large,0,0,&p)==-2);free(large);
 puts("Restore Lines core: block selection, reverse ranges, insert/delete/EOF, whole blocks, UTF-8/mixed EOL, boundaries and limits passed");return 0;
}
