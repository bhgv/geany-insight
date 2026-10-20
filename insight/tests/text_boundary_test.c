/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "text_boundary.h"
#include <string.h>
static void check(const gchar *text,gint position,gint mode,gint expected) { GError *error=NULL;gint target=-1;g_assert_true(insight_text_boundary(text,strlen(text),position,mode,&target,&error));g_assert_no_error(error);g_assert_cmpint(target,==,expected);g_assert_true(g_utf8_validate(text,target,NULL)); }
static void fail(const gchar *text,gsize length,gint position,gint mode) { GError *error=NULL;gint target=987;g_assert_false(insight_text_boundary(text,length,position,mode,&target,&error));g_assert_nonnull(error);g_assert_cmpint(target,==,987);g_clear_error(&error); }
int main(void) {
 const gchar *text="head\r\n\t αβ  \r\nend.";gint start=6,content=8,tail=12,end=14;
 check(text,10,0,content);check(text,content,0,start);check(text,7,0,start);check(text,start,0,0);check(text,0,0,0);
 check(text,10,1,tail);check(text,tail,1,end);check(text,13,1,end);check(text,end,1,strlen(text));
 check(text,0,2,strlen(text));check("a.b. c",1,2,2);check("a.b. c",2,2,4);check("a.b. c",4,2,6);check("no period\n",0,2,10);
 check("a\n \t\nlast",3,0,2);check("a\n \t\nlast",2,0,0);check("a\n \t\nlast",2,1,4);check("a\n \t\nlast",4,1,9);
 check("a\r\rb",2,0,0);check("a\r\rb",2,1,4);check("a\n",2,0,0);check("a\n",2,1,2);
 check("",0,0,0);check("",0,1,0);check("",0,2,0);
 fail(text,strlen(text),9,0);fail(text,strlen(text),15,0);fail(text,strlen(text),-1,0);fail(text,strlen(text),100,0);fail(text,strlen(text),0,3);fail("a\0b",3,0,0);fail("\xff",1,0,0);
 gchar *large=g_strnfill(8*1024*1024+1,'a');fail(large,8*1024*1024+1,0,0);g_free(large);
 g_print("Text boundaries: three-step smart edges, UTF-8/CRLF, whitespace-only/empty/EOF and literal sentence periods passed\n");return 0;
}
