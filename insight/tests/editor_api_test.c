/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned long line, selection_start, selection_end;
static char content[128]="hello";
static char filename[128]="/tmp/test.c";
static int saved;
static const char *text(void *ctx) { (void)ctx; return content; }
static int replace(void *ctx,const char *s) { (void)ctx; (void)s; return 1; }
static unsigned long position(void *ctx) { (void)ctx; return 42; }
static int go(void *ctx,unsigned long value) { (void)ctx; if(value>100) return 0; line=value; return 1; }
static int fake_open(void *ctx,const char *path) { (void)ctx; snprintf(filename,sizeof filename,"%s",path); return 1; }
static int fake_save(void *ctx) { (void)ctx; saved++; return 1; }
static const char *fake_filename(void *ctx) { (void)ctx; return filename; }
static int fake_range(void *ctx,unsigned long *start,unsigned long *end) { (void)ctx; *start=selection_start; *end=selection_end; return 1; }
static int fake_selection(void *ctx,unsigned long a,unsigned long b) { (void)ctx; if(a>b || b>strlen(content)) return 0; selection_start=a; selection_end=b; return 1; }
static unsigned long fake_count(void *ctx) { (void)ctx; return 1; }
static const char *fake_line(void *ctx,unsigned long line) { (void)ctx; return line==1 ? content : NULL; }
static int fake_replace_range(void *ctx,unsigned long a,unsigned long b,const char *s) {
 (void)ctx; size_t length=strlen(content),n=strlen(s);
 if(a>b || b>length || length-(b-a)+n>=sizeof content) return 0;
 memmove(content+a+n,content+b,length-b+1); memcpy(content+a,s,n); return 1;
}
static const InsightEditorAPI api={.position=position,.goto_line=go,.replace_range=fake_replace_range,.line_text=fake_line,.line_count=fake_count,.set_selection=fake_selection,.selection_range=fake_range,.filename=fake_filename,.save_file=fake_save,.open_file=fake_open};
static int run(InsightHost *host,const char *source,char *error) { return insight_run(host,source,strlen(source),error,512); }
int main(void) {
 InsightHost host={.text=text,.selection=text,.replace_selection=replace,.api=&api}; char error[512];
 assert(run(&host,"assert(editor.position()==42); editor.goto_line(7)",error)==0 && line==7);
 assert(run(&host,"editor.goto_line(0)",error)==1 && line==7);
 assert(run(&host,"editor.goto_line(-1)",error)==1);
 assert(run(&host,"editor.goto_line(101)",error)==1);
 assert(run(&host,"editor.open('/tmp/other.c')",error)==0 && !strcmp(filename,"/tmp/other.c"));
 assert(run(&host,"editor.open('a'..string.char(0))",error)==1);
 assert(run(&host,"editor.save()",error)==0 && saved==1);
 assert(run(&host,"assert(editor.filename()=='/tmp/other.c')",error)==0);
 assert(run(&host,"local a,b=editor.selection_range(); assert(a==0 and b==0)",error)==0);
 assert(run(&host,"editor.set_selection(1,4); local a,b=editor.selection_range(); assert(a==1 and b==4)",error)==0);
 assert(run(&host,"editor.set_selection(4,1)",error)==1);
 assert(run(&host,"editor.set_selection(0,99)",error)==1);
 assert(run(&host,"assert(editor.line_count()==1)",error)==0);
 assert(run(&host,"assert(editor.line_text(1)=='hello')",error)==0);
 assert(run(&host,"editor.line_text(2)",error)==1);
 assert(run(&host,"editor.replace_range(1,4,'XX'); assert(editor.text()=='hXXo')",error)==0);
 assert(run(&host,"editor.replace_range(4,1,'x')",error)==1);
 host.api=NULL; assert(run(&host,"editor.position()",error)==1 && strstr(error,"unavailable"));
 puts("editor API: all checks passed"); return 0;
}
