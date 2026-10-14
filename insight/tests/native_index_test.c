/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-index.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/tagmanager/tm_ctags.h"
#include "../../src/tagmanager/tm_workspace.h"
#include <glib/gstdio.h>
typedef struct { TMSourceFile *source; const char *path; InsightIndex *shared; GMutex *mutex; GCond *ready; guint *waiting; gboolean *start; } ParserThread;
static gpointer parse_in_worker(gpointer data) {
 ParserThread *job=data; g_mutex_lock(job->mutex); (*job->waiting)++; g_cond_broadcast(job->ready); while(!*job->start) g_cond_wait(job->ready,job->mutex); g_mutex_unlock(job->mutex);
 InsightIndex *index=insight_index_new(); const char *code="class Parallel { public: int run(int x) { return x; } };\nint concurrent_function(void) { return 1; }\n";
 for(guint n=0;n<50;n++) {
  assert(insight_native_parse(index,job->source,job->path,code,strlen(code))); const InsightSymbol *hit[1]; assert(insight_index_find(index,"concurrent_function",1,hit,1)==1); assert(hit[0]->line==2);
  InsightIndex *held=insight_index_ref(job->shared); assert(insight_index_find(held,"immutable_symbol",1,hit,1)==1); assert(!strcmp(hit[0]->path,"/immutable.c")); insight_index_free(held);
 }
 insight_index_free(index); insight_index_free(job->shared); return NULL;
}
int main(void) {
 assert(insight_native_language_compatible(TM_PARSER_C,TM_PARSER_CPP));assert(insight_native_language_compatible(TM_PARSER_CPP,TM_PARSER_ASM));assert(!insight_native_language_compatible(TM_PARSER_C,TM_PARSER_VHDL));assert(!insight_native_language_compatible(TM_PARSER_CPP,TM_PARSER_SH));assert(!insight_native_language_compatible(TM_PARSER_NONE,TM_PARSER_NONE));assert(insight_native_language_compatible(TM_PARSER_PYTHON,TM_PARSER_PYTHON));
 InsightIndex *index=insight_index_new(); assert(index);
 char name[]="foo",args[]="(int value)"; TMTag function={.name=name,.scope="Scope",.line=7,.kind_letter='f',.arglist=args,.var_type="int",.inheritance="Base"};
 TMTag invalid={.name="\xff",.line=3,.kind_letter='v'};
 GPtrArray *tags=g_ptr_array_new(); g_ptr_array_add(tags,&function); g_ptr_array_add(tags,&invalid);
 TMSourceFile source={.tags_array=tags};
 assert(insight_native_import(index,&source,"/project/a.cpp")); name[0]='X'; args[1]='X';
 const InsightSymbol *found[2]; assert(insight_index_find(index,"foo",1,found,2)==1);
 assert(found[0]->line==7 && !strcmp(found[0]->scope,"Scope"));
 assert(!strcmp(found[0]->signature,"(int value)") && !strcmp(found[0]->type,"int") && !strcmp(found[0]->inheritance,"Base"));
 assert(insight_native_definition_rank(found[0],"/project/a.cpp")==12);
 assert(insight_native_definition_rank(found[0],"/project/b.cpp")==8);
 InsightSymbol prototype={"foo","/project/a.cpp","Scope","p",1,NULL,NULL,NULL};
 assert(insight_native_definition_rank(&prototype,"/project/a.cpp")<insight_native_definition_rank(found[0],"/project/b.cpp"));
 InsightSymbol local={"column","/project/a.cpp","current_function","l",223,"","vString *",""};
 InsightSymbol foreign={"column","/project/a.cpp","other_function","z",121,"","vString *",""};
 assert(insight_native_definition_rank_at(&local,"/project/a.cpp","current_function",225)>12);
 assert(insight_native_definition_rank_at(&foreign,"/project/a.cpp","current_function",225)<0);
 assert(insight_native_definition_rank_at(&local,"/project/b.cpp","current_function",225)<0);
 assert(insight_native_definition_rank_at(&local,"/project/a.cpp","current_function",200)<0);
 assert(insight_native_definition_rank_at(&local,"/project/a.cpp",NULL,225)<0);
 InsightSymbol assembly_label={"entry","/project/native.S","","l",1,"","",""};assert(insight_native_definition_rank_at(&assembly_label,"/project/main.cpp",NULL,1)>=0);
 assert(!insight_native_import(index,&source,"\xff")); assert(insight_index_count(index)==1);
 GPtrArray huge={.pdata=NULL,.len=250001}; source.tags_array=&huge;
 assert(!insight_native_import(index,&source,"/project/a.cpp")); assert(insight_index_count(index)==1);
 source.tags_array=tags; g_ptr_array_set_size(tags,0);
 assert(insight_native_import(index,&source,"/project/a.cpp") && insight_index_count(index)==0);
 assert(insight_native_import(index,NULL,"/project/a.cpp"));
 g_ptr_array_free(tags,TRUE); insight_index_free(index);
 tm_get_workspace();
 const char *code="class Base {};\nclass Widget : public Base {\npublic: int run(int value) { return value; }\n};\nint parsed_function(int value) { return value; }\n";
 char filename[]="native-parser-test-XXXXXX"; gint descriptor=g_mkstemp(filename); assert(descriptor>=0); g_close(descriptor,NULL);
 char *absolute=g_canonicalize_filename(filename,NULL); TMSourceFile *parsed=tm_source_file_new(absolute,"C++"); assert(parsed && parsed->lang>=0);
 index=insight_index_new();assert(insight_native_parse(index,parsed,absolute,code,strlen(code)));
 assert(insight_index_find(index,"parsed_function",1,found,2)==1);assert(strstr(found[0]->signature,"value") && strstr(found[0]->type,"int"));
 assert(insight_index_find(index,"Widget",1,found,2)==1 && strstr(found[0]->inheritance,"Base"));
 const char *scope_code="void unrelated(int column) { (void)column; }\nvoid current(void) {\n int column = 1;\n (void)column;\n}\n";
 assert(insight_native_parse(index,parsed,absolute,scope_code,strlen(scope_code)));
 assert(insight_index_find(index,"column",1,found,2)==2);
 gchar *current_scope=insight_native_function_scope(index,absolute,4); assert(current_scope && !strcmp(current_scope,"current"));
 guint eligible=0; for(guint n=0;n<2;n++) if(insight_native_definition_rank_at(found[n],absolute,current_scope,4)>=0) { eligible++; assert(found[n]->line==3); assert(!strcmp(found[n]->kind,"l")); }
 assert(eligible==1); g_free(current_scope);
 const char *member_code="typedef struct sCell Cell;\nstruct sCell { void *data; };\nstruct Other { void *data; };\nvoid use(Cell *sp, void *data) { (void)sp->data; }\n";
 assert(insight_native_parse(index,parsed,absolute,member_code,strlen(member_code)));
 gboolean is_member=FALSE; gchar *owner=insight_native_member_owner(index,absolute,"use",4,member_code,(gsize)(strstr(member_code,"sp->data")-member_code)+6,&is_member);
 assert(is_member && owner && !strcmp(owner,"sCell"));
 const InsightSymbol *members[16]; size_t member_count=insight_index_find(index,"data",1,members,16); assert(member_count>=3); eligible=0;
 for(size_t n=0;n<member_count && n<16;n++) if(insight_native_member_matches(members[n],owner)) { eligible++; assert(members[n]->line==2); assert(!strcmp(members[n]->kind,"m")); }
 assert(eligible==1); g_free(owner);
 owner=insight_native_member_owner(index,absolute,"use",4,"unknown->data",12,&is_member); assert(is_member && !owner);
 owner=insight_native_member_owner(index,absolute,"use",4,"data",3,&is_member); assert(!is_member && !owner);
 const char *qualified_code="namespace std { int min(int a, int b) { return a; } }\nnamespace other { int min(int a, int b) { return b; } }\nvoid caller(int min) { (void)std::min(1, 2); }\n";
 assert(insight_native_parse(index,parsed,absolute,qualified_code,strlen(qualified_code)));
 owner=insight_native_member_owner(index,absolute,"caller",3,qualified_code,(gsize)(strstr(qualified_code,"std::min")-qualified_code)+7,&is_member);
 assert(is_member && owner && !strcmp(owner,"std")); member_count=insight_index_find(index,"min",1,members,16); eligible=0;
 for(size_t n=0;n<member_count && n<16;n++) if(insight_native_member_matches(members[n],owner)) { eligible++; assert(members[n]->line==1); assert(!strcmp(members[n]->kind,"f")); }
 assert(eligible==1); g_free(owner);
 owner=insight_native_member_owner(index,absolute,"caller",3,"outer::inner::min",16,&is_member); assert(is_member && !strcmp(owner,"outer::inner")); g_free(owner);
 owner=insight_native_member_owner(index,absolute,"caller",3,"::min",5,&is_member); assert(is_member && owner && !*owner); g_free(owner);
 size_t before=insight_index_count(index);assert(!insight_native_parse(index,parsed,absolute,"a\0b",3));assert(insight_index_count(index)==before);
 assert(insight_native_parse(index,parsed,absolute,"",0));assert(insight_index_count(index)==0);
 /* Publication transfers worker tags and rewrites owner pointers. A pinned
  * old tag remains valid even after its source's array is replaced. */
 const char *before_text="int before_publication(void) { return 1; }\n"; assert(insight_native_parse(index,parsed,absolute,before_text,strlen(before_text))); tm_get_workspace(); tm_workspace_add_source_file_noupdate(parsed); assert(parsed->tags_array->len==1); TMTag *retained=tm_tag_ref(g_ptr_array_index(parsed->tags_array,0));
 TMSourceFile *prepared=tm_source_file_new(absolute,"C++"); const char *after_text="int after_publication(void) { return 2; }\n"; tm_source_file_parse(prepared,(guchar *)after_text,strlen(after_text),TRUE); tm_workspace_prepare_source_file_tags(prepared); tm_workspace_publish_source_file_tags(parsed,prepared);
 assert(prepared->tags_array->len==0 && parsed->tags_array->len==1); TMTag *published=g_ptr_array_index(parsed->tags_array,0); assert(published->file==parsed && !strcmp(published->name,"after_publication")); assert(!strcmp(retained->name,"before_publication")); assert(tm_get_workspace()->tags_array->len==1); assert(g_ptr_array_index(tm_get_workspace()->tags_array,0)==published); tm_tag_unref(retained); tm_source_file_free(prepared); tm_workspace_remove_source_file(parsed);
 /* Ctags' global parser state must be serialized across simultaneous jobs.
  * Readers retain a snapshot after its original owner releases it. */
 GMutex mutex; GCond ready; g_mutex_init(&mutex); g_cond_init(&ready); guint waiting=0; gboolean start=FALSE;
 InsightIndex *shared=insight_index_new(); InsightSymbol immutable={"immutable_symbol",NULL,NULL,"f",1,NULL,NULL,NULL}; assert(insight_index_update(shared,"/immutable.c",&immutable,1));
 ParserThread jobs[4]; GThread *threads[4];
 for(guint n=0;n<4;n++) { jobs[n]=(ParserThread){tm_source_file_new(absolute,"C++"),absolute,insight_index_ref(shared),&mutex,&ready,&waiting,&start}; assert(jobs[n].source); threads[n]=g_thread_new("parser-regression",parse_in_worker,&jobs[n]); }
 insight_index_free(shared); g_mutex_lock(&mutex); while(waiting<4) g_cond_wait(&ready,&mutex); start=TRUE; g_cond_broadcast(&ready); g_mutex_unlock(&mutex);
 for(guint n=0;n<4;n++) { g_thread_join(threads[n]); tm_source_file_free(jobs[n].source); } g_cond_clear(&ready); g_mutex_clear(&mutex);
 tm_source_file_free(parsed);insight_index_free(index);tm_workspace_free();g_unlink(filename);g_free(absolute);
 puts("native index: all checks passed"); return 0;
}
