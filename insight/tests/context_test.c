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
 puts("context: all checks passed"); return 0;
}
