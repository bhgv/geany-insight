/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "index.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
struct InsightIndex { InsightSymbol *items; size_t count; };
static char *copy(const char *s) {
 size_t n=strlen(s)+1; char *p=malloc(n); if(p) memcpy(p,s,n); return p;
}
static void release(InsightSymbol *s) {
 free((void *)s->name); free((void *)s->path);
 free((void *)s->scope); free((void *)s->kind);
}
static int compare(const void *a,const void *b) {
 const InsightSymbol *x=a,*y=b; int r=strcmp(x->name,y->name);
 if(!r) r=strcmp(x->path,y->path);
 if(!r) r=(x->line>y->line)-(x->line<y->line);
 if(!r) r=strcmp(x->scope,y->scope);
 if(!r) r=strcmp(x->kind,y->kind);
 return r;
}
InsightIndex *insight_index_new(void) { return calloc(1,sizeof(InsightIndex)); }
void insight_index_free(InsightIndex *i) {
 if(!i) return;
 for(size_t n=0;n<i->count;n++) release(&i->items[n]);
 free(i->items); free(i);
}
size_t insight_index_count(const InsightIndex *i) { return i ? i->count : 0; }
int insight_index_update(InsightIndex *i,const char *path,const InsightSymbol *s,size_t count) {
 if(!i || !path || !*path || (count && !s)) return 0;
 size_t keep=0;
 for(size_t n=0;n<i->count;n++) if(strcmp(i->items[n].path,path)) keep++;
 if(count>SIZE_MAX-keep || keep+count>SIZE_MAX/sizeof(InsightSymbol)) return 0;
 size_t total=keep+count;
 InsightSymbol *next=total ? calloc(total,sizeof *next) : NULL;
 if(total && !next) return 0;
 for(size_t n=0;n<count;n++) {
  if(!s[n].name || !*s[n].name || !s[n].line) goto failure;
  next[n].name=copy(s[n].name); next[n].path=copy(path);
  next[n].scope=copy(s[n].scope ? s[n].scope : "");
  next[n].kind=copy(s[n].kind ? s[n].kind : ""); next[n].line=s[n].line;
  if(!next[n].name || !next[n].path || !next[n].scope || !next[n].kind) goto failure;
 }
 size_t pos=count;
 for(size_t n=0;n<i->count;n++) {
  if(strcmp(i->items[n].path,path)) next[pos++]=i->items[n];
  else release(&i->items[n]);
 }
 free(i->items); i->items=next; i->count=total;
 if(total>1) qsort(next,total,sizeof *next,compare);
 return 1;
failure:
 for(size_t n=0;n<count;n++) release(&next[n]);
 free(next); return 0;
}
size_t insight_index_find(const InsightIndex *i,const char *query,int exact,
 const InsightSymbol **out,size_t capacity) {
 if(!i || !query) return 0;
 size_t left=0,right=i->count;
 while(left<right) {
  size_t mid=left+(right-left)/2;
  if(strcmp(i->items[mid].name,query)<0) left=mid+1; else right=mid;
 }
 size_t length=strlen(query),matches=0;
 for(size_t n=left;n<i->count;n++) {
  int r=exact ? strcmp(i->items[n].name,query) : strncmp(i->items[n].name,query,length);
  if(r) break;
  if(out && matches<capacity) out[matches]=&i->items[n];
  matches++;
 }
 return matches;
}
