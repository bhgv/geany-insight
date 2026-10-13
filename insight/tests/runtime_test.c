/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char result[128];
static const char *text(void *p) { (void)p; return "hello world"; }
static const char *multiline(void *p) { (void)p; return "foo foo_bar\r\nfoo\rfoo\nимя имя_x имя"; }
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
 assert(run("local r,n=editor.occurrences('hello'); assert(editor.has('occurrences') and n==1 and #r==1 and r[1].start==0 and r[1].finish==5 and r[1].line==1 and r[1].column==0)",error)==0);
 assert(run("local r,n=editor.occurrences('hell'); assert(n==0 and #r==0)",error)==0);
 assert(run("editor.occurrences('123')",error)==1);
 assert(run("editor.occurrences('hello',0)",error)==1);
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
 InsightSymbol symbols[]={ {"foo",NULL,"namespace","f",42,"(int value)","int",""} };
 assert(insight_index_update(index,"project/a.cpp",symbols,1));
 InsightHost host={.context=NULL,.text=text,.selection=selection,.replace_selection=replace,.index=index};
 const char *script="local info=project.stats(); assert(info.symbols==1 and info.files_with_symbols==1); local s,total=project.symbols('fo'); assert(total==1 and #s==1); assert(s[1].name=='foo' and s[1].line==42 and s[1].path=='project/a.cpp' and s[1].signature=='(int value)' and s[1].type=='int' and s[1].inheritance==''); local e,n=project.symbols('fo',true); assert(#e==0 and n==0)";
 assert(insight_run(&host,script,strlen(script),error,sizeof error)==0);
 const char *file_query="local f,n=project.files('project/'); assert(n==1 and f[1]=='project/a.cpp'); local e,t=project.files('',1,1); assert(t==1 and #e==0)";
 assert(insight_run(&host,file_query,strlen(file_query),error,sizeof error)==0);
 const char *advanced="local s,n=project.symbols('foo',true,{scope='namespace',kind='f',path='project/a.cpp',limit=1}); assert(n==1 and #s==1); local e,t=project.symbols('',false,{path='absent'}); assert(t==0 and #e==0)";
 assert(insight_run(&host,advanced,strlen(advanced),error,sizeof error)==0);
 host.text=multiline;
 const char *matches="local r,n=editor.occurrences('foo',2); assert(n==3 and #r==2 and r[2].line==2 and r[2].column==0); local u,t=editor.occurrences('имя'); assert(t==2 and u[1].line==4 and u[2].column==16)";
 assert(insight_run(&host,matches,strlen(matches),error,sizeof error)==0);
 const char *paged="local r,n=editor.occurrences('foo',1,1); assert(n==3 and #r==1 and r[1].line==2); local e,t=editor.occurrences('foo',1,10); assert(t==3 and #e==0)";
 assert(insight_run(&host,paged,strlen(paged),error,sizeof error)==0);
 assert(run("editor.occurrences('hello',1,-1)",error)==1);
 assert(run("editor.occurrences(string.char(255))",error)==1);
 insight_index_free(index);
 assert(run("local function inner() error('boom') end; inner()",error)==1 && strstr(error,"stack traceback"));
 assert(insight_run(NULL,"",0,error,sizeof error)==2);
 puts("runtime: all checks passed"); return 0;
}
