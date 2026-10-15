/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "relation_query.h"
#include <glib.h>
#include <stdint.h>
#include <string.h>
static int exclude_middle(const InsightSymbol *symbol,void *data) { (void)data;return strcmp(symbol->name,"Middle")!=0; }
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
 InsightSymbol chain[]={
  {"Root",NULL,"n","c",1,"","",""},
  {"Middle",NULL,"n","c",2,"","","Root"},
  {"End",NULL,"n","c",3,"","","Middle"},
  {"root_member",NULL,"n::Root","m",4,"","",""},
  {"middle_member",NULL,"n::Middle","m",5,"","",""},
  {"end_member",NULL,"n::End","m",6,"","",""},
  {"Root",NULL,"other","c",7,"","",""},
  {"LoopA",NULL,"n","c",8,"","","LoopB"},
  {"LoopB",NULL,"n","c",9,"","","LoopA"},
  {"Global",NULL,"","c",10,"","",""},
  {"Global",NULL,"n","c",11,"","",""},
  {"GlobalChild",NULL,"n","c",12,"","","::Global"}
 };
 g_assert_true(insight_index_update(index,"/src/chain.cpp",chain,G_N_ELEMENTS(chain)));
 int limited=0,ambiguous=0;
 g_assert_cmpuint(insight_relation_query_transitive(index,"n::End",INSIGHT_RELATION_BASES,32,NULL,NULL,0,out,8,&limited,&ambiguous),==,2);
 g_assert_false(limited);g_assert_false(ambiguous);
 g_assert_cmpuint(insight_relation_query_transitive(index,"n.End",INSIGHT_RELATION_MEMBERS,0,NULL,NULL,0,out,8,NULL,NULL),==,1);g_assert_cmpstr(out[0]->name,==,"end_member");
 g_assert_cmpuint(insight_relation_query_transitive(index,"n.End",INSIGHT_RELATION_MEMBERS,32,NULL,NULL,0,out,8,NULL,NULL),==,3);
 g_assert_cmpuint(insight_relation_query_transitive(index,"n::Root",INSIGHT_RELATION_DERIVED,32,NULL,NULL,0,out,8,NULL,NULL),==,2);
 g_assert_cmpuint(insight_relation_query_transitive(index,"n::End",INSIGHT_RELATION_BASES,1,NULL,NULL,0,out,8,&limited,NULL),==,1);g_assert_true(limited);
 g_assert_cmpuint(insight_relation_query_transitive(index,"Root",INSIGHT_RELATION_BASES,32,NULL,NULL,0,out,8,NULL,&ambiguous),==,0);g_assert_true(ambiguous);
 g_assert_cmpuint(insight_relation_query_transitive(index,"n::LoopA",INSIGHT_RELATION_BASES,32,NULL,NULL,0,out,8,&limited,NULL),==,1);g_assert_false(limited);
 g_assert_cmpuint(insight_relation_query_transitive(index,"n::GlobalChild",INSIGHT_RELATION_BASES,32,NULL,NULL,0,out,8,NULL,NULL),==,1);g_assert_cmpstr(out[0]->scope,==,"");
 g_assert_cmpuint(insight_relation_query_transitive(index,"n::End",INSIGHT_RELATION_MEMBERS,32,NULL,NULL,2,out,1,NULL,NULL),==,3);
 g_assert_cmpuint(insight_relation_query_transitive(index,"n::End",INSIGHT_RELATION_BASES,32,exclude_middle,NULL,0,out,8,NULL,NULL),==,0);
 InsightSymbol diamond[]={
  {"A",NULL,"diamond","c",1,"","",""},
  {"B",NULL,"diamond","c",2,"","","A"},
  {"C",NULL,"diamond","c",3,"","","A"},
  {"D",NULL,"diamond","c",4,"","","B, C"},
  {"common",NULL,"diamond.A","m",5,"","",""}
 };
 g_assert_true(insight_index_update(index,"/src/diamond.cpp",diamond,G_N_ELEMENTS(diamond)));
 g_assert_cmpuint(insight_relation_query_transitive(index,"diamond.D",INSIGHT_RELATION_BASES,32,NULL,NULL,0,out,8,NULL,NULL),==,3);
 g_assert_cmpuint(insight_relation_query_transitive(index,"diamond.D",INSIGHT_RELATION_MEMBERS,32,NULL,NULL,0,out,8,NULL,NULL),==,1);g_assert_cmpstr(out[0]->name,==,"common");
 g_assert_cmpuint(insight_relation_query_transitive(index,"diamond.A",INSIGHT_RELATION_DERIVED,32,NULL,NULL,0,out,8,NULL,NULL),==,3);
 g_assert_cmpuint(insight_relation_query_transitive(index,"n::End",INSIGHT_RELATION_BASES,33,NULL,NULL,0,out,8,NULL,NULL),==,SIZE_MAX);
 InsightSymbol aliases[]={
  {"Core",NULL,"aliases","c",1,"","",""},
  {"Value",NULL,"aliases::Core","m",2,"","int",""},
  {"First",NULL,"aliases","t",3,"","Core",""},
  {"Second",NULL,"aliases","t",4,"","First",""},
  {"Child",NULL,"aliases","c",5,"","","public virtual Second"},
  {"Leaf",NULL,"aliases","c",6,"","","Child"},
  {"LoopOne",NULL,"aliases","t",7,"","LoopTwo",""},
  {"LoopTwo",NULL,"aliases","t",8,"","LoopOne",""},
  {"LoopChild",NULL,"aliases","c",9,"","","LoopOne"},
  {"Qualified",NULL,"aliases","c",10,"","","public :: aliases :: Second <int>"},
  {"Choice",NULL,"aliases","u",11,"","",""},
  {"choice_member",NULL,"aliases::Choice","m",12,"","",""},
  {"Color",NULL,"aliases","g",13,"","",""},
  {"Red",NULL,"aliases::Color","e",14,"","",""},
  {"space",NULL,"aliases","n",15,"","",""},
  {"in_space",NULL,"aliases::space","v",16,"","",""}
 };
 g_assert_true(insight_index_update(index,"/src/aliases.cpp",aliases,G_N_ELEMENTS(aliases)));
 g_assert_cmpuint(insight_relation_query_transitive(index,"aliases::Leaf",INSIGHT_RELATION_BASES,32,NULL,NULL,0,out,8,&limited,&ambiguous),==,2);g_assert_false(limited);g_assert_false(ambiguous);
 g_assert_cmpuint(insight_relation_query_transitive(index,"aliases.Second",INSIGHT_RELATION_DERIVED,32,NULL,NULL,0,out,8,NULL,NULL),==,3);
 g_assert_cmpuint(insight_relation_query_transitive(index,"aliases::Second",INSIGHT_RELATION_MEMBERS,0,NULL,NULL,0,out,8,NULL,NULL),==,1);g_assert_cmpstr(out[0]->name,==,"Value");
 g_assert_cmpuint(insight_relation_query_transitive(index,"aliases::LoopChild",INSIGHT_RELATION_BASES,32,NULL,NULL,0,out,8,NULL,&ambiguous),==,0);g_assert_true(ambiguous);
 g_assert_cmpuint(insight_relation_query_transitive(index,"aliases::LoopOne",INSIGHT_RELATION_MEMBERS,0,NULL,NULL,0,out,8,NULL,&ambiguous),==,0);g_assert_true(ambiguous);
 g_assert_cmpuint(insight_relation_query_transitive(index,"aliases.Qualified",INSIGHT_RELATION_BASES,32,NULL,NULL,0,out,8,NULL,NULL),==,1);g_assert_cmpstr(out[0]->name,==,"Core");
 g_assert_cmpuint(insight_relation_query_transitive(index,"aliases.Choice",INSIGHT_RELATION_MEMBERS,0,NULL,NULL,0,out,8,NULL,NULL),==,1);g_assert_cmpstr(out[0]->name,==,"choice_member");
 g_assert_cmpuint(insight_relation_query_transitive(index,"aliases.Color",INSIGHT_RELATION_MEMBERS,0,NULL,NULL,0,out,8,NULL,NULL),==,1);g_assert_cmpstr(out[0]->name,==,"Red");
 g_assert_cmpuint(insight_relation_query_transitive(index,"aliases.space",INSIGHT_RELATION_MEMBERS,0,NULL,NULL,0,out,8,NULL,NULL),==,1);g_assert_cmpstr(out[0]->name,==,"in_space");
 InsightSymbol members[]={
  {"Root",NULL,"M","s",1,"","",""},
  {"Left",NULL,"M","s",2,"","","Root"},
  {"Right",NULL,"M","s",3,"","","Root"},
  {"Diamond",NULL,"M","s",4,"","","Left, Right"},
  {"Hidden",NULL,"M","s",5,"","","Root"},
  {"Clash",NULL,"M","s",6,"","","Hidden, Right"},
  {"Alias",NULL,"M","t",7,"","Diamond",""},
  {"Loop",NULL,"M","s",8,"","","Loop"},
  {"Missing",NULL,"M","s",9,"","","UnknownBase"},
  {"field",NULL,"M::Root","m",10,"","int",""},
  {"field",NULL,"M::Hidden","m",11,"","float",""},
  {"run",NULL,"M::Root","f",12,"()","void",""},
  {"run",NULL,"M::Root","f",13,"(int)","void",""},
  {"value",NULL,"implicit::namespace","v",14,"","int",""},
  {"global_member",NULL,"","v",15,"","int",""}
 };g_assert_true(insight_index_update(index,"/members.cpp",members,G_N_ELEMENTS(members)));
 g_assert_cmpuint(insight_member_query(index,"M::Alias","field","/members.cpp",NULL,NULL,out,8,&limited,&ambiguous),==,1);g_assert_false(limited);g_assert_false(ambiguous);g_assert_cmpstr(out[0]->scope,==,"M::Root");
 g_assert_cmpuint(insight_member_query(index,"M::Hidden","field","/members.cpp",NULL,NULL,out,8,NULL,NULL),==,1);g_assert_cmpstr(out[0]->scope,==,"M::Hidden");
 g_assert_cmpuint(insight_member_query(index,"M::Clash","field","/members.cpp",NULL,NULL,out,8,NULL,&ambiguous),==,0);g_assert_true(ambiguous);
 g_assert_cmpuint(insight_member_query(index,"M::Diamond","run","/members.cpp",NULL,NULL,out,1,NULL,NULL),==,2);g_assert_cmpstr(out[0]->scope,==,"M::Root");
 g_assert_cmpuint(insight_member_query(index,"M::Loop","field","/members.cpp",NULL,NULL,out,8,NULL,&ambiguous),==,0);g_assert_true(ambiguous);
 g_assert_cmpuint(insight_member_query(index,"M::Missing","field","/members.cpp",NULL,NULL,out,8,NULL,&ambiguous),==,0);g_assert_true(ambiguous);
 g_assert_cmpuint(insight_member_query(index,"implicit.namespace","value","/members.cpp",NULL,NULL,out,8,NULL,NULL),==,1);
 g_assert_cmpuint(insight_member_query(index,"","global_member","/members.cpp",NULL,NULL,out,8,NULL,NULL),==,1);
 InsightSymbol deep[35];gchar *deep_names[35],*deep_bases[35];
 for(guint i=0;i<35;i++) { deep_names[i]=g_strdup_printf("Depth%02u",i);deep_bases[i]=i<34 ? g_strdup_printf("Depth%02u",i+1) : g_strdup("");deep[i]=(InsightSymbol){deep_names[i],NULL,"M","s",i+1,"","",deep_bases[i]}; }
 g_assert_true(insight_index_update(index,"/deep.cpp",deep,G_N_ELEMENTS(deep)));for(guint i=0;i<35;i++) { g_free(deep_names[i]);g_free(deep_bases[i]); }
 InsightSymbol deep_field={"deep_field",NULL,"M::Depth34","m",1,"","int",""};g_assert_true(insight_index_update(index,"/deep-field.cpp",&deep_field,1));
 g_assert_cmpuint(insight_member_query(index,"M::Depth00","deep_field","/deep.cpp",NULL,NULL,out,8,&limited,NULL),==,0);g_assert_true(limited);
 InsightSymbol *crowded=g_new0(InsightSymbol,4097);for(guint i=0;i<4097;i++) crowded[i]=(InsightSymbol){"common",NULL,"M::Root","m",i+1,"","int",""};
 g_assert_true(insight_index_update(index,"/crowded.cpp",crowded,4097));g_free(crowded);
 g_assert_cmpuint(insight_member_query(index,"M::Root","common","/members.cpp",NULL,NULL,out,8,&limited,NULL),==,0);g_assert_true(limited);
 insight_index_free(index);g_print("Relation query checks passed\n");return 0;
}
