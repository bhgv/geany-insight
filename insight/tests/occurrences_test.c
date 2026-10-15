/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "occurrences.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 const char *source="foo foobar foo_bar\r\nfoo\rfoo\nимя имя_x имя";
 InsightOccurrence out[8]; size_t total;
 assert(insight_occurrences_find(source,strlen(source),"foo",3,0,out,8,&total));
 assert(total==3 && out[1].line==2 && out[1].column==0 && out[2].line==3);
 assert(insight_occurrences_find(source,strlen(source),"foo",3,1,out,1,&total));
 assert(total==3 && out[0].line==2);
 assert(insight_occurrences_find(source,strlen(source),"foo",3,SIZE_MAX,NULL,0,&total) && total==3);
 assert(insight_occurrences_find(source,strlen(source),"имя",strlen("имя"),0,out,8,&total));
 assert(total==2 && out[1].line==4 && out[1].column==16);
 assert(insight_occurrences_find("",0,"foo",3,0,NULL,0,&total) && total==0);
 assert(!insight_occurrences_find(source,strlen(source),"123",3,0,out,8,&total) && total==0);
 assert(!insight_occurrences_find(source,strlen(source),"foo bar",7,0,out,8,&total));
 assert(!insight_occurrences_find("a\0b",3,"a",1,0,out,8,&total));
 assert(!insight_occurrences_find("\xc0\xaf",2,"foo",3,0,out,8,&total));
 assert(!insight_occurrences_find("foo",3,"\xff",1,0,out,8,&total));
 assert(!insight_occurrences_find("foo",3,"foo",3,0,NULL,1,&total));
 assert(!insight_occurrences_find("foo",3,"foo",3,0,out,8,NULL));
 const char *unicode="foo😀foo\xc2\xa0" "foo foo_foo foo\xcc\x81";
 assert(insight_occurrences_find(unicode,strlen(unicode),"foo",3,0,out,8,&total) && total==3);
 assert(out[0].start==0 && out[1].start==7 && out[2].start==12);
 assert(insight_occurrences_find(unicode,strlen(unicode),"foo",3,1,out,1,&total) && total==3 && out[0].start==7);
 assert(!insight_occurrences_find("😀",4,"😀",4,0,out,8,&total) && total==0);
 assert(!insight_occurrences_find("\xcc\x81",2,"\xcc\x81",2,0,out,8,&total));
 assert(insight_occurrences_find("e\xcc\x81 e",5,"e\xcc\x81",3,0,out,8,&total) && total==1);
 assert(insight_occurrences_find("变量 变量_x",strlen("变量 变量_x"),"变量",6,0,out,8,&total) && total==1);
 puts("occurrences: all checks passed"); return 0;
}
