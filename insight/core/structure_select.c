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

static int horizontal(char c) { return c==' ' || c=='\t' || c=='\r'; }
static int word_char(unsigned char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c>=128; }
static int word_is(const char *mask,size_t begin,size_t end,const char *word) { return end-begin==strlen(word) && !memcmp(mask+begin,word,end-begin); }
static size_t indentation(const char *text,size_t start,size_t end) { size_t width=0;for(size_t i=start;i<end;i++) { if(text[i]==' ') width++;else if(text[i]=='\t') width=(width/8+1)*8;else break; }return width; }
static int function_range_impl(const char *text,const char *mask,size_t length,size_t declaration,InsightFunctionStyle style,size_t *body,size_t *last,int suite) {
 if(!text || !mask || !body || !last || declaration>=length || style<INSIGHT_FUNCTION_BRACES || style>INSIGHT_FUNCTION_LUA) return -1;
 if(style==INSIGHT_FUNCTION_BRACES) {
  size_t end=0;if(insight_function_extent(mask,length,declaration,&end)!=1) return 0;
  for(size_t i=declaration;i<end;i++) if(mask[i]=='{') { size_t a,z;if(insight_select_match(mask,length,i,&a,&z)==1 && z==end) { *body=i+1;*last=end;return 1; } }
  return 0;
 }
 size_t start=declaration;while(start<length && horizontal(mask[start])) start++;
 if(style==INSIGHT_FUNCTION_PYTHON) {
  size_t word=start;while(word<length && word_char((unsigned char)mask[word])) word++;
  if(word_is(mask,start,word,"async")) { start=word;while(start<length && horizontal(mask[start])) start++;word=start;while(word<length && word_char((unsigned char)mask[word])) word++; }
  if(!word_is(mask,start,word,"def")) {
   static const char *headers[]={"class","if","elif","else","for","while","try","except","finally","with","match","case"};int accepted=0;
   if(suite) for(size_t i=0;i<sizeof headers/sizeof headers[0];i++) if(word_is(mask,start,word,headers[i])) accepted=1;
   if(!accepted) return 0;
  }
  size_t colon=word;
  for(;colon<length;colon++) {
   if(mask[colon]=='(' || mask[colon]=='[' || mask[colon]=='{') { size_t a,z;if(insight_select_match(mask,length,colon,&a,&z)!=1) return 0;colon=z-1; }
   else if(mask[colon]==':') break;
   else if(mask[colon]=='\n' || mask[colon]==';') return 0;
  }
  if(colon==length) return 0;
  size_t line_end=colon+1;while(line_end<length && mask[line_end]!='\n') line_end++;
  size_t code=colon+1;while(code<line_end && horizontal(mask[code])) code++;
  if(code<line_end) { *body=colon+1;*last=line_end;return 1; }
  size_t indent=indentation(text,declaration,length),finish=0,first_body=line_end<length ? line_end+1 : length;
  size_t pos=first_body,depth=0;char brackets[4096];int continuation=0,has_body=0;
  while(pos<length) {
   size_t end=pos;while(end<length && mask[end]!='\n') end++;
   size_t token=pos;while(token<end && horizontal(mask[token])) token++;
   size_t width=indentation(text,pos,end);
   if(token<end) {
    if(!depth && !continuation && width<=indent) break;
    if(!depth && !continuation && width>indent) has_body=1;
    for(size_t i=token;i<end;i++) { if(opener(mask[i])) { if(depth==sizeof brackets) return 0;brackets[depth++]=mask[i]; }else if(closer(mask[i])) { if(!depth || !matches(brackets[depth-1],mask[i])) return 0;depth--; } }
    size_t tail=end;while(tail>pos && horizontal(mask[tail-1])) tail--;continuation=tail>pos && mask[tail-1]=='\\';finish=end;
   } else {
    /* Masked docstrings can be the entire body; their physical indentation
     * establishes a suite without treating dedented string contents as code. */
    size_t raw=pos;while(raw<end && horizontal(text[raw])) raw++;
    if(raw<end && (text[raw]=='\'' || text[raw]=='"') && width>indent) { has_body=1;finish=end; }
    if(has_body && raw<end && text[raw]!='#' && token==end) finish=end;
   }
   pos=end<length ? end+1 : length;
  }
  if(!has_body || depth || continuation) return 0;
  *body=first_body;*last=finish;return 1;
 }
 if(suite) {
  size_t word=start;while(word<length && word_char((unsigned char)mask[word])) word++;
  if(word_is(mask,start,word,"local")) { start=word;while(start<length && horizontal(mask[start])) start++;word=start;while(word<length && word_char((unsigned char)mask[word])) word++;if(!word_is(mask,start,word,"function")) return 0; }
  else if(!word_is(mask,start,word,"function") && !word_is(mask,start,word,"if") && !word_is(mask,start,word,"for") && !word_is(mask,start,word,"while") && !word_is(mask,start,word,"do") && !word_is(mask,start,word,"repeat")) return 0;
 }
 /* Lua block keywords nest independently of expression parentheses. */
 char stack[4096];size_t depth=0;int started=0;size_t first_body=0;
 for(size_t i=start;i<length;) {
  if(!word_char((unsigned char)mask[i])) { i++;continue; }
  size_t begin=i++;while(i<length && word_char((unsigned char)mask[i])) i++;
  if(word_is(mask,begin,i,"function")) {
   if(depth==sizeof stack) return 0;
   stack[depth++]='f';
   if(!started) { started=1;size_t paren=i;while(paren<length && mask[paren]!='(' && mask[paren]!='\n') paren++;size_t a,z;if(paren==length || mask[paren]!='(' || insight_select_match(mask,length,paren,&a,&z)!=1) return 0;first_body=z;i=z; }
  } else if((started || suite) && (word_is(mask,begin,i,"if") || word_is(mask,begin,i,"for") || word_is(mask,begin,i,"while") || word_is(mask,begin,i,"repeat"))) {
   if(depth==sizeof stack) return 0;
   if(!started) { started=1;first_body=i; }
   stack[depth++]=word_is(mask,begin,i,"repeat") ? 'r' : (word_is(mask,begin,i,"if") ? 'i' : 'w');
  } else if((started || suite) && word_is(mask,begin,i,"do")) {
   if(!started) { started=1;first_body=i; }
   if(depth && stack[depth-1]=='w') stack[depth-1]='d';else { if(depth==sizeof stack) return 0;stack[depth++]='d'; }
  } else if(started && (word_is(mask,begin,i,"end") || word_is(mask,begin,i,"until"))) {
   int repeat=word_is(mask,begin,i,"until");if(!depth || (stack[depth-1]=='r')!=repeat) return 0;
   if(!--depth) { if(repeat) while(i<length && mask[i]!='\n') i++;*body=first_body;*last=i;return 1; }
  }
 }
 return 0;
}

