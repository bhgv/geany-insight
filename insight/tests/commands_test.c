/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char result[128];
static const char *text(void *context) { return context ? context : "first"; }
static const char *selection(void *context) { (void)context; return "selection"; }
static int replace(void *context,const char *s) { (void)context; snprintf(result,sizeof result,"%s",s); return 1; }
static int load(InsightSession *session,const char *source,char *error) {
 return insight_session_load(session,source,strlen(source),error,512);
}
int main(void) {
 InsightSession *session=insight_session_new(); assert(session);
 InsightHost host={.context=NULL,.text=text,.selection=selection,.replace_selection=replace,.index=NULL}; char error[512];
 assert(insight_session_command_count(session)==0);
 assert(insight_session_command_name(session,0)==NULL);
 assert(insight_session_command_label(session,0)==NULL);
 const char *extension=
 "assert(INSIGHT_API_VERSION=='0.8'); local calls=0; local get=editor.text; "
 "commands.register('count','Count',function() calls=calls+1; editor.replace_selection(tostring(calls)) end,'<Primary><Alt>n'); "
 "commands.register('text','Text',function() editor.replace_selection(get()) end); "
 "commands.register('bad','Bad',function() error({}) end); "
 "commands.register('loop','Loop',function() while true do end end); "
 "commands.register('budget','Budget',function() local s=0; for i=1,150000 do s=s+i end; editor.replace_selection(tostring(s)) end); "
 "commands.register('late','Late',function() commands.register('new','New',function() end) end); "
 "commands.register('partial','Partial',function() editor.replace_selection('before error'); error('failure') end)";
 assert(load(session,extension,error)==0);
 assert(insight_session_command_count(session)==7);
 assert(!strcmp(insight_session_command_name(session,0),"count"));
 assert(!strcmp(insight_session_command_label(session,0),"Count"));
 assert(!strcmp(insight_session_command_shortcut(session,0),"<Primary><Alt>n"));
 assert(insight_session_invoke(session,"count",&host,error,sizeof error)==0 && !strcmp(result,"1"));
 assert(insight_session_invoke(session,"count",&host,error,sizeof error)==0 && !strcmp(result,"2"));
 assert(insight_session_invoke(session,"text",&host,error,sizeof error)==0 && !strcmp(result,"first"));
 host.context="second";
 assert(insight_session_invoke(session,"text",&host,error,sizeof error)==0 && !strcmp(result,"second"));
 assert(load(session,"local t=setmetatable({}, {__index=function() return 42 end}); assert(t.x==42); commands.register('ordinary','Ordinary',function() end)",error)==0);
 assert(load(session,extension,error)==0);
 assert(insight_session_invoke(session,"count",&host,error,sizeof error)==0);
 assert(insight_session_invoke(session,"count",&host,error,sizeof error)==0);
 assert(load(session,"setmetatable({}, {__gc=function() while true do end end})",error)==1 && strstr(error,"finalizers"));
 assert(load(session,"editor.text()",error)==1 && strstr(error,"unavailable"));
 assert(load(session,"not lua !!!",error)==1);
 assert(load(session,"while true do end",error)==1 && strstr(error,"budget"));
 assert(load(session,"local s=string.rep('x',32*1024*1024)",error)==1);
 assert(load(session,"commands.register('x','X',function() end); commands.register('x','X',function() end)",error)==1);
 assert(strstr(error,"duplicate"));
 assert(load(session,"commands.register('1bad','Bad',function() end)",error)==1);
 assert(load(session,"commands.register('x',string.rep('x',300),function() end)",error)==1);
 assert(load(session,"for i=1,129 do commands.register('c'..i,'Command',function() end) end",error)==1);
 assert(strstr(error,"limit"));
 assert(insight_session_command_count(session)==7);
 assert(insight_session_invoke(session,"count",&host,error,sizeof error)==0 && !strcmp(result,"3"));
 assert(insight_session_invoke(session,"missing",&host,error,sizeof error)==1 && strstr(error,"unknown"));
 assert(insight_session_invoke(session,"bad",&host,error,sizeof error)==1 && strstr(error,"non-string"));
 assert(insight_session_invoke(session,"loop",&host,error,sizeof error)==1 && strstr(error,"budget"));
 assert(insight_session_invoke(session,"late",&host,error,sizeof error)==1);
 assert(insight_session_command_count(session)==7);
 assert(insight_session_invoke(session,"partial",&host,error,sizeof error)==1 && !strcmp(result,"before error"));
 assert(insight_session_invoke(session,"budget",&host,error,sizeof error)==0);
 assert(insight_session_invoke(session,"budget",&host,error,sizeof error)==0);
 assert(insight_session_invoke(session,"text",&host,error,sizeof error)==0 && !strcmp(result,"second"));
 assert(load(session,"",error)==0 && insight_session_command_count(session)==0);
 assert(insight_session_invoke(session,"count",&host,error,sizeof error)==1);
 assert(insight_session_invoke(session,"count",NULL,error,sizeof error)==2);
 insight_session_free(session); insight_session_free(NULL);
 puts("commands: all checks passed"); return 0;
}
