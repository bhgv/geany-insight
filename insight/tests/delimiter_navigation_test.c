/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "delimiter_navigation.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void next(const char *text,size_t caret,unsigned action,int status,size_t expected)
{
    size_t position=999;assert(insight_delimiter_neighbor(text,strlen(text),caret,action,&position)==status);assert(position==(status==1 ? expected:999));
}
int main(void)
{
    const char *text="{ outer(α, inner[x](y)) }";size_t length=strlen(text);
    size_t y=strstr(text,"y")-text,inner=strstr(text,"(y)")-text,outer=strchr(text,'(')-text;
    InsightDelimiter *pairs=NULL;size_t count=0;
    assert(!insight_enclosing_delimiters(text,length,y,&pairs,&count));assert(count==3);assert(pairs[0].begin==inner);assert(pairs[1].begin==outer);assert(pairs[2].begin==0);free(pairs);
    next(text,y,0,1,inner);next(text,inner,0,1,outer);next(text,outer,0,0,0);
    next(text,y,1,1,inner+2);next(text,inner+2,1,1,length-3);next(text,length-3,1,0,0);
    next(text,0,2,0,0);next(text,length,2,1,0);next(text,0,3,1,length-1);next(text,length-1,3,0,0);
    next("{ { } }",2,2,1,0);next("{ { } }",4,3,1,6);next("{",1,2,1,0);next("}",0,3,0,0);
    next("(a [b] c)",4,0,1,0);next("(a [b] c)",4,1,1,8);
    next("([)]",2,0,0,0);next("( unfinished",5,0,0,0);next("(a) (b)",3,0,0,0);
    next("(a)",0,1,1,2);next("(a)",2,0,1,0);next("(a)",3,1,0,0);next("",0,0,0,0);
    size_t output=777;assert(insight_delimiter_neighbor(NULL,0,0,0,&output)==-1);assert(output==777);
    assert(insight_delimiter_neighbor("",0,1,0,&output)==-1);assert(insight_delimiter_neighbor("",0,0,4,&output)==-1);
    assert(insight_delimiter_neighbor("",INSIGHT_DELIMITER_LIMIT+1,0,0,&output)==-2);
    char *deep=malloc(2*INSIGHT_DELIMITER_DEPTH+3);memset(deep,'(',INSIGHT_DELIMITER_DEPTH+1);memset(deep+INSIGHT_DELIMITER_DEPTH+1,')',INSIGHT_DELIMITER_DEPTH+1);deep[2*INSIGHT_DELIMITER_DEPTH+2]=0;
    assert(insight_enclosing_delimiters(deep,strlen(deep),100,&pairs,&count)==-2);assert(!pairs && !count);
    deep[INSIGHT_DELIMITER_DEPTH]='x';deep[INSIGHT_DELIMITER_DEPTH+1]='x';assert(!insight_enclosing_delimiters(deep,strlen(deep),INSIGHT_DELIMITER_DEPTH,&pairs,&count));assert(count==INSIGHT_DELIMITER_DEPTH);free(pairs);free(deep);
    /* Every caret position in deterministic balanced nesting: results are
     * contained, properly ordered and their delimiter endpoints match. */
    for(size_t depth=1;depth<150;depth++) {
        char *nested=malloc(2*depth+2);memset(nested,'(',depth);nested[depth]='x';memset(nested+depth+1,')',depth);nested[2*depth+1]=0;
        for(size_t caret=0;caret<=2*depth+1;caret++) {
            assert(!insight_enclosing_delimiters(nested,2*depth+1,caret,&pairs,&count));
            for(size_t i=0;i<count;i++) { assert(pairs[i].begin<=caret && caret<pairs[i].end);assert(nested[pairs[i].begin]=='(' && nested[pairs[i].end-1]==')');if(i) assert(pairs[i].begin<pairs[i-1].begin && pairs[i].end>pairs[i-1].end); }
            free(pairs);
        }free(nested);
    }
    puts("Delimiter navigation: nesting, strict boundaries, malformed masks, UTF-8 offsets, depth/size guards and deterministic properties passed");return 0;
}
