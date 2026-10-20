/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "delimiter_navigation.h"
#include <stdlib.h>
static int opening(char c) { return c=='(' || c=='[' || c=='{'; }
static int closing(char c) { return c==')' || c==']' || c=='}'; }
static int matched(char a,char b) { return (a=='(' && b==')') || (a=='[' && b==']') || (a=='{' && b=='}'); }
int insight_enclosing_delimiters(const char *mask,size_t length,size_t caret,InsightDelimiter **pairs,size_t *count)
{
    if(!pairs || !count) return -1;
    *pairs=NULL;*count=0;
    if(!mask || caret>length) return -1;
    if(length>INSIGHT_DELIMITER_LIMIT) return -2;
    size_t *stack=malloc(INSIGHT_DELIMITER_DEPTH*sizeof *stack);
    InsightDelimiter *result=malloc(INSIGHT_DELIMITER_DEPTH*sizeof *result);
    if(!stack || !result) { free(stack);free(result);return -1; }
    size_t depth=0,found=0;
    for(size_t i=0;i<length;i++)
    {
        if(opening(mask[i]))
        {
            if(depth==INSIGHT_DELIMITER_DEPTH) { free(stack);free(result);return -2; }
            stack[depth++]=i;
        }
        else if(closing(mask[i]))
        {
            if(!depth || !matched(mask[stack[depth-1]],mask[i])) { depth=0;continue; }
            size_t begin=stack[--depth];
            if(begin<=caret && caret<=i) result[found++]=(InsightDelimiter){begin,i+1};
        }
    }
    free(stack);
    if(!found) { free(result);result=NULL; }
    *pairs=result;*count=found;return 0;
}
int insight_delimiter_neighbor(const char *mask,size_t length,size_t caret,unsigned action,size_t *position)
{
    if(!mask || !position || caret>length || action>3) return -1;
    if(length>INSIGHT_DELIMITER_LIMIT) return -2;
    if(action==2)
    {
        for(size_t i=caret;i>0;) if(mask[--i]=='{') { *position=i;return 1; }
        return 0;
    }
    if(action==3)
    {
        for(size_t i=caret;i<length;) { i++;if(i<length && mask[i]=='}') { *position=i;return 1; } }
        return 0;
    }
    InsightDelimiter *pairs=NULL;size_t count=0;
    int status=insight_enclosing_delimiters(mask,length,caret,&pairs,&count);
    if(status) return status;
    int found=0;
    for(size_t i=0;i<count;i++) if(mask[pairs[i].begin]=='(')
    {
        size_t target=action ? pairs[i].end-1 : pairs[i].begin;
        if(action ? target>caret : target<caret) { *position=target;found=1;break; }
    }
    free(pairs);return found;
}
