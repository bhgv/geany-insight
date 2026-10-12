/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char result[128];
static const char *text(void *p) { (void)p; return "hello world"; }
static const char *selection(void *p) { (void)p; return "hello"; }
static int replace(void *p,const char *s) { (void)p; snprintf(result,sizeof result,"%s",s); return 1; }
static int run(const char *s,char *error) {
 InsightHost h={.context=NULL,.text=text,.selection=selection,.replace_selection=replace,.index=NULL};
 return insight_run(&h,s,strlen(s),error,512);
}
int main(void) {
 char error[512];
 assert(run("assert(editor.text() == 'hello world'); editor.replace_selection(string.upper(editor.selection()))",error)==0);
 assert(strcmp(result,"HELLO")==0);
 assert(run("assert(io == nil and os == nil and package == nil and debug == nil and load == nil and pcall == nil)",error)==0);
 assert(run("while true do end",error)==1);
 assert(strstr(error,"budget"));
 assert(run("local s=string.rep('x',32*1024*1024)",error)==1);
 assert(run("error({})",error)==1);
 assert(strstr(error,"non-string"));
 assert(run("editor.replace_selection('a' .. string.char(0))",error)==1);
 assert(run("project.symbols(\"foo\")",error)==1);
 assert(strstr(error,"unavailable"));
 assert(run("this is not lua",error)==1);
 assert(run("editor.replace_selection('ok')",error)==0);
 assert(strcmp(result,"ok")==0);
 InsightIndex *index=insight_index_new(); assert(index);
 InsightSymbol symbols[]={ {"foo",NULL,"namespace","f",42} };
 assert(insight_index_update(index,"project/a.cpp",symbols,1));
 InsightHost host={.context=NULL,.text=text,.selection=selection,.replace_selection=replace,.index=index};
 const char *script="local s,total=project.symbols('fo'); assert(total==1 and #s==1); assert(s[1].name=='foo' and s[1].line==42 and s[1].path=='project/a.cpp'); local e,n=project.symbols('fo',true); assert(#e==0 and n==0)";
 assert(insight_run(&host,script,strlen(script),error,sizeof error)==0);
 insight_index_free(index);
 assert(insight_run(NULL,"",0,error,sizeof error)==2);
 puts("runtime: all checks passed"); return 0;
}
