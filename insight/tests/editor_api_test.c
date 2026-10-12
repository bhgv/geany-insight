/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned long line;
static const char *text(void *ctx) { (void)ctx; return "hello"; }
static int replace(void *ctx,const char *s) { (void)ctx; (void)s; return 1; }
static unsigned long position(void *ctx) { (void)ctx; return 42; }
static int go(void *ctx,unsigned long value) { (void)ctx; if(value>100) return 0; line=value; return 1; }
static const InsightEditorAPI api={.position=position,.goto_line=go};
static int run(InsightHost *host,const char *source,char *error) { return insight_run(host,source,strlen(source),error,512); }
int main(void) {
 InsightHost host={.text=text,.selection=text,.replace_selection=replace,.api=&api}; char error[512];
 assert(run(&host,"assert(editor.position()==42); editor.goto_line(7)",error)==0 && line==7);
 assert(run(&host,"editor.goto_line(0)",error)==1 && line==7);
 assert(run(&host,"editor.goto_line(-1)",error)==1);
 assert(run(&host,"editor.goto_line(101)",error)==1);
 host.api=NULL; assert(run(&host,"editor.position()",error)==1 && strstr(error,"unavailable"));
 puts("editor API: all checks passed"); return 0;
}
