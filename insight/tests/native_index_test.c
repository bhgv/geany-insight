/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-index.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 InsightIndex *index=insight_index_new(); assert(index);
 char name[]="foo"; TMTag function={.name=name,.scope="Scope",.line=7,.kind_letter='f'};
 TMTag invalid={.name="\xff",.line=3,.kind_letter='v'};
 GPtrArray *tags=g_ptr_array_new(); g_ptr_array_add(tags,&function); g_ptr_array_add(tags,&invalid);
 TMSourceFile source={.tags_array=tags};
 assert(insight_native_import(index,&source,"/project/a.cpp")); name[0]='X';
 const InsightSymbol *found[2]; assert(insight_index_find(index,"foo",1,found,2)==1);
 assert(found[0]->line==7 && !strcmp(found[0]->scope,"Scope"));
 assert(insight_native_definition_rank(found[0],"/project/a.cpp")==6);
 assert(insight_native_definition_rank(found[0],"/project/b.cpp")==2);
 assert(!insight_native_import(index,&source,"\xff")); assert(insight_index_count(index)==1);
 GPtrArray huge={.pdata=NULL,.len=250001}; source.tags_array=&huge;
 assert(!insight_native_import(index,&source,"/project/a.cpp")); assert(insight_index_count(index)==1);
 source.tags_array=tags; g_ptr_array_set_size(tags,0);
 assert(insight_native_import(index,&source,"/project/a.cpp") && insight_index_count(index)==0);
 assert(insight_native_import(index,NULL,"/project/a.cpp"));
 g_ptr_array_free(tags,TRUE); insight_index_free(index);
 puts("native index: all checks passed"); return 0;
}
