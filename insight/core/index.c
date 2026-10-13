/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "index.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
struct InsightIndex { InsightSymbol *items; size_t count; };
static char *copy(const char *s) {
 size_t n=strlen(s)+1; char *p=malloc(n); if(p) memcpy(p,s,n); return p;
}
static void release(InsightSymbol *s) {
 free((void *)s->name); free((void *)s->path);
 free((void *)s->scope); free((void *)s->kind);
 free((void *)s->signature); free((void *)s->type); free((void *)s->inheritance);
}
static int compare(const void *a,const void *b) {
 const InsightSymbol *x=a,*y=b; int r=strcmp(x->name,y->name);
 if(!r) r=strcmp(x->path,y->path);
 if(!r) r=(x->line>y->line)-(x->line<y->line);
 if(!r) r=strcmp(x->scope,y->scope);
 if(!r) r=strcmp(x->kind,y->kind);
 if(!r) r=strcmp(x->signature,y->signature);
 if(!r) r=strcmp(x->type,y->type);
 if(!r) r=strcmp(x->inheritance,y->inheritance);
 return r;
}
InsightIndex *insight_index_new(void) { return calloc(1,sizeof(InsightIndex)); }
void insight_index_free(InsightIndex *i) {
 if(!i) return;
 for(size_t n=0;n<i->count;n++) release(&i->items[n]);
 free(i->items); free(i);
}
size_t insight_index_count(const InsightIndex *i) { return i ? i->count : 0; }
static int compare_paths(const void *a,const void *b) { return strcmp(*(const char *const *)a,*(const char *const *)b); }
size_t insight_index_files(const InsightIndex *i,const char *prefix,size_t offset,const char **out,size_t capacity) {
 if(!i || !i->count || !prefix) return 0;
 if(i->count>SIZE_MAX/sizeof(char *)) return SIZE_MAX;
 const char **paths=malloc(i->count*sizeof *paths); if(!paths) return SIZE_MAX;
 for(size_t n=0;n<i->count;n++) paths[n]=i->items[n].path;
 qsort(paths,i->count,sizeof *paths,compare_paths);
 size_t count=0,shown=0,length=strlen(prefix);
 for(size_t n=0;n<i->count;n++) {
  if(n && !strcmp(paths[n-1],paths[n])) continue;
  if(strncmp(paths[n],prefix,length)) continue;
  if(out && count>=offset && shown<capacity) out[shown++]=paths[n];
  count++;
 }
 free(paths); return count;
}
size_t insight_index_file_count(const InsightIndex *i) {
 return insight_index_files(i,"",0,NULL,0);
}
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
  next[n].signature=copy(s[n].signature ? s[n].signature : "");
  next[n].type=copy(s[n].type ? s[n].type : "");
  next[n].inheritance=copy(s[n].inheritance ? s[n].inheritance : "");
  if(!next[n].name || !next[n].path || !next[n].scope || !next[n].kind || !next[n].signature || !next[n].type || !next[n].inheritance) goto failure;
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
size_t insight_index_query(const InsightIndex *i,const char *query,int exact,const char *scope,const char *kind,const char *path,size_t offset,
 const InsightSymbol **out,size_t capacity) {
 if(!i || !query) return 0;
 size_t left=0,right=i->count;
 while(left<right) {
  size_t mid=left+(right-left)/2;
  if(strcmp(i->items[mid].name,query)<0) left=mid+1; else right=mid;
 }
 size_t length=strlen(query),matches=0,shown=0;
 for(size_t n=left;n<i->count;n++) {
  int r=exact ? strcmp(i->items[n].name,query) : strncmp(i->items[n].name,query,length);
  if(r) break;
  if(scope && *scope && !strstr(i->items[n].scope,scope)) continue;
  if(kind && *kind && strcmp(i->items[n].kind,kind)) continue;
  if(path && *path && strcmp(i->items[n].path,path)) continue;
  if(out && matches>=offset && shown<capacity) out[shown++]=&i->items[n];
  matches++;
 }
 return matches;
}

