/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "type_resolve.h"
#include <string.h>
static void name_is(const char *raw,const char *expected,gboolean global) { gboolean absolute=FALSE;gchar *name=insight_type_name(raw,&absolute);g_assert_cmpstr(name,==,expected);g_assert_cmpint(absolute,==,global);g_free(name); }
static void resolved(InsightIndex *index,const char *raw,const char *scope,const char *path,const char *owner,guint aliases) { InsightTypeResult r=insight_type_resolve(index,raw,scope,path,NULL,NULL);g_assert_cmpint(r.status,==,INSIGHT_TYPE_FOUND);g_assert_cmpstr(r.owner,==,owner);g_assert_nonnull(r.symbol);g_assert_cmpuint(r.aliases,==,aliases);insight_type_result_clear(&r); }
static int exclude_nested(const InsightSymbol *symbol,void *data) { (void)data;return strcmp(symbol->path,"/nested.h")!=0; }
static int exclude_header(const InsightSymbol *symbol,void *data) { (void)data;return strcmp(symbol->path,"/header.h")!=0; }
static int exclude_base(const InsightSymbol *symbol,void *data) { (void)data;return strcmp(symbol->name,"Base")!=0; }
static void inherited_types(void) {
 InsightIndex *index=insight_index_new();InsightSymbol tags[]={
  {"Base",NULL,"N","s",1,"","",""}, {"Nested",NULL,"N::Base","s",2,"","",""},
  {"BaseAlias",NULL,"N","t",3,"","Base",""}, {"Derived",NULL,"N","s",4,"","","public virtual BaseAlias"}, {"Alias",NULL,"N","t",5,"","Derived",""},
  {"Left",NULL,"N","s",6,"","","Base"}, {"Right",NULL,"N","s",7,"","","::N::Base"}, {"Diamond",NULL,"N","s",8,"","","Left,Right"},
  {"Other",NULL,"N","s",9,"","",""}, {"Nested",NULL,"N::Other","s",10,"","",""}, {"Ambiguous",NULL,"N","s",11,"","","Base,Other"},
  {"Direct",NULL,"N","s",12,"","","Missing"}, {"Nested",NULL,"N::Direct","s",13,"","",""},
  {"MissingBase",NULL,"N","s",14,"","","Base,Missing"}, {"CycleA",NULL,"N","s",15,"","","CycleB"}, {"CycleB",NULL,"N","s",16,"","","CycleA"},
  {"Hider",NULL,"N","s",17,"","","Base"}, {"Nested",NULL,"N::Hider","m",18,"","int",""}, {"Mixed",NULL,"N","s",19,"","","Hider,Base"},
  {"Malformed",NULL,"N","s",20,"","","Base<"}
 };g_assert_true(insight_index_update(index,"/inherited.cpp",tags,G_N_ELEMENTS(tags)));
 InsightSymbol extra[]={ {"Empty",NULL,"N","s",201,"","",""},{"NoNested",NULL,"N","s",202,"","","Empty"},{"Nested",NULL,"N::Derived::special","t",203,"","Other::Nested",""} };g_assert_true(insight_index_update(index,"/lexical.h",extra,G_N_ELEMENTS(extra)));
 InsightSymbol global={"Nested",NULL,"","s",200,"","",""};g_assert_true(insight_index_update(index,"/global.h",&global,1));
 resolved(index,"Nested","N::Derived::method","/inherited.cpp","N::Base::Nested",1);
 resolved(index,"Nested","N::Direct::method","/inherited.cpp","N::Direct::Nested",0);
 resolved(index,"Nested","N::NoNested::method","/inherited.cpp","Nested",0);resolved(index,"Nested","N::Derived::special","/inherited.cpp","N::Other::Nested",1);
 InsightTypeResult blocked=insight_type_resolve(index,"Nested","N::Hider::method","/inherited.cpp",NULL,NULL);g_assert_cmpint(blocked.status,==,INSIGHT_TYPE_UNKNOWN);insight_type_result_clear(&blocked);blocked=insight_type_resolve(index,"Nested","N::MissingBase::method","/inherited.cpp",NULL,NULL);g_assert_cmpint(blocked.status,==,INSIGHT_TYPE_UNKNOWN);insight_type_result_clear(&blocked);
 resolved(index,"Derived::Nested","N","/inherited.cpp","N::Base::Nested",1);resolved(index,"Alias::Nested","N","/inherited.cpp","N::Base::Nested",2);resolved(index,"Diamond::Nested","N","/inherited.cpp","N::Base::Nested",0);resolved(index,"Direct::Nested","N","/inherited.cpp","N::Direct::Nested",0);
 const gchar *missing[]={"Ambiguous::Nested","MissingBase::Nested","CycleA::Nested","Hider::Nested","Mixed::Nested","Malformed::Nested"};const InsightTypeStatus states[]={INSIGHT_TYPE_AMBIGUOUS,INSIGHT_TYPE_UNKNOWN,INSIGHT_TYPE_CYCLE,INSIGHT_TYPE_UNKNOWN,INSIGHT_TYPE_UNKNOWN,INSIGHT_TYPE_INVALID};
 for(guint i=0;i<G_N_ELEMENTS(missing);i++) { InsightTypeResult r=insight_type_resolve(index,missing[i],"N","/inherited.cpp",NULL,NULL);g_assert_cmpint(r.status,==,states[i]);g_assert_null(r.owner);insight_type_result_clear(&r); }
 InsightTypeResult filtered=insight_type_resolve(index,"Derived::Nested","N","/inherited.cpp",exclude_base,NULL);g_assert_cmpint(filtered.status,==,INSIGHT_TYPE_UNKNOWN);insight_type_result_clear(&filtered);
 for(guint i=0;i<13;i++) { gchar *bases=i ? g_strdup_printf("WideL%u,WideR%u",i-1,i-1) : g_strdup("Base");gchar *left=g_strdup_printf("WideL%u",i),*right=g_strdup_printf("WideR%u",i),*path=g_strdup_printf("/wide%u.h",i);InsightSymbol rows[]={{left,NULL,"N","s",30+i,"","",bases},{right,NULL,"N","s",60+i,"","",bases}};g_assert_true(insight_index_update(index,path,rows,2));g_free(bases);g_free(left);g_free(right);g_free(path); }
 filtered=insight_type_resolve(index,"WideL12::Nested","N","/inherited.cpp",NULL,NULL);g_assert_cmpint(filtered.status,==,INSIGHT_TYPE_LIMIT);insight_type_result_clear(&filtered);
 gchar *previous=g_strdup("Base");for(guint i=0;i<80;i++) { gchar *name=g_strdup_printf("Deep%u",i);InsightSymbol row={name,NULL,"N","s",100+i,"","",previous};gchar *path=g_strdup_printf("/deep%u.h",i);g_assert_true(insight_index_update(index,path,&row,1));g_free(path);g_free(previous);previous=name; }
 gchar *deep=g_strconcat(previous,"::Nested",NULL);InsightTypeResult limited=insight_type_resolve(index,deep,"N","/inherited.cpp",NULL,NULL);g_assert_cmpint(limited.status,==,INSIGHT_TYPE_LIMIT);insight_type_result_clear(&limited);g_free(deep);g_free(previous);
 insight_index_free(index);
}
static void qualified_precedence(void) {
 InsightIndex *index=insight_index_new();InsightSymbol tags[]={
  {"Pick",NULL,"","n",1,"","",""},{"Nested",NULL,"Pick","s",2,"","",""},
  {"Base",NULL,"N","s",3,"","",""},{"Pick",NULL,"N::Base","s",4,"","",""},{"Nested",NULL,"N::Base::Pick","s",5,"","",""},{"Derived",NULL,"N","s",6,"","","Base"},
  {"Other",NULL,"N","s",7,"","",""},{"Nested",NULL,"N::Other","s",8,"","",""},{"Pick",NULL,"N::Derived::special","t",9,"","Other",""},
  {"EmptyBase",NULL,"N","s",10,"","",""},{"Pick",NULL,"N::EmptyBase","s",11,"","",""},{"EmptyDerived",NULL,"N","s",12,"","","EmptyBase"},
  {"Ambiguous",NULL,"N","s",13,"","","Base,EmptyBase"},{"Hider",NULL,"N","s",14,"","","Base"},{"Pick",NULL,"N::Hider","m",15,"","int",""},{"Pick",NULL,"N::Derived::value","z",16,"","int",""}
 };g_assert_true(insight_index_update(index,"/qualified.cpp",tags,G_N_ELEMENTS(tags)));
 resolved(index,"Pick::Nested","N::Derived::method","/qualified.cpp","N::Base::Pick::Nested",0);resolved(index,"::Pick::Nested","N::Derived::method","/qualified.cpp","Pick::Nested",0);resolved(index,"Pick::Nested","N::Derived::special","/qualified.cpp","N::Other::Nested",1);
 const gchar *scopes[]={"N::EmptyDerived::method","N::Ambiguous::method","N::Hider::method","N::Derived::value"};const InsightTypeStatus states[]={INSIGHT_TYPE_UNKNOWN,INSIGHT_TYPE_AMBIGUOUS,INSIGHT_TYPE_UNKNOWN,INSIGHT_TYPE_UNKNOWN};for(guint i=0;i<G_N_ELEMENTS(scopes);i++) { InsightTypeResult r=insight_type_resolve(index,"Pick::Nested",scopes[i],"/qualified.cpp",NULL,NULL);g_assert_cmpint(r.status,==,states[i]);g_assert_null(r.owner);insight_type_result_clear(&r); }
 InsightSymbol reopen={"Pick",NULL,"","n",20,"","",""};g_assert_true(insight_index_update(index,"/namespace-one.h",&reopen,1));g_assert_true(insight_index_update(index,"/namespace-two.h",&reopen,1));resolved(index,"::Pick::Nested","N::Derived","/consumer.cpp","Pick::Nested",0);
 InsightSymbol collision={"Pick",NULL,"","c",22,"","",""};g_assert_true(insight_index_update(index,"/namespace-conflict.h",&collision,1));InsightTypeResult mixed=insight_type_resolve(index,"::Pick::Nested","","/consumer.cpp",NULL,NULL);g_assert_cmpint(mixed.status,==,INSIGHT_TYPE_AMBIGUOUS);insight_type_result_clear(&mixed);g_assert_true(insight_index_update(index,"/namespace-conflict.h",NULL,0));
 InsightSymbol ns={"N",NULL,"","n",21,"","",""};g_assert_true(insight_index_update(index,"/namespace-n-one.h",&ns,1));g_assert_true(insight_index_update(index,"/namespace-n-two.h",&ns,1));resolved(index,"N::Base::Pick::Nested","","/consumer.cpp","N::Base::Pick::Nested",0);
 InsightSymbol work[35];gchar *names[33];GString *bases=g_string_new(NULL);work[0]=(InsightSymbol){"WorkEmpty",NULL,"N","s",30,"","",""};
 for(guint i=0;i<33;i++) { names[i]=g_strdup_printf("WorkAlias%u",i);work[i+1]=(InsightSymbol){names[i],NULL,"N","t",40+i,"","WorkEmpty",""};if(i) g_string_append_c(bases,',');g_string_append(bases,names[i]); }
 work[34]=(InsightSymbol){"WorkDerived",NULL,"N","s",80,"","",bases->str};g_assert_true(insight_index_update(index,"/work.h",work,G_N_ELEMENTS(work)));for(guint i=0;i<33;i++) g_free(names[i]);g_string_free(bases,TRUE);
 InsightTypeResult capped=insight_type_resolve(index,"Pick::Nested","N::WorkDerived::method","/qualified.cpp",NULL,NULL);g_assert_cmpint(capped.status,==,INSIGHT_TYPE_LIMIT);g_assert_cmpuint(capped.aliases,==,0);insight_type_result_clear(&capped);
 insight_index_free(index);
}
static void dependent_aliases(void) {
 InsightIndex *index=insight_index_new();InsightSymbol tags[]={
  {"T",NULL,"","s",1,"","",""},{"Dependent",NULL,"","t",2,"","T",""},{"T",NULL,"Dependent","Z",2,"","class",""},
  {"Qualified",NULL,"","t",3,"","T::Nested",""},{"T",NULL,"Qualified","Z",3,"","class",""},
  {"Concrete",NULL,"","t",4,"","::T",""},{"T",NULL,"Concrete","Z",4,"","class",""},{"Ordinary",NULL,"","t",5,"","T",""}
 };g_assert_true(insight_index_update(index,"/alias.cpp",tags,G_N_ELEMENTS(tags)));
 const gchar *names[]={"Dependent<int>","Qualified<int>"};for(guint i=0;i<G_N_ELEMENTS(names);i++) { InsightTypeResult r=insight_type_resolve(index,names[i],"","/alias.cpp",NULL,NULL);g_assert_cmpint(r.status,==,INSIGHT_TYPE_UNKNOWN);g_assert_null(r.owner);insight_type_result_clear(&r); }
 resolved(index,"Concrete<int>","","/alias.cpp","T",1);resolved(index,"Ordinary","","/alias.cpp","T",1);insight_index_free(index);
}
int main(void) {
 inherited_types();qualified_precedence();dependent_aliases();
 name_is("const struct N::Box<int, Pair<long, short>>::Nested * const &","N::Box::Nested",FALSE);
 name_is("volatile ::Global *","Global",TRUE);name_is("N :: Foo [10][2]","N::Foo",FALSE);name_is("Module.Class","Module::Class",FALSE);
 const char *invalid[]={"","Foo Bar","Foo<int","Foo>","Foo[2","Foo (*)(int)","decltype(value)","A:::B","A::","Foo;Bar"};
 for(guint i=0;i<G_N_ELEMENTS(invalid);i++) { gchar *name=insight_type_name(invalid[i],NULL);g_assert_null(name); }
 InsightIndex *index=insight_index_new();InsightSymbol tags[]={
  {"Foo",NULL,"","s",1,"","",""},
  {"Foo",NULL,"N","c",2,"","",""},
  {"Foo",NULL,"Other","c",3,"","",""},
  {"A",NULL,"N","t",4,"","B *",""},
  {"B",NULL,"N","t",5,"","Foo const *",""},
  {"A",NULL,"Other","t",6,"","::Foo",""},
  {"GlobalAlias",NULL,"N","t",7,"","::Foo",""},
  {"LoopA",NULL,"N","t",8,"","LoopB",""},
  {"LoopB",NULL,"N","t",9,"","LoopA",""},
  {"C",NULL,"N","t",10,"","NoSuchType",""},
  {"Box",NULL,"N","c",11,"","",""},
  {"Nested",NULL,"N::Box","c",12,"","",""},
  {"Bad",NULL,"N","t",13,"","void (*)(int)",""}
 };g_assert_true(insight_index_update(index,"/main.cpp",tags,G_N_ELEMENTS(tags)));
 resolved(index,"A","N::function","/main.cpp","N::Foo",2);resolved(index,"Other::A","N::function","/main.cpp","Foo",1);
 resolved(index,"Foo","N::Box::method","/main.cpp","N::Foo",0);resolved(index,"::Foo","N::function","/main.cpp","Foo",0);
 resolved(index,"GlobalAlias","N","/main.cpp","Foo",1);resolved(index,"N::Box<int>::Nested","Other","/main.cpp","N::Box::Nested",0);
 InsightSymbol nested[]={
  {"BoxAlias",NULL,"N","t",14,"","Box",""},
  {"SecondAlias",NULL,"N","t",15,"","BoxAlias",""},
  {"NamespaceAlias",NULL,"","t",16,"","N",""},
  {"N",NULL,"","n",17,"","",""},
  {"NestedAlias",NULL,"N::Box","t",18,"","Nested",""},
  {"Cross",NULL,"N::Box","t",19,"","N::BoxAlias::Nested",""},
  {"Recursive",NULL,"N","t",20,"","Recursive::Nested",""}
 };g_assert_true(insight_index_update(index,"/nested.h",nested,G_N_ELEMENTS(nested)));
 resolved(index,"BoxAlias::Nested","N::function","/main.cpp","N::Box::Nested",1);
 resolved(index,"SecondAlias::NestedAlias","N","/main.cpp","N::Box::Nested",3);
 resolved(index,"NamespaceAlias::BoxAlias::Nested","","/main.cpp","N::Box::Nested",2);
 resolved(index,"BoxAlias::Cross","N","/main.cpp","N::Box::Nested",3);
 InsightTypeResult nested_result=insight_type_resolve(index,"Recursive::Nested","N","/main.cpp",NULL,NULL);g_assert_cmpint(nested_result.status,==,INSIGHT_TYPE_CYCLE);insight_type_result_clear(&nested_result);
 InsightTypeResult missing=insight_type_resolve(index,"BoxAlias::Missing","N","/main.cpp",NULL,NULL);g_assert_cmpint(missing.status,==,INSIGHT_TYPE_UNKNOWN);g_assert_cmpuint(missing.aliases,==,1);insight_type_result_clear(&missing);
 missing=insight_type_resolve(index,"BoxAlias::Nested","N","/main.cpp",exclude_nested,NULL);g_assert_cmpint(missing.status,==,INSIGHT_TYPE_UNKNOWN);insight_type_result_clear(&missing);
 InsightSymbol conflict={"BoxAlias",NULL,"N","t",1,"","::Foo",""};g_assert_true(insight_index_update(index,"/conflict.h",&conflict,1));
 missing=insight_type_resolve(index,"BoxAlias::Nested","N","/unrelated.cpp",NULL,NULL);g_assert_cmpint(missing.status,==,INSIGHT_TYPE_AMBIGUOUS);insight_type_result_clear(&missing);g_assert_true(insight_index_update(index,"/conflict.h",NULL,0));
 GString *deep=g_string_new("Unknown");for(guint i=0;i<80;i++) g_string_append(deep,"::Nested");missing=insight_type_resolve(index,deep->str,"N","/main.cpp",NULL,NULL);g_assert_cmpint(missing.status,==,INSIGHT_TYPE_LIMIT);insight_type_result_clear(&missing);g_string_free(deep,TRUE);
 InsightTypeResult r=insight_type_resolve(index,"LoopA","N","/main.cpp",NULL,NULL);g_assert_cmpint(r.status,==,INSIGHT_TYPE_CYCLE);insight_type_result_clear(&r);
 r=insight_type_resolve(index,"C","N","/main.cpp",NULL,NULL);g_assert_cmpint(r.status,==,INSIGHT_TYPE_UNKNOWN);insight_type_result_clear(&r);
 r=insight_type_resolve(index,"Bad","N","/main.cpp",NULL,NULL);g_assert_cmpint(r.status,==,INSIGHT_TYPE_INVALID);insight_type_result_clear(&r);
 InsightSymbol duplicate={"Foo",NULL,"N","c",1,"","",""};g_assert_true(insight_index_update(index,"/other.cpp",&duplicate,1));
 resolved(index,"N::Foo","","/main.cpp","N::Foo",0);
 r=insight_type_resolve(index,"N::Foo","","/unrelated.cpp",NULL,NULL);g_assert_cmpint(r.status,==,INSIGHT_TYPE_AMBIGUOUS);insight_type_result_clear(&r);
 InsightSymbol alias={"External",NULL,"","t",1,"","N::Foo",""};g_assert_true(insight_index_update(index,"/header.h",&alias,1));
 r=insight_type_resolve(index,"External","","/main.cpp",NULL,NULL);g_assert_cmpint(r.status,==,INSIGHT_TYPE_AMBIGUOUS);insight_type_result_clear(&r);
 r=insight_type_resolve(index,"External","","/main.cpp",exclude_header,NULL);g_assert_cmpint(r.status,==,INSIGHT_TYPE_UNKNOWN);insight_type_result_clear(&r);
 /* Alias lookup context follows its declaration, not the use-site namespace. */
 g_assert_true(insight_index_update(index,"/other.cpp",NULL,0));resolved(index,"External","Other::function","/main.cpp","N::Foo",1);
 InsightSymbol chain[34];gchar *names[34],*targets[34];for(guint i=0;i<34;i++) { names[i]=g_strdup_printf("Link%02u",i);targets[i]=i<33 ? g_strdup_printf("Link%02u",i+1) : g_strdup("::Foo");chain[i]=(InsightSymbol){names[i],NULL,"chain","t",i+1,"",targets[i],""}; }
 g_assert_true(insight_index_update(index,"/chain.h",chain,G_N_ELEMENTS(chain)));for(guint i=0;i<34;i++) { g_free(names[i]);g_free(targets[i]); }
 r=insight_type_resolve(index,"Link00","chain","/main.cpp",NULL,NULL);g_assert_cmpint(r.status,==,INSIGHT_TYPE_LIMIT);g_assert_cmpuint(r.aliases,==,32);insight_type_result_clear(&r);
 insight_index_free(index);g_print("Type resolution: nominal normalization, lexical namespaces, alias declaration context, ambiguity, eligibility, cycles and limits passed\n");return 0;
}