int insight_function_range(const char *text,const char *mask,size_t length,size_t declaration,InsightFunctionStyle style,size_t *body,size_t *last) {
 return function_range_impl(text,mask,length,declaration,style,body,last,0);
}
int insight_suite_range(const char *text,const char *mask,size_t length,size_t declaration,InsightFunctionStyle style,size_t *body,size_t *last) {
 return function_range_impl(text,mask,length,declaration,style,body,last,1);
}
int insight_select_suite(const char *text,const char *mask,size_t length,size_t begin,size_t end,InsightFunctionStyle style,size_t *first,size_t *last) {
 if(!text || !mask || !first || !last || begin>end || end>length || style<INSIGHT_FUNCTION_BRACES || style>INSIGHT_FUNCTION_LUA) return -1;
 if(style==INSIGHT_FUNCTION_BRACES) return insight_select_block(mask,length,begin,end,first,last);
 size_t best=SIZE_MAX,a=0,z=0;
 if(insight_select_block(mask,length,begin,end,&a,&z)==1) best=z-a;
 for(size_t line=0;line<length && line<=begin;) {
  size_t body=0,finish=0;
  if(function_range_impl(text,mask,length,line,style,&body,&finish,1)==1 && end<=finish &&
     (begin!=end || begin<finish) && !(line==begin && finish==end) && finish-line<best) { best=finish-line;a=line;z=finish; }
  while(line<length && mask[line]!='\n') line++;
  if(line<length) line++;
 }
 if(best==SIZE_MAX) return 0;
 *first=a;*last=z;return 1;
}
