/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "paragraph.h"
#include <string.h>
static void check(const gchar *text,gint anchor,gint caret,gboolean join,gint margin,gint tab,gint start,gint end,const gchar *expected) {
 GError *error=NULL;gint a=-1,b=-1;gchar *out=insight_paragraph(text,anchor,caret,join,margin,tab,"\n",&a,&b,&error);g_assert_no_error(error);g_assert_cmpint(a,==,start);g_assert_cmpint(b,==,end);g_assert_cmpstr(out,==,expected);g_free(out);
}
static void fail(const gchar *text,gint anchor,gint caret,gint margin,gint tab) {
 GError *error=NULL;gint a,b;gchar *out=insight_paragraph(text,anchor,caret,FALSE,margin,tab,"\n",&a,&b,&error);g_assert_null(out);g_assert_nonnull(error);g_clear_error(&error);
}
int main(void) {
 const gchar *text="  one two\r\n  three four\r\n \t\r\nfive six\nseven\n";
 check(text,7,7,FALSE,12,4,0,25,"  one two\r\n  three four\r\n");
 check(text,7,7,FALSE,9,4,0,25,"  one two\r\n  three\r\n  four\r\n");
 check(text,41,2,FALSE,12,4,0,strlen(text),"  one two\r\n  three four\r\n \t\r\nfive six\nseven\n");
 check("one\ntwo\n\nthree\nfour",0,4,TRUE,80,4,0,4,"one\n");
 check("one\ntwo\n\nthree\nfour",0,0,TRUE,80,4,0,8,"one two\n");
 check("  a  b \t\r\n\t c   d  \nnext",3,3,TRUE,80,4,0,strlen("  a  b \t\r\n\t c   d  \n"),"  a  b c   d  \n");
 check("one\n\n\n two\n",0,10,TRUE,80,4,0,11,"one two\n");
 check("last\n",0,0,TRUE,80,4,0,5,"last\n");
 check("last\n",5,5,TRUE,80,4,5,5,"");
 check(" \t\r\nnext",1,1,FALSE,80,4,0,4," \t\r\n");
 check("\tαβ 界 a\n  b",0,0,FALSE,9,4,0,strlen("\tαβ 界 a\n  b"),"\tαβ 界\n\ta b");
 check("é é é",0,0,FALSE,3,4,0,strlen("é é é"),"é é\né");
 check("    longword tiny",0,0,FALSE,3,4,0,17,"    longword\n    tiny");
 check("",0,0,FALSE,80,4,0,0,"");
 fail("α",1,1,80,4);fail("abc",-1,0,80,4);fail("abc",0,4,80,4);fail("abc",0,0,0,4);fail("abc",0,0,80,0);fail("\xff",0,0,80,4);
 gchar *large=g_strnfill(8*1024*1024+1,'a');fail(large,0,0,80,4);g_free(large);
 large=g_strnfill(500001,'\n');fail(large,0,0,80,4);g_free(large);
 GString *expansion=g_string_new(NULL);g_string_append_len(expansion,"    ",4);for(guint i=0;i<1100;i++) g_string_append_c(expansion,' ');for(guint i=0;i<8000;i++) g_string_append(expansion,"a ");g_string_append(expansion,"\n\n");fail(expansion->str,0,0,1,4);g_string_free(expansion,TRUE);
 g_print("Paragraph: scope, blank separators, exact joins, mixed EOL, Unicode widths, indentation, boundaries and validation passed\n");return 0;
}
