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
int main(void) { tm_get_workspace();annotations();receivers();call_results();tm_workspace_free();puts("Python navigation series: all checks passed");return 0; }
