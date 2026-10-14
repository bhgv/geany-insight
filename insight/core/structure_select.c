/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "structure_select.h"
#include <stdlib.h>
#include <stdint.h>
static int opener(char c) { return c=='(' || c=='[' || c=='{'; }
static int closer(char c) { return c==')' || c==']' || c=='}'; }
static int matches(char a,char b) { return (a=='(' && b==')') || (a=='[' && b==']') || (a=='{' && b=='}'); }
static int select_pair(const char *mask,size_t length,size_t begin,size_t end,int matching,size_t *first,size_t *last) {
 if(!mask || !first || !last || begin>end || end>length || length>SIZE_MAX/sizeof(size_t)) return -1;
 size_t target=begin;
 if(matching) {
  if(target<length && (opener(mask[target]) || closer(mask[target]))) {}
  else if(target && (opener(mask[target-1]) || closer(mask[target-1]))) target--;
  else return 0;
 }
 size_t count=0;
 for(size_t i=0;i<length;i++) if(opener(mask[i])) count++;
 if(!count) return 0;
 size_t *stack=malloc(count*sizeof(size_t));if(!stack) return -1;
 size_t depth=0,best=SIZE_MAX,a=0,z=0;
 for(size_t i=0;i<length;i++) {
  if(opener(mask[i])) { stack[depth++]=i;continue; }
  if(!closer(mask[i])) continue;
  /* Do not fabricate pairs across mismatched delimiters in unfinished code. */
  if(!depth || !matches(mask[stack[depth-1]],mask[i])) { depth=0;continue; }
  size_t start=stack[--depth],finish=i+1;
  int contains=matching ? (target==start || target==i) :
   (start<=begin && end<=finish && (begin!=end || begin<finish) && !(start==begin && finish==end));
  if(contains && finish-start<best) { best=finish-start;a=start;z=finish; }
 }
 free(stack);if(best==SIZE_MAX) return 0;
 *first=a;*last=z;return 1;
}
int insight_select_block(const char *mask,size_t length,size_t begin,size_t end,size_t *first,size_t *last) {
 return select_pair(mask,length,begin,end,0,first,last);
}
int insight_select_match(const char *mask,size_t length,size_t caret,size_t *first,size_t *last) {
 return select_pair(mask,length,caret,caret,1,first,last);
}
