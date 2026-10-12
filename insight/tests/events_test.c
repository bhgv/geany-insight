/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static const char *text(void *p) { (void)p; return "document"; }
static int replace(void *p,const char *s) { (void)p; (void)s; assert(0); return 0; }
int main(void) {
 InsightSession *s=insight_session_new(); char error[512];
 InsightHost host={.text=text,.selection=text,.replace_selection=replace};
 const char *source="n=0; events.on('document_save',function(e) assert(e.name=='document_save' and e.path=='file.cpp'); assert(editor.text()=='document'); n=n+1 end); events.on('document_save',function() error('bad handler') end); events.on('document_save',function() n=n+1 end); events.on('document_activate',function() editor.replace_selection('forbidden') end); commands.register('check','Check',function() assert(n==4) end)";
 assert(insight_session_load(s,source,strlen(source),error,sizeof error)==0);
 assert(insight_session_emit(s,"document_save","file.cpp",&host,error,sizeof error)==1 && strstr(error,"bad handler"));
 assert(insight_session_emit(s,"document_save","file.cpp",&host,error,sizeof error)==1);
 assert(insight_session_invoke(s,"check",&host,error,sizeof error)==0);
 assert(insight_session_emit(s,"document_activate",NULL,&host,error,sizeof error)==1 && strstr(error,"read-only"));
 assert(!insight_session_is_running(s)); insight_session_free(s);
 puts("events: all checks passed"); return 0;
}
