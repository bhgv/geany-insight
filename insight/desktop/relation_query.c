/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relation_query.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static int owns_scope(const char *scope,const char *owner) {
 if(!scope || !owner) return 0;
 size_t n=strlen(scope),length=strlen(owner);
 if(n<length || strcmp(scope+n-length,owner)) return 0;
 if(n==length) return 1;
 size_t before=n-length;
 return scope[before-1]=='.' || (before>=2 && scope[before-1]==':' && scope[before-2]==':');
}
size_t insight_relation_query(const InsightIndex *index,const char *owner,InsightRelationMode mode,size_t offset,const InsightSymbol **out,size_t capacity) {
 if(!index || !owner || !*owner || strlen(owner)>1024 || (mode!=INSIGHT_RELATION_DEFINITIONS && mode!=INSIGHT_RELATION_MEMBERS) || (capacity && !out)) return SIZE_MAX;
 if(mode==INSIGHT_RELATION_DEFINITIONS) return insight_index_query(index,owner,1,NULL,NULL,NULL,offset,out,capacity);
 size_t total=0,count=insight_index_count(index);
 if(!count) return 0;
 if(count>SIZE_MAX/sizeof(const InsightSymbol *)) return SIZE_MAX;
 const InsightSymbol **candidates=malloc(count*sizeof *candidates);
 if(!candidates) return SIZE_MAX;
 /* Query once: paginating index_query repeatedly would rescan the whole
  * project to compute its total, causing quadratic work on large projects. */
 size_t found=insight_index_query(index,"",0,owner,NULL,NULL,0,candidates,count);
 if(found==SIZE_MAX || found>count) { free(candidates); return SIZE_MAX; }
 for(size_t i=0;i<found;i++) {
  const InsightSymbol *symbol=candidates[i];
  if(!owns_scope(symbol->scope,owner)) continue;
  if(total>=offset && total-offset<capacity) out[total-offset]=symbol;
  total++;
 }
 free(candidates);
 return total;
}
