/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "document.h"
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 InsightDocument d={0};
 assert(insight_document_text(NULL)==NULL);
 assert(insight_document_selection(NULL)==NULL);
 assert(insight_document_selection(&d)==NULL);
 const char *text="AПривет😀Z";
 assert(insight_document_init(&d,text,strlen(text),1,13));
 assert(strcmp(insight_document_selection(&d),"Привет")==0);
 InsightHost host={.context=&d,.text=insight_document_text,.selection=insight_document_selection,.replace_selection=insight_document_replace,.index=NULL};
 char error[512]; const char *source="editor.replace_selection('мир')";
 assert(insight_run(&host,source,strlen(source),error,sizeof error)==0);
 assert(strcmp(d.text,"Aмир😀Z")==0 && d.start==1 && d.end==7);
 assert(insight_document_replace(&d,""));
 assert(strcmp(d.text,"A😀Z")==0 && d.start==1 && d.end==1);
 assert(!insight_document_replace(&d,"\xff"));
 assert(strcmp(d.text,"A😀Z")==0);
 insight_document_dispose(&d);
 assert(!insight_document_init(&d,text,strlen(text),2,13));
 assert(!insight_document_init(&d,"\xc0\x80",2,0,0));
 assert(!insight_document_init(&d,"\xed\xa0\x80",3,0,0));
 assert(!insight_document_init(&d,"\xf4\x90\x80\x80",4,0,0));
 assert(!insight_document_init(&d,"a\0b",3,0,0));
 assert(!insight_document_init(&d,"abc",3,2,1));
 assert(insight_document_init(&d,"",0,0,0));
 assert(insight_document_replace(&d,"😀"));
 assert(d.length==4 && d.end==4);
 insight_document_dispose(&d);
 puts("document: all checks passed"); return 0;
}
