/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relation_query.h"
#include <glib.h>
#include <stdint.h>
int main(void) {
 InsightIndex *index=insight_index_new();
 InsightSymbol tags[]={
  {"Widget","/src/a.cpp","","c",1},
  {"run","/src/a.cpp","Widget","f",3},
  {"value","/src/a.cpp","ns::Widget","v",4},
  {"other","/src/a.cpp","NotWidget","v",5},
  {"nested","/src/a.cpp","Widget::Nested","v",6},
  {"python_member","/src/a.cpp","ns.Widget","m",7}
 };
 g_assert_true(insight_index_update(index,"/src/a.cpp",tags,G_N_ELEMENTS(tags)));
 const InsightSymbol *out[8]; size_t total=insight_relation_query(index,"Widget",INSIGHT_RELATION_MEMBERS,0,out,8);g_assert_cmpuint(total,==,3);
 for(size_t i=0;i<total;i++) { g_assert_cmpstr(out[i]->name,!=,"other");g_assert_cmpstr(out[i]->name,!=,"nested"); }
 g_assert_cmpuint(insight_relation_query(index,"Widget",INSIGHT_RELATION_MEMBERS,2,out,1),==,3);
 g_assert_cmpuint(insight_relation_query(index,"ns::Widget",INSIGHT_RELATION_MEMBERS,0,out,8),==,1);g_assert_cmpstr(out[0]->name,==,"value");
 g_assert_cmpuint(insight_relation_query(index,"Widget",INSIGHT_RELATION_DEFINITIONS,0,out,8),==,1);g_assert_cmpuint(out[0]->line,==,1);
 g_assert_cmpuint(insight_relation_query(index,"missing",INSIGHT_RELATION_MEMBERS,0,out,8),==,0);
 g_assert_cmpuint(insight_relation_query(index,"",INSIGHT_RELATION_MEMBERS,0,out,8),==,SIZE_MAX);
 insight_index_free(index);g_print("Relation query checks passed\n");return 0;
}
