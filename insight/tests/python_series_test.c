/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-index.h"
#include "../desktop/source_mask.h"
#include "../desktop/relation_query.h"
#include "../../src/tagmanager/tm_workspace.h"
#include <glib/gstdio.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct { gchar *path,*mask;const gchar *code;InsightIndex *index;TMSourceFile *file;InsightNativeSource source; } Fixture;
static Fixture fixture(const gchar *code) {
 gchar name[]="python-series-XXXXXX";gint fd=g_mkstemp(name);assert(fd>=0);g_close(fd,NULL);
 Fixture f={0};f.path=g_canonicalize_filename(name,NULL);f.code=code;f.file=tm_source_file_new(f.path,"Python");f.index=insight_index_new();assert(insight_native_parse(f.index,f.file,f.path,code,strlen(code)));
 f.mask=insight_source_code_mask(code,strlen(code),INSIGHT_CODE_PYTHON,NULL);f.source=(InsightNativeSource){code,f.mask,strlen(code),NULL};f.source.python=insight_native_python_bindings_new(f.index,f.path,&f.source);return f;
}
static void clear(Fixture *f) { insight_native_python_bindings_free(f->source.python);g_free(f->mask);insight_index_free(f->index);tm_source_file_free(f->file);g_unlink(f->path);g_free(f->path); }
static void declaration(Fixture *f,const gchar *name,const gchar *scope,const gchar *type) {
 const InsightSymbol *tags[16];gsize count=insight_index_find(f->index,name,1,tags,16);assert(count<=16);guint matches=0;for(gsize i=0;i<count;i++) if(!strcmp(tags[i]->scope,scope)) { assert(!strcmp(tags[i]->type,type));matches++; }assert(matches==1);
}
static void owner(Fixture *f,const gchar *expression,const gchar *expected) {
 const gchar *hit=strstr(f->code,expression);assert(hit);gsize position=strrchr(expression,'.')-expression+(hit-f->code)+1;gchar *scope=insight_native_python_scope(f->index,f->path,&f->source,position);gboolean member=FALSE;
 gchar *actual=insight_native_python_owner(f->index,f->path,scope,100000,&f->source,position,&member,NULL,NULL);if(!member || g_strcmp0(actual,expected)) g_printerr("owner %s: expected %s, got %s\n",expression,expected ? expected : "NULL",actual ? actual : "NULL");assert(member && !g_strcmp0(actual,expected));g_free(scope);g_free(actual);
}
static void annotations(void) {
 Fixture f=fixture("class Model:\n    field: int\n    def setup(self):\n        self.typed: int\n    def run(self):\n        local: Model\n        return local.field + self.typed\nmodule: Model\nmodule.field\n");
 declaration(&f,"field","Model","int");declaration(&f,"typed","Model","int");declaration(&f,"local","Model.run","Model");declaration(&f,"module","","Model");owner(&f,"local.field","Model");owner(&f,"self.typed","Model");owner(&f,"module.field","Model");clear(&f);
}
static void receivers(void) {
 Fixture f=fixture("import builtins\nclass Named:\n    field: int\n    def setup(me):\n        me.extra: int = 1\n    def run(me):\n        return me.field\n    @builtins.staticmethod\n    def static(self):\n        self.wrong = 2\n        return self.field # qualified_static\n    @staticmethod\n    @identity(\n        1,\n    )\n    def stacked(self):\n        return self.field # stacked_static\ndef plain(me):\n    return me.field # plain\n");
 declaration(&f,"extra","Named","int");owner(&f,"me.field\n","Named");owner(&f,"self.field # qualified_static",NULL);owner(&f,"self.field # stacked_static",NULL);owner(&f,"me.field # plain",NULL);
 const InsightSymbol *tag;assert(!insight_index_find(f.index,"wrong",1,&tag,1));clear(&f);
}
static void call_results(void) {
 Fixture f=fixture("class Result:\n    field: int\ndef make() -> Result:\n    return Result()\ndef unknown():\n    return Result()\nasync def asynchronous() -> Result:\n    return Result()\nResult().field\nmake().field\nunknown().field\nasynchronous().field\ndef shadow(Result):\n    return Result().field # shadow_call\n");
 owner(&f,"Result().field\n","Result");owner(&f,"make().field","Result");owner(&f,"unknown().field",NULL);owner(&f,"asynchronous().field",NULL);owner(&f,"Result().field # shadow_call",NULL);clear(&f);
}
static void import_hit(Fixture *f,const gchar *expression,const gchar *word,const gchar *expected_path,const gchar *expected_name) {
 const gchar *hit=strstr(f->code,expression),*token=g_strrstr(expression,word);assert(hit && token);gsize position=hit-f->code+(token-expression);gchar *scope=insight_native_python_scope(f->index,f->path,&f->source,position);const InsightSymbol *hits[8];gboolean imported=FALSE;gsize count=insight_native_python_import_query(f->index,f->path,scope,&f->source,position,word,NULL,NULL,hits,8,&imported);if(count!=(expected_name ? 1u : 0u)) g_printerr("import %s count=%zu imported=%d\n",expression,count,imported);assert(imported && count==(expected_name ? 1u : 0u));if(count) assert(!strcmp(hits[0]->path,expected_path) && !strcmp(hits[0]->name,expected_name));g_free(scope);
}
static void dotted_imports(void) {
 Fixture f=fixture("import nested.provider\nimport nested.other\nimport nested.provider as lib\nnested.provider.exported()\nnested.provider.External.field\nlib.External.field\nnested.other.exported()\nnested.unimported.exported()\n");
 gchar *directory=g_path_get_dirname(f.path),*provider=g_build_filename(directory,"nested","provider.py",NULL),*other=g_build_filename(directory,"nested","other.py",NULL);
 InsightSymbol tags[]={{"External",provider,"","c",1,"","",""},{"field",provider,"External","v",2,"","int",""},{"exported",provider,"","f",3,"()","External",""}};assert(insight_index_update(f.index,provider,tags,G_N_ELEMENTS(tags)));InsightSymbol extra={"exported",other,"","f",1,"()","",""};assert(insight_index_update(f.index,other,&extra,1));
 insight_native_python_bindings_free(f.source.python);f.source.python=insight_native_python_bindings_new(f.index,f.path,&f.source);
 import_hit(&f,"nested.provider.exported()","exported",provider,"exported");import_hit(&f,"nested.provider.External.field","field",provider,"field");import_hit(&f,"lib.External.field","field",provider,"field");import_hit(&f,"nested.other.exported()","exported",other,"exported");import_hit(&f,"nested.unimported.exported()","exported",NULL,NULL);
 g_free(provider);g_free(other);g_free(directory);clear(&f);
}
int main(void) { tm_get_workspace();annotations();receivers();call_results();dotted_imports();tm_workspace_free();puts("Python navigation series: all checks passed");return 0; }
