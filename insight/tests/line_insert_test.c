/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "line_insert.h"
#include <string.h>
static void check(const gchar *source,gint anchor,gint caret,gint mode,gboolean indent,const gchar *eol,gint start,gint end,gint position,const gchar *replacement,const gchar *expected) {
 GError *error=NULL;InsightLineInsert plan={0};g_assert_true(insight_line_insert(source,anchor,caret,mode,indent,eol,&plan,&error));g_assert_no_error(error);g_assert_cmpint(plan.start,==,start);g_assert_cmpint(plan.end,==,end);g_assert_cmpint(plan.caret,==,position);g_assert_cmpstr(plan.text,==,replacement);
 GString *out=g_string_new_len(source,plan.start);g_string_append(out,plan.text);g_string_append(out,source+plan.end);g_assert_cmpstr(out->str,==,expected);g_assert_true(g_utf8_validate(out->str,plan.caret,NULL));g_string_free(out,TRUE);insight_line_insert_clear(&plan);g_assert_null(plan.text);
}
static void fail(const gchar *source,gint anchor,gint caret,gint mode,const gchar *eol) { GError *error=NULL;InsightLineInsert p={0};g_assert_false(insight_line_insert(source,anchor,caret,mode,TRUE,eol,&p,&error));g_assert_nonnull(error);g_assert_null(p.text);g_clear_error(&error); }
int main(void) {
 check("\t  αβ\r\nnext",3,7,0,TRUE,"\r\n",0,0,3,"\t  \r\n","\t  \r\n\t  αβ\r\nnext");
 check("\t  αβ\r\nnext",7,3,1,TRUE,"\r\n",9,9,12,"\t  \r\n","\t  αβ\r\n\t  \r\nnext");
 check("\t  αβ",7,7,1,TRUE,"\n",7,7,11,"\n\t  ","\t  αβ\n\t  ");
 check("  αβ",4,4,2,TRUE,"\n",4,4,4,"\n  ","  α\n  β");
 check("  αβ\r\n  tail",12,2,2,TRUE,"\r\n",2,12,2,"\r\n  ","  \r\n  il");
 check("    x",2,2,2,TRUE,"\n",2,2,2,"\n  ","  \n    x");
 check("  a\n  b",0,6,0,FALSE,"\n",4,4,4,"\n","  a\n\n  b");
 check("last\n",0,0,1,FALSE,"\n",5,5,5,"\n","last\n\n");
 check("last\n",5,5,1,TRUE,"\r",5,5,6,"\r","last\n\r");
 check("",0,0,0,TRUE,"\n",0,0,0,"\n","\n");check("",0,0,1,TRUE,"\n",0,0,1,"\n","\n");check("",0,0,2,TRUE,"\n",0,0,0,"\n","\n");
 fail("α",1,0,0,"\n");fail("a\r\nb",2,2,0,"\n");fail("abc",-1,0,0,"\n");fail("abc",0,4,0,"\n");fail("abc",0,0,3,"\n");fail("abc",0,0,0,"");fail("\xff",0,0,0,"\n");
 gchar *large=g_strnfill(8*1024*1024,'a');fail(large,0,0,0,"\n");g_free(large);
 g_print("Line insertion: above/below/stay, active caret, UTF-8/reverse ranges, exact indentation, EOL/EOF and atomic limits passed\n");return 0;
}
