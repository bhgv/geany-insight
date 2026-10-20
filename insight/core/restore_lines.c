/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "restore_lines.h"
#include "diff.h"
#include <stdlib.h>
#include <string.h>
static size_t bounded_length(const char *text)
{
    size_t n=0;while(n<=INSIGHT_RESTORE_LIMIT && text[n]) n++;return n;
}
static int split_eol(const char *text,size_t length,size_t position)
{
    return position>0 && position<length && text[position-1]=='\r' && text[position]=='\n';
}
static size_t line_start(const char *text,size_t position)
{
    while(position && text[position-1]!='\r' && text[position-1]!='\n') position--;
    return position;
}
static size_t line_end(const char *text,size_t length,size_t position)
{
    while(position<length && text[position]!='\r' && text[position]!='\n') position++;
    if(position<length) { char c=text[position++];if(c=='\r' && position<length && text[position]=='\n') position++; }
    return position;
}
void insight_restore_lines_clear(InsightRestorePlan *plan)
{
    free(plan->replacement);memset(plan,0,sizeof *plan);
}
int insight_restore_lines_plan(const char *original,const char *current,
    size_t anchor,size_t caret,InsightRestorePlan *out)
{
    memset(out,0,sizeof *out);
    if(!original || !current) return -4;
    size_t original_length=bounded_length(original),length=bounded_length(current);
    if(original_length>INSIGHT_RESTORE_LIMIT || length>INSIGHT_RESTORE_LIMIT) return -2;
    if(anchor>length || caret>length || split_eol(current,length,anchor) || split_eol(current,length,caret)) return -4;
    size_t begin=anchor<caret ? anchor:caret,end=anchor>caret ? anchor:caret;
    size_t first=line_start(current,begin),last;
    if(begin!=end && end==line_start(current,end)) last=end;
    else last=line_end(current,length,end);
    InsightDiff diff={0};int status=insight_diff_compute(original,current,&diff);
    if(status) return status;
    for(size_t i=0;i<diff.count;i++)
    {
        InsightDiffHunk *h=&diff.hunks[i];
        int touches=h->b_begin==h->b_end ? h->b_begin>=first && h->b_begin<=last : h->b_begin<last && h->b_end>first;
        if(!touches) continue;
        if(!out->blocks) { out->current_begin=h->b_begin;out->original_begin=h->a_begin; }
        out->current_end=h->b_end;out->original_end=h->a_end;out->blocks++;
    }
    insight_diff_clear(&diff);
    if(!out->blocks) return 0;
    out->length=out->original_end-out->original_begin;
    if(out->length>INSIGHT_RESTORE_LIMIT-(length-(out->current_end-out->current_begin))) { memset(out,0,sizeof *out);return -2; }
    out->replacement=malloc(out->length+1);
    if(!out->replacement) { memset(out,0,sizeof *out);return -1; }
    memcpy(out->replacement,original+out->original_begin,out->length);out->replacement[out->length]=0;
    return 0;
}
