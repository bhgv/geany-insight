/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "index.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t crc32(const unsigned char *s,size_t n) {
 uint32_t c=UINT32_MAX;
 for(size_t j=0;j<n;j++) { c^=s[j]; for(int k=0;k<8;k++) c=(c>>1)^(0xedb88320u&(0u-(c&1u))); }
 return c^UINT32_MAX;
}
static void repair(unsigned char *s,size_t n) {
 uint32_t c=crc32(s,n-4);
 for(unsigned j=0;j<4;j++) s[n-4+j]=(unsigned char)(c>>(8*j));
}
static void rejected(const unsigned char *data,size_t n) {
 char *root=(char *)1;
 InsightIndex *i=insight_index_deserialize(data,n,&root);
 assert(i==NULL && root==NULL);
}
int main(void) {
 InsightIndex *i=insight_index_new(); assert(i);
 InsightSymbol symbols[]={ {"alpha",NULL,"Scope","f",123,"(int value)","int","Base, ns::Iface"},{"Привет",NULL,NULL,NULL,ULONG_MAX,NULL,NULL,NULL} };
 assert(insight_index_update(i,"/project/файл\n\t.cpp",symbols,2));
 unsigned char *data=NULL; size_t length=0;
 assert(insight_index_serialize(i,"/проекты/код",&data,&length));
 char *root=NULL; InsightIndex *loaded=insight_index_deserialize(data,length,&root);
 assert(loaded && !strcmp(root,"/проекты/код")); free(root);
 const InsightSymbol *found[2];
 assert(insight_index_find(loaded,"Привет",1,found,2)==1);
 assert(found[0]->line==ULONG_MAX && !strcmp(found[0]->path,"/project/файл\n\t.cpp"));
 assert(insight_index_find(loaded,"alpha",1,found,2)==1);
 assert(!strcmp(found[0]->signature,"(int value)") && !strcmp(found[0]->type,"int") && !strcmp(found[0]->inheritance,"Base, ns::Iface"));
 unsigned char *legacy=malloc(length); assert(legacy); size_t header=16+strlen("/проекты/код"),input=header,output=header; memcpy(legacy,data,header);legacy[7]=1;
 for(unsigned record=0;record<2;record++) {
  memcpy(legacy+output,data+input,8); input+=8;output+=8;
  for(unsigned field=0;field<7;field++) {
   uint32_t size=0;for(unsigned byte=0;byte<4;byte++) size|=(uint32_t)data[input+byte]<<(8*byte);
   if(field<4) { memcpy(legacy+output,data+input,4+size);output+=4+size; }
   input+=4+size;
  }
 }
 size_t legacy_length=output+4;repair(legacy,legacy_length); char *legacy_root=NULL;InsightIndex *old=insight_index_deserialize(legacy,legacy_length,&legacy_root); assert(old);
 assert(insight_index_find(old,"alpha",1,found,2)==1 && !strcmp(found[0]->signature,"") && !strcmp(found[0]->type,"") && !strcmp(found[0]->inheritance,""));
 free(legacy_root);free(legacy);insight_index_free(old);
 unsigned char *roundtrip=NULL; size_t next_length=0;
 assert(insight_index_serialize(loaded,"/проекты/код",&roundtrip,&next_length));
 assert(length==next_length && !memcmp(data,roundtrip,length)); free(roundtrip);
 InsightIndex *clone=insight_index_clone(loaded); assert(clone);
 assert(insight_index_find(clone,"alpha",1,found,2)==1 && !strcmp(found[0]->inheritance,"Base, ns::Iface"));
 assert(insight_index_update(clone,"/project/файл\n\t.cpp",NULL,0));
 assert(insight_index_count(clone)==0 && insight_index_count(loaded)==2);
 insight_index_free(clone); insight_index_free(loaded);
 for(size_t n=0;n<length;n++) rejected(data,n);
 rejected(data,(size_t)INSIGHT_INDEX_SNAPSHOT_LIMIT+1);
 unsigned char *bad=malloc(length+1); assert(bad);
 for(size_t n=0;n<length;n++) {
  memcpy(bad,data,length); bad[n]^=1; rejected(bad,length);
 }
 /* Unknown version with a valid checksum. */
 memcpy(bad,data,length); bad[7]=3; repair(bad,length); rejected(bad,length);
 /* Impossible record count, valid checksum: must fail before allocation. */
 memcpy(bad,data,length); memset(bad+8,255,4); repair(bad,length); rejected(bad,length);
 /* Oversized root string. */
 memcpy(bad,data,length); memset(bad+12,255,4); repair(bad,length); rejected(bad,length);
 /* Embedded NUL in metadata. */
 memcpy(bad,data,length); bad[16]=0; repair(bad,length); rejected(bad,length);
 /* A record with zero line / embedded NUL, despite a valid checksum. */
 size_t first_record=16+strlen("/проекты/код");
 memcpy(bad,data,length); memset(bad+first_record,0,8); repair(bad,length); rejected(bad,length);
 memcpy(bad,data,length); bad[first_record+12]=0; repair(bad,length); rejected(bad,length);
 memcpy(bad,data,length); bad[length]=0; rejected(bad,length+1);
 repair(bad,length+1); rejected(bad,length+1);
 free(bad); free(data); insight_index_free(i);
 i=insight_index_new(); assert(insight_index_serialize(i,"",&data,&length));
 loaded=insight_index_deserialize(data,length,&root);
 assert(loaded && insight_index_count(loaded)==0 && !strcmp(root,""));
 free(root); free(data); insight_index_free(loaded); insight_index_free(i);
 data=(unsigned char *)1; length=123;
 assert(!insight_index_serialize(NULL,"",&data,&length) && data==NULL && length==0);
 assert(!insight_index_clone(NULL));
 puts("snapshot: all checks passed"); return 0;
}