size_t insight_index_find_filtered(const InsightIndex *i,const char *query,int exact,const char *scope,const char *kind,const InsightSymbol **out,size_t capacity) {
 return insight_index_query(i,query,exact,scope,kind,NULL,0,out,capacity);
}
size_t insight_index_find(const InsightIndex *i,const char *query,int exact,const InsightSymbol **out,size_t capacity) {
 return insight_index_find_filtered(i,query,exact,NULL,NULL,out,capacity);
}
InsightIndex *insight_index_clone(const InsightIndex *source) {
 if(!source) return NULL;
 InsightIndex *next=insight_index_new(); if(!next) return NULL;
 next->count=source->count;
 next->items=source->count ? calloc(source->count,sizeof *next->items) : NULL;
 if(source->count && !next->items) { next->count=0; insight_index_free(next); return NULL; }
 for(size_t n=0;n<source->count;n++) {
  next->items[n].name=copy(source->items[n].name);
  next->items[n].path=copy(source->items[n].path);
  next->items[n].scope=copy(source->items[n].scope);
  next->items[n].kind=copy(source->items[n].kind);
  next->items[n].signature=copy(source->items[n].signature);
  next->items[n].type=copy(source->items[n].type);
  next->items[n].inheritance=copy(source->items[n].inheritance);
  next->items[n].line=source->items[n].line;
  if(!next->items[n].name || !next->items[n].path || !next->items[n].scope || !next->items[n].kind || !next->items[n].signature || !next->items[n].type || !next->items[n].inheritance) {
   insight_index_free(next); return NULL;
  }
 }
 return next;
}
#define SNAPSHOT_FIELDS_LIMIT 65536u
#define SNAPSHOT_SYMBOLS_LIMIT 250000u
static const unsigned char magic[8]={'G','I','S','I','D','X',0,2};
static uint32_t checksum(const unsigned char *s,size_t length) {
 uint32_t crc=UINT32_MAX;
 for(size_t n=0;n<length;n++) {
  crc^=s[n];
  for(int bit=0;bit<8;bit++) crc=(crc>>1) ^ (0xedb88320u & (0u-(crc&1u)));
 }
 return crc^UINT32_MAX;
}
static int field_size(const char *s,size_t *total) {
 size_t length=strlen(s);
 if(length>SNAPSHOT_FIELDS_LIMIT || *total>INSIGHT_INDEX_SNAPSHOT_LIMIT-4-length) return 0;
 *total+=4+length; return 1;
}
static void put32(unsigned char **cursor,uint32_t value) {
 for(unsigned n=0;n<4;n++) *(*cursor)++=(unsigned char)(value>>(8*n));
}
static void put64(unsigned char **cursor,uint64_t value) {
 for(unsigned n=0;n<8;n++) *(*cursor)++=(unsigned char)(value>>(8*n));
}
static void put_string(unsigned char **cursor,const char *s) {
 size_t length=strlen(s); put32(cursor,(uint32_t)length);
 memcpy(*cursor,s,length); *cursor+=length;
}
int insight_index_serialize(const InsightIndex *i,const char *root,unsigned char **data,size_t *length) {
 if(data) *data=NULL;
 if(length) *length=0;
 if(!i || !root || !data || !length || i->count>SNAPSHOT_SYMBOLS_LIMIT) return 0;
 size_t total=8+4+4; /* magic, count, checksum */
 if(!field_size(root,&total)) return 0;
 for(size_t n=0;n<i->count;n++) {
  const InsightSymbol *s=&i->items[n];
  if(total>INSIGHT_INDEX_SNAPSHOT_LIMIT-8) return 0;
  total+=8;
  if(!field_size(s->name,&total) || !field_size(s->path,&total) ||
   !field_size(s->scope,&total) || !field_size(s->kind,&total) ||
   !field_size(s->signature,&total) || !field_size(s->type,&total) || !field_size(s->inheritance,&total)) return 0;
 }
 unsigned char *buffer=malloc(total); if(!buffer) return 0;
 unsigned char *cursor=buffer; memcpy(cursor,magic,8); cursor+=8;
 put32(&cursor,(uint32_t)i->count); put_string(&cursor,root);
 for(size_t n=0;n<i->count;n++) {
  const InsightSymbol *s=&i->items[n]; put64(&cursor,(uint64_t)s->line);
  put_string(&cursor,s->name); put_string(&cursor,s->path);
  put_string(&cursor,s->scope); put_string(&cursor,s->kind);
  put_string(&cursor,s->signature); put_string(&cursor,s->type); put_string(&cursor,s->inheritance);
 }
 put32(&cursor,checksum(buffer,total-4));
 *data=buffer; *length=total; return 1;
}
static uint32_t get32(const unsigned char *s) {
 uint32_t value=0;
 for(unsigned n=0;n<4;n++) value|=(uint32_t)s[n]<<(8*n);
 return value;
}
static uint64_t get64(const unsigned char *s) {
 uint64_t value=0;
 for(unsigned n=0;n<8;n++) value|=(uint64_t)s[n]<<(8*n);
 return value;
}
static char *get_string(const unsigned char **cursor,const unsigned char *end) {
 if((size_t)(end-*cursor)<4) return NULL;
 uint32_t length=get32(*cursor); *cursor+=4;
 if(length>SNAPSHOT_FIELDS_LIMIT || (size_t)(end-*cursor)<length || memchr(*cursor,0,length)) return NULL;
 char *s=malloc((size_t)length+1); if(!s) return NULL;
 memcpy(s,*cursor,length); s[length]=0; *cursor+=length; return s;
}
InsightIndex *insight_index_deserialize(const unsigned char *data,size_t length,char **root) {
 if(root) *root=NULL;
 if(!data || !root || length<20 || length>INSIGHT_INDEX_SNAPSHOT_LIMIT || memcmp(data,magic,7) || (data[7]!=1 && data[7]!=2)) return NULL;
 if(checksum(data,length-4)!=get32(data+length-4)) return NULL;
 uint32_t count=get32(data+8);
 if(count>SNAPSHOT_SYMBOLS_LIMIT) return NULL;
 const unsigned char *cursor=data+12,*end=data+length-4;
 char *path=get_string(&cursor,end); if(!path) return NULL;
 if(count>(size_t)(end-cursor)/(data[7]==1 ? 26u : 38u)) { free(path); return NULL; }
 InsightIndex *i=insight_index_new(); if(!i) { free(path); return NULL; }
 i->items=count ? calloc(count,sizeof *i->items) : NULL;
 if(count && !i->items) { free(path); insight_index_free(i); return NULL; }
 i->count=count;
 for(size_t n=0;n<count;n++) {
  if((size_t)(end-cursor)<8) goto failure;
  uint64_t line=get64(cursor); cursor+=8;
  if(!line || line>ULONG_MAX) goto failure;
  InsightSymbol *s=&i->items[n]; s->line=(unsigned long)line;
  s->name=get_string(&cursor,end); s->path=get_string(&cursor,end);
  s->scope=get_string(&cursor,end); s->kind=get_string(&cursor,end);
  s->signature=data[7]==2 ? get_string(&cursor,end) : copy("");
  s->type=data[7]==2 ? get_string(&cursor,end) : copy("");
  s->inheritance=data[7]==2 ? get_string(&cursor,end) : copy("");
  if(!s->name || !*s->name || !s->path || !*s->path || !s->scope || !s->kind || !s->signature || !s->type || !s->inheritance) goto failure;
 }
 if(cursor!=end) goto failure;
 if(count>1) qsort(i->items,count,sizeof *i->items,compare);
 *root=path; return i;
failure:
 free(path); insight_index_free(i); return NULL;
}
