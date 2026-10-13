/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "index.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 InsightIndex *i=insight_index_new(); assert(i);
 InsightSymbol a[]={ {"foo",NULL,"A","function",10,NULL,NULL,NULL},{"foobar",NULL,NULL,NULL,20,NULL,NULL,NULL} };
 InsightSymbol b[]={ {"foo",NULL,"B","function",3,NULL,NULL,NULL} };
 assert(insight_index_update(i,"a.cpp",a,2));
 assert(insight_index_update(i,"b.cpp",b,1));
 assert(insight_index_file_count(i)==2);
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
 insight_index_free(i); insight_index_free(NULL);
 puts("index: all checks passed"); return 0;
}
