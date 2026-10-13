/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relation_query.h"
#include <glib.h>
#include <stdint.h>
int main(void) {
 InsightIndex *index=insight_index_new();
 InsightSymbol tags[]={
  {"Widget","/src/a.cpp","","c",1,NULL,NULL,NULL},
  {"run","/src/a.cpp","Widget","f",3,NULL,NULL,NULL},
  {"value","/src/a.cpp","ns::Widget","v",4,NULL,NULL,NULL},
  {"other","/src/a.cpp","NotWidget","v",5,NULL,NULL,NULL},
  {"nested","/src/a.cpp","Widget::Nested","v",6,NULL,NULL,NULL},
  {"python_member","/src/a.cpp","ns.Widget","m",7,NULL,NULL,NULL}
 };
 g_assert_true(insight_index_update(index,"/src/a.cpp",tags,G_N_ELEMENTS(tags)));
 const InsightSymbol *out[8]; size_t total=insight_relation_query(index,"Widget",INSIGHT_RELATION_MEMBERS,0,out,8);g_assert_cmpuint(total,==,3);
 for(size_t i=0;i<total;i++) { g_assert_cmpstr(out[i]->name,!=,"other");g_assert_cmpstr(out[i]->name,!=,"nested"); }
 g_assert_cmpuint(insight_relation_query(index,"Widget",INSIGHT_RELATION_MEMBERS,2,out,1),==,3);
 g_assert_cmpuint(insight_relation_query(index,"ns::Widget",INSIGHT_RELATION_MEMBERS,0,out,8),==,1);g_assert_cmpstr(out[0]->name,==,"value");
 g_assert_cmpuint(insight_relation_query(index,"Widget",INSIGHT_RELATION_DEFINITIONS,0,out,8),==,1);g_assert_cmpuint(out[0]->line,==,1);
 g_assert_cmpuint(insight_relation_query(index,"missing",INSIGHT_RELATION_MEMBERS,0,out,8),==,0);
 g_assert_cmpuint(insight_relation_query(index,"",INSIGHT_RELATION_MEMBERS,0,out,8),==,SIZE_MAX);
 InsightSymbol hierarchy[]={
  {"Base",NULL,"ns","c",10,"","",""},
  {"Other",NULL,"ns","i",11,"","",""},
  {"Derived",NULL,"ns","c",12,"","","public Base<int, long>, ns::Other"},
  {"Leaf",NULL,"ns","c",13,"","","Derived"},
  {"NotDerived",NULL,"elsewhere","c",14,"","","NotBase"},
  {"Nested",NULL,"ns::Base","c",15,"","",""},
  {"NestedChild",NULL,"ns","c",16,"","","ns::Base<int>::Nested"}
 };
 g_assert_true(insight_index_update(index,"/src/types.cpp",hierarchy,G_N_ELEMENTS(hierarchy)));
 g_assert_cmpuint(insight_relation_query(index,"ns::Derived",INSIGHT_RELATION_BASES,0,out,8),==,2);
 g_assert_cmpuint(insight_relation_query(index,"Base",INSIGHT_RELATION_DERIVED,0,out,8),==,1);g_assert_cmpstr(out[0]->name,==,"Derived");
 g_assert_cmpuint(insight_relation_query(index,"ns::Base",INSIGHT_RELATION_DERIVED,0,out,8),==,1);
 g_assert_cmpuint(insight_relation_query(index,"Derived",INSIGHT_RELATION_DERIVED,0,out,8),==,1);g_assert_cmpstr(out[0]->name,==,"Leaf");
 g_assert_cmpuint(insight_relation_query(index,"NotBase",INSIGHT_RELATION_DERIVED,0,out,8),==,1);
 g_assert_cmpuint(insight_relation_query(index,"ns::Derived",INSIGHT_RELATION_BASES,1,out,1),==,2);
 g_assert_cmpuint(insight_relation_query(index,"NestedChild",INSIGHT_RELATION_BASES,0,out,8),==,1);g_assert_cmpstr(out[0]->name,==,"Nested");
 g_assert_cmpuint(insight_relation_query(index,"::ns::Base",INSIGHT_RELATION_DERIVED,0,out,8),==,1);
 insight_index_free(index);g_print("Relation query checks passed\n");return 0;
}
