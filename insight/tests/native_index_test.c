/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-index.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/tagmanager/tm_ctags.h"
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
 assert(!insight_native_import(index,&source,"\xff")); assert(insight_index_count(index)==1);
 GPtrArray huge={.pdata=NULL,.len=250001}; source.tags_array=&huge;
 assert(!insight_native_import(index,&source,"/project/a.cpp")); assert(insight_index_count(index)==1);
 source.tags_array=tags; g_ptr_array_set_size(tags,0);
 assert(insight_native_import(index,&source,"/project/a.cpp") && insight_index_count(index)==0);
 assert(insight_native_import(index,NULL,"/project/a.cpp"));
 g_ptr_array_free(tags,TRUE); insight_index_free(index);
 tm_ctags_init();
 const char *code="class Base {};\nclass Widget : public Base {\npublic: int run(int value) { return value; }\n};\nint parsed_function(int value) { return value; }\n";
 char filename[]="native-parser-test-XXXXXX"; gint descriptor=g_mkstemp(filename); assert(descriptor>=0); g_close(descriptor,NULL);
 char *absolute=g_canonicalize_filename(filename,NULL); TMSourceFile *parsed=tm_source_file_new(absolute,"C++"); assert(parsed && parsed->lang>=0);
 index=insight_index_new();assert(insight_native_parse(index,parsed,absolute,code,strlen(code)));
 assert(insight_index_find(index,"parsed_function",1,found,2)==1);assert(strstr(found[0]->signature,"value") && strstr(found[0]->type,"int"));
 assert(insight_index_find(index,"Widget",1,found,2)==1 && strstr(found[0]->inheritance,"Base"));
 size_t before=insight_index_count(index);assert(!insight_native_parse(index,parsed,absolute,"a\0b",3));assert(insight_index_count(index)==before);
 assert(insight_native_parse(index,parsed,absolute,"",0));assert(insight_index_count(index)==0);
 /* Ctags' global parser state must be serialized across simultaneous jobs.
  * Readers retain a snapshot after its original owner releases it. */
 GMutex mutex; GCond ready; g_mutex_init(&mutex); g_cond_init(&ready); guint waiting=0; gboolean start=FALSE;
 InsightIndex *shared=insight_index_new(); InsightSymbol immutable={"immutable_symbol",NULL,NULL,"f",1,NULL,NULL,NULL}; assert(insight_index_update(shared,"/immutable.c",&immutable,1));
 ParserThread jobs[4]; GThread *threads[4];
 for(guint n=0;n<4;n++) { jobs[n]=(ParserThread){tm_source_file_new(absolute,"C++"),absolute,insight_index_ref(shared),&mutex,&ready,&waiting,&start}; assert(jobs[n].source); threads[n]=g_thread_new("parser-regression",parse_in_worker,&jobs[n]); }
 insight_index_free(shared); g_mutex_lock(&mutex); while(waiting<4) g_cond_wait(&ready,&mutex); start=TRUE; g_cond_broadcast(&ready); g_mutex_unlock(&mutex);
 for(guint n=0;n<4;n++) { g_thread_join(threads[n]); tm_source_file_free(jobs[n].source); } g_cond_clear(&ready); g_mutex_clear(&mutex);
 tm_source_file_free(parsed);insight_index_free(index);g_unlink(filename);g_free(absolute);
 puts("native index: all checks passed"); return 0;
}
