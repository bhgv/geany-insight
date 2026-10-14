/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "structure_select.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
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
 if(!mask || !first || !last || caret>length) return -1;
 size_t target=caret;
 if(target<length && (opener(mask[target]) || closer(mask[target]))) {}
 else if(target && (opener(mask[target-1]) || closer(mask[target-1]))) target--;
 else return 0;
 if(closer(mask[target])) return select_pair(mask,length,target,target,1,first,last);
 size_t capacity=32,depth=0;char *stack=malloc(capacity);if(!stack) return -1;
 for(size_t i=target;i<length;i++) {
  if(opener(mask[i])) {
   if(depth==capacity) {
    if(capacity>SIZE_MAX/2) { free(stack);return -1; }
    char *grown=realloc(stack,capacity*2);if(!grown) { free(stack);return -1; }stack=grown;capacity*=2;
   }
   stack[depth++]=mask[i];
  } else if(closer(mask[i])) {
   if(!depth || !matches(stack[depth-1],mask[i])) { free(stack);return 0; }
   if(!--depth) { *first=target;*last=i+1;free(stack);return 1; }
  }
 }
 free(stack);return 0;
}
/* A brace-bodied definition, starting at the parser's declaration line. */
int insight_function_extent(const char *mask,size_t length,size_t declaration,size_t *last) {
 if(!mask || !last || declaration>=length) return -1;
 int parameters=0,initializer=0;
 for(size_t i=declaration;i<length;i++) {
  char c=mask[i];
  if(c=='(' || c=='[') {
   size_t a=0,z=0;if(insight_select_match(mask,length,i,&a,&z)!=1 || a!=i) return 0;
   if(c=='(') parameters=1;
   i=z-1;continue;
  }
  if(c==';' || c=='}') return 0;
  if(c==':' && parameters && (i==0 || mask[i-1]!=':') && (i+1==length || mask[i+1]!=':')) initializer=1;
  if(c=='=' && i+1<length && mask[i+1]=='>') {
   size_t next=i+2;while(next<length && (mask[next]==' ' || mask[next]=='\t' || mask[next]=='\r' || mask[next]=='\n')) next++;
   if(next==length || mask[next]!='{') return 0;
  }
  if(c=='{') {
   if(!parameters) return 0;
   size_t a=0,z=0;if(insight_select_match(mask,length,i,&a,&z)!=1 || a!=i) return 0;
   size_t next=z;while(next<length && (mask[next]==' ' || mask[next]=='\t' || mask[next]=='\r' || mask[next]=='\n')) next++;
   if(next<length && (mask[next]=='{' || (initializer && mask[next]==','))) { i=z-1;continue; }
   *last=z;return 1;
  }
 }
 return 0;
}
int insight_neighbor_position(const size_t *positions,size_t count,size_t current,int direction,size_t *next) {
 if((!positions && count) || !next || (direction!=1 && direction!=-1)) return -1;
 int found=0;size_t best=0;
 for(size_t i=0;i<count;i++) {
  size_t value=positions[i];
  if((direction>0 && value>current && (!found || value<best)) ||
     (direction<0 && value<current && (!found || value>best))) { best=value;found=1; }
 }
 if(found) *next=best;
 return found;
}

static int call_identifier(unsigned char c) {
 return (c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_' || c=='$' || c>=128;
}
int insight_next_call(const char *mask,size_t length,size_t *cursor,size_t *first,size_t *last) {
 static const char *keywords[]={"if","for","while","switch","catch","sizeof","alignof","decltype","typeof","return","static_assert","synchronized","with"};
 if(!mask || !cursor || !first || !last || *cursor>length) return -1;
 size_t i=*cursor;
 while(i<length) {
  if(!call_identifier((unsigned char)mask[i])) { i++;continue; }
  size_t begin=i++;
  while(i<length && (call_identifier((unsigned char)mask[i]) || (mask[i]>='0' && mask[i]<='9'))) i++;
  size_t end=i,j=i;
  while(j<length && (mask[j]==' ' || mask[j]=='\t' || mask[j]=='\r' || mask[j]=='\n')) j++;
  if(j==length || mask[j]!='(') continue;
  int skip=0;
  for(size_t k=0;k<sizeof keywords/sizeof keywords[0];k++) if(strlen(keywords[k])==end-begin && !memcmp(mask+begin,keywords[k],end-begin)) { skip=1;break; }
  if(skip) continue;
  *cursor=i;*first=begin;*last=end;return 1;
 }
 *cursor=length;return 0;
}
