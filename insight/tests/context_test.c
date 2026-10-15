/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "context.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
 size_t a,b; const char *s="call(имя_foo, 123)";
 assert(insight_word_at(s,strlen(s),8,&a,&b));
 assert(b-a==strlen("имя_foo") && !memcmp(s+a,"имя_foo",b-a));
 assert(insight_word_at("foo",3,3,&a,&b) && a==0 && b==3);
 assert(!insight_word_at("123",3,1,&a,&b));
 assert(!insight_word_at("",0,0,&a,&b));
 assert(!insight_word_at("foo",3,4,&a,&b));
 const char *unicode="foo\xc2\xa0" "bar"; /* NBSP is not part of an identifier. */
 assert(insight_word_at(unicode,strlen(unicode),7,&a,&b) && a==5 && b==8);
 unicode="foo😀bar";assert(insight_word_at(unicode,strlen(unicode),8,&a,&b) && a==7 && b==10);
 unicode="e\xcc\x81";assert(insight_word_at(unicode,strlen(unicode),2,&a,&b) && a==0 && b==3);
 unicode="变量";assert(insight_word_at(unicode,strlen(unicode),4,&a,&b) && a==0 && b==6);
 assert(!insight_word_at("😀",4,0,&a,&b));
 assert(!insight_word_at("\xcc\x81",2,0,&a,&b));
 size_t end,ts,te; unsigned long first;
 assert(insight_context_range("a\r\nb\nc",6,2,1,&a,&end,&first,&ts,&te));
 assert(a==0 && end==6 && first==1 && ts==3 && te==4);
 assert(!insight_context_range("a",1,3,1,&a,&end,&first,&ts,&te));
 assert(insight_context_range("a\n",2,2,0,&a,&end,&first,&ts,&te) && a==2 && end==2);
 puts("context: all checks passed"); return 0;
}
