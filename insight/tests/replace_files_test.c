/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "replace_files.h"
#include <glib/gstdio.h>
#include <string.h>
static InsightReplacePlan *plan(const gchar *text,const gchar *old,const gchar *replacement,InsightReplaceOptions *o) { GError *e=NULL;InsightReplacePlan *p=insight_replace_plan(text,old,replacement,o,NULL,&e);g_assert_no_error(e);g_assert_nonnull(p);return p; }
static void result(InsightReplacePlan *p,const gchar *expected,guint count) { GError *e=NULL;guint n;gchar *s=insight_replace_render(p,&n,&e);g_assert_no_error(e);g_assert_cmpstr(s,==,expected);g_assert_cmpuint(n,==,count);g_free(s); }
int main(void) {
 InsightReplaceOptions o={0};InsightReplacePlan *p=plan("ABC abc Abc aBc\r\nabc_word αabc abcα\r\n","abc","xyz",&o);g_assert_cmpuint(p->hits->len,==,7);result(p,"xyz xyz xyz xyz\r\nxyz_word αxyz xyzα\r\n",7);insight_replace_plan_free(p);
 o.whole=TRUE;o.preserve_case=TRUE;p=plan("ABC abc Abc aBc\r\nabc_word αabc abcα\r\n","abc","word",&o);result(p,"WORD word Word word\r\nabc_word αabc abcα\r\n",4);((InsightReplaceHit *)g_ptr_array_index(p->hits,1))->selected=FALSE;result(p,"WORD abc Word word\r\nabc_word αabc abcα\r\n",3);insight_replace_plan_free(p);
 o.first_per_line=TRUE;p=plan("abc abc\r\nABC abc\nAbc", "abc","test",&o);result(p,"test abc\r\nTEST abc\nTest",3);insight_replace_plan_free(p);
 o=(InsightReplaceOptions){.regex=TRUE,.case_sensitive=TRUE};p=plan("α=12\r\nβ=3\r\n","(?<name>[αβ])=([0-9]+)","\\g<name>(\\2)",&o);result(p,"α(12)\r\nβ(3)\r\n",2);InsightReplaceHit *h=g_ptr_array_index(p->hits,1);g_assert_cmpuint(h->line,==,2);g_assert_cmpuint(h->column,==,1);insight_replace_plan_free(p);
 p=plan("a\r\nb\r\n","^|$","x",&o);result(p,"xax\r\nxbx\r\nx",5);insight_replace_plan_free(p);
 GError *e=NULL;g_assert_null(insight_replace_plan("no hit","(x)","\\2",&o,NULL,&e));g_clear_error(&e);g_assert_null(insight_replace_plan("α","(\\C)\\C","\\1",&o,NULL,&e));g_clear_error(&e);
 gchar *hard=g_strnfill(8192,'a'),*subject=g_strconcat(hard,"!",NULL);g_assert_null(insight_replace_plan(subject,"(a+)+$","x",&o,NULL,NULL));g_free(hard);g_free(subject);
 o=(InsightReplaceOptions){.region=INSIGHT_REGION_SKIP_COMMENTS,.dialect=INSIGHT_CODE_C};p=plan("word; // word\n\"word\"; /* word */\n","word","new",&o);result(p,"new; // word\n\"new\"; /* word */\n",2);insight_replace_plan_free(p);
 o.region=INSIGHT_REGION_COMMENTS;p=plan("int n; //  two spaces\n/*  two spaces */\n","  ","_",&o);result(p,"int n; //_two spaces\n/*_two spaces */\n",2);insight_replace_plan_free(p);
 o.region=INSIGHT_REGION_CODE;p=plan("word; // word\n\"word\"; R\"tag(word)tag\";\n","word","new",&o);result(p,"new; // word\n\"word\"; R\"tag(word)tag\";\n",1);insight_replace_plan_free(p);
 o.region=INSIGHT_REGION_COMMENTS;o.dialect=INSIGHT_CODE_PYTHON;p=plan("x='word' # word\n\"\"\"word\"\"\"\n","word","new",&o);result(p,"x='word' # new\n\"\"\"word\"\"\"\n",1);insight_replace_plan_free(p);
 GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);g_assert_null(insight_replace_plan("","missing","x",&o,cancel,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&e);g_object_unref(cancel);
 g_assert_true(insight_replace_mask("/tmp/α.c"," ?.c ; *.h ",&e));g_assert_false(insight_replace_mask("/tmp/a.cpp","*.c;*.h",&e));g_assert_no_error(e);
 gchar *dir=g_dir_make_tmp("replace-files-core-XXXXXX",NULL),*path=g_build_filename(dir,"bom.c",NULL);g_assert_true(g_file_set_contents(path,"\357\273\277word\r\n",-1,&e));gchar *read=insight_replace_read(path,NULL,&e);g_assert_no_error(e);g_assert_cmpstr(read,==,"word\r\n");g_free(read);g_assert_true(g_file_set_contents(path,"a\0b",3,&e));g_assert_null(insight_replace_read(path,NULL,&e));g_clear_error(&e);g_free(path);g_free(dir);
 g_print("Replace Files: literal/Unicode/case/words, selected hits, first-per-line, captures, CRLF/zero-width, UTF-8 and regex limits, exact lexical membership, masks, cancellation and bounded BOM/NUL reads passed\n");return 0;
}
