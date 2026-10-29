/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "index.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(void) {
 InsightIndex *i=insight_index_new(); assert(i);
 assert(insight_index_exclusive(i)); InsightIndex *retained=insight_index_ref(i); assert(retained==i && !insight_index_exclusive(i)); insight_index_free(retained); assert(insight_index_exclusive(i));
 InsightSymbol a[]={ {"foo",NULL,"A","function",10,NULL,NULL,NULL},{"foobar",NULL,NULL,NULL,20,NULL,NULL,NULL} };
 InsightSymbol b[]={ {"foo",NULL,"B","function",3,NULL,NULL,NULL} };
 assert(insight_index_update(i,"a.cpp",a,2));
 assert(insight_index_update(i,"b.cpp",b,1));
 assert(insight_index_file_count(i)==2);
 assert(!insight_index_at(NULL,0)); assert(!insight_index_at(i,3));
 assert(!strcmp(insight_index_at(i,0)->path,"a.cpp")); assert(!strcmp(insight_index_at(i,1)->path,"b.cpp")); assert(!strcmp(insight_index_at(i,2)->name,"foobar"));
 const char *paths[2];
 assert(insight_index_files(i,"",1,paths,1)==2 && !strcmp(paths[0],"b.cpp"));
 assert(insight_index_files(i,"a",0,paths,2)==1 && !strcmp(paths[0],"a.cpp"));
 assert(insight_index_files(i,"zzz",0,NULL,0)==0);
 const InsightSymbol *out[5];
 assert(insight_index_find(i,"foo",1,out,5)==2);
 assert(strcmp(out[0]->path,"a.cpp")==0 && strcmp(out[1]->path,"b.cpp")==0);
 assert(insight_index_find(i,"foo",0,out,1)==3);
 assert(insight_index_find_filtered(i,"foo",0,"B","function",out,5)==1 && out[0]->line==3);
 assert(insight_index_find_filtered(i,"foo",0,NULL,"unknown",NULL,0)==0);
 assert(insight_index_find(i,"",0,NULL,0)==3);
 assert(insight_index_find(i,"zzz",0,out,5)==0);
 assert(insight_index_query(i,"foo",0,NULL,NULL,"a.cpp",1,out,5)==2 && !strcmp(out[0]->name,"foobar"));
 assert(insight_index_query(i,"foo",1,"B","function","a.cpp",0,out,5)==0);
 assert(insight_index_query(i,"foo",0,NULL,NULL,NULL,20,out,5)==3);
 InsightSymbol bad[]={ {NULL,NULL,NULL,NULL,1,NULL,NULL,NULL} };
 assert(!insight_index_update(i,"a.cpp",bad,1));
 assert(insight_index_count(i)==3);
 char name[]="new"; InsightSymbol c[]={ {name,NULL,NULL,NULL,8,NULL,NULL,NULL} };
 assert(insight_index_update(i,"a.cpp",c,1)); name[0]='X';
 assert(insight_index_find(i,"foo",1,out,5)==1 && out[0]->line==3);
 assert(insight_index_find(i,"new",1,out,5)==1);
 assert(insight_index_update(i,"b.cpp",NULL,0));
 assert(insight_index_count(i)==1);
 assert(insight_index_update(i,"a.cpp",NULL,0));
 assert(insight_index_count(i)==0);
 /* Repeated file replacement checked against a separate small model. */
 int model[12][8]={{0}};
 for(unsigned step=0;step<500;step++) {
  unsigned file=(step*7)%12, number=step%8;
  char path[32],names[8][16]; InsightSymbol batch[8];
  snprintf(path,sizeof path,"file-%u",file);
  for(unsigned k=0;k<8;k++) model[file][k]=0;
  for(unsigned k=0;k<number;k++) {
   unsigned name=(step+k)%8; model[file][name]=1;
   snprintf(names[k],sizeof names[k],"symbol-%u",name);
   batch[k]=(InsightSymbol){names[k],NULL,NULL,NULL,k+1,NULL,NULL,NULL};
  }
  assert(insight_index_update(i,path,batch,number));
  for(unsigned name=0;name<8;name++) {
   char query[16]; size_t expected=0;
   snprintf(query,sizeof query,"symbol-%u",name);
   for(unsigned f=0;f<12;f++) expected+=model[f][name];
   assert(insight_index_find(i,query,1,NULL,0)==expected);
  }
 }
 /* Borrowed input survives replacement/deletion; sorting must compare every field. */
 InsightIndex *merge=insight_index_new(); InsightSymbol left[]={ {"same",NULL,"z","function",4,"(z)","int","Base"}, {"alpha",NULL,"","function",1,NULL,NULL,NULL}, {"same",NULL,"a","function",4,"(x)","int","Base"} };
 InsightSymbol right[]={ {"zeta",NULL,"","function",7,NULL,NULL,NULL}, {"same",NULL,"","function",2,NULL,NULL,NULL}, {"beta",NULL,"","function",3,NULL,NULL,NULL} };
 assert(insight_index_update(merge,"a.c",left,3)); assert(insight_index_update(merge,"b.c",right,3));
 assert(!strcmp(insight_index_at(merge,0)->name,"alpha")); assert(!strcmp(insight_index_at(merge,1)->name,"beta")); assert(!strcmp(insight_index_at(merge,2)->scope,"a")); assert(!strcmp(insight_index_at(merge,3)->scope,"z")); assert(!strcmp(insight_index_at(merge,4)->path,"b.c"));
 const InsightSymbol *borrowed=insight_index_at(merge,2); assert(insight_index_update(merge,borrowed->path,borrowed,1)); assert(insight_index_count(merge)==4); assert(insight_index_find(merge,"same",1,out,5)==2); assert(!strcmp(out[0]->scope,"a"));
 borrowed=insight_index_at(merge,0); assert(insight_index_update(merge,borrowed->path,NULL,0)); assert(insight_index_count(merge)==1); assert(!strcmp(insight_index_at(merge,0)->path,"a.c")); insight_index_free(merge);
 /* Full-size snapshot traversal must use ordinal access, not repeated queries. */
 InsightIndex *large=insight_index_new(); size_t count=300000; InsightSymbol *rows=calloc(count,sizeof *rows); assert(large && rows);
 for(size_t n=0;n<count;n++) rows[n]=(InsightSymbol){"large_symbol",NULL,NULL,"function",n+1,NULL,NULL,NULL};
 assert(insight_index_update(large,"large.c",rows,count)); free(rows);
 for(size_t n=0;n<count;n++) { const InsightSymbol *symbol=insight_index_at(large,n); assert(symbol && symbol->line==n+1 && !strcmp(symbol->name,"large_symbol")); }
 assert(!insight_index_at(large,count));unsigned char *snapshot=NULL;size_t length=0;assert(insight_index_serialize(large,"/project",&snapshot,&length));char *root=NULL;InsightIndex *restored=insight_index_deserialize(snapshot,length,&root);assert(restored && insight_index_count(restored)==count);free(snapshot);free(root);insight_index_free(restored);insight_index_free(large);
 insight_index_free(i); insight_index_free(NULL);
 puts("index: all checks passed"); return 0;
}
