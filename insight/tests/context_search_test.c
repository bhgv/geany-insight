#include "context_search.h"
#include <glib.h>
static void find(const char *s,const char *q,gsize from,gboolean forward,guint flags,gsize expected,gboolean wrap) {
 gsize a,b; gboolean w; GError *error=NULL; g_assert_true(insight_context_find(s,strlen(s),q,from,forward,flags,&a,&b,&w,&error)); g_assert_no_error(error); g_assert_cmpuint(a,==,expected); g_assert_cmpint(w,==,wrap);
}
int main(void) {
 find("WriteOpenDoc", "~wod",0,TRUE,0,0,FALSE);
 find("Текст", "~тст",0,TRUE,0,0,FALSE);
 find("WriteOpenDoc WriteOpenDoc", "~wod",12,FALSE,0,0,FALSE);
 find("WriteOpenDoc", "~wod",12,TRUE,0,0,TRUE);
 find("a[x]b", "~a[b",0,TRUE,0,0,FALSE);
 find("Word", "~wd",0,TRUE,INSIGHT_SEARCH_WORD,0,FALSE);
 find("x ~wod", "~wod",0,TRUE,INSIGHT_SEARCH_REGEX,2,FALSE);
 find("foo Foo food","foo",4,TRUE,0,4,FALSE); find("foo Foo food","foo",4,TRUE,INSIGHT_SEARCH_CASE,8,FALSE);
 find("foo Foo food","foo",8,TRUE,INSIGHT_SEARCH_WORD,0,TRUE); find("foo Foo food","foo",4,FALSE,0,0,FALSE);
 find("foo Foo food","foo",0,FALSE,0,8,TRUE); find("a\nb","^b",0,TRUE,INSIGHT_SEARCH_REGEX,2,FALSE);
 find("тест ТЕСТ","тест",1,TRUE,0,9,FALSE); find("_foo foo1 foo","foo",0,TRUE,INSIGHT_SEARCH_WORD,10,FALSE);
 find("a\nb","^",1,TRUE,INSIGHT_SEARCH_REGEX,2,FALSE);
 gsize a,b; gboolean w; GError *e=NULL; g_assert_false(insight_context_find("a",1,"[",0,TRUE,INSIGHT_SEARCH_REGEX,&a,&b,&w,&e)); g_assert_nonnull(e); g_clear_error(&e);
 g_assert_false(insight_context_find("a",1,"missing",0,TRUE,0,&a,&b,&w,NULL));
 InsightSearchMatch matches[4]; gsize count=0;
 const char *exact="parser Parser parserDefinition subparsersDefault parser _parser parser1";
 g_assert_true(insight_context_collect(exact,strlen(exact),"parser",INSIGHT_SEARCH_CASE|INSIGHT_SEARCH_WORD,matches,4,&count,&e));
 g_assert_cmpuint(count,==,2); g_assert_cmpuint(matches[0].start,==,0); g_assert_cmpuint(matches[1].start,==,49);

 g_assert_true(insight_context_collect("x\r\nx\rx\nx",8,"x",0,matches,2,&count,&e)); g_assert_no_error(e);g_assert_cmpuint(count,==,4);g_assert_cmpuint(matches[1].start,==,3);g_assert_cmpuint(matches[1].line,==,2);g_assert_cmpuint(matches[1].column,==,0);
 g_assert_true(insight_context_collect("а\nб",5,"^",INSIGHT_SEARCH_REGEX,matches,4,&count,&e));g_assert_cmpuint(count,==,2);g_assert_cmpuint(matches[1].start,==,3);g_assert_cmpuint(matches[1].end,==,3);g_assert_cmpuint(matches[1].line,==,2);
 g_assert_true(insight_context_collect("foo food FOO",12,"foo",INSIGHT_SEARCH_WORD,matches,4,&count,&e));g_assert_cmpuint(count,==,2);
 g_assert_true(insight_context_collect("abc",3,"missing",0,NULL,0,&count,&e));g_assert_cmpuint(count,==,0);
 g_assert_false(insight_context_collect("abc",3,"[",INSIGHT_SEARCH_REGEX,matches,4,&count,&e));g_assert_nonnull(e);g_clear_error(&e);g_assert_cmpuint(count,==,0);
 g_assert_true(insight_context_collect("WriteOpenDoc",12,"~wod",0,matches,4,&count,&e));
 g_assert_cmpuint(count,==,1); g_assert_cmpuint(matches[0].end,==,10);
 g_assert_true(insight_context_collect("w\nod",4,"~wod",0,matches,4,&count,&e)); g_assert_cmpuint(count,==,0);
 g_assert_true(insight_context_collect("WriteOpenDoc",12,"~wod",INSIGHT_SEARCH_CASE,matches,4,&count,&e)); g_assert_cmpuint(count,==,0);
 g_assert_true(insight_context_collect("WriteOpenDoc",12,"~wod",INSIGHT_SEARCH_WORD,matches,4,&count,&e)); g_assert_cmpuint(count,==,0);
 g_assert_true(insight_context_collect("x\r\nТекст",strlen("x\r\nТекст"),"~тст",0,matches,4,&count,&e));
 g_assert_cmpuint(count,==,1); g_assert_cmpuint(matches[0].start,==,3); g_assert_cmpuint(matches[0].end,==,13); g_assert_cmpuint(matches[0].line,==,2);
 g_assert_false(insight_context_collect("abc",3,"~",0,matches,4,&count,&e)); g_assert_nonnull(e); g_clear_error(&e);
 /* Dense matches previously caused repeated expensive UTF-8 validation. */
 GString *dense=g_string_sized_new(1400000); for(guint n=0;n<100000;n++) g_string_append(dense,"function_0();\n");
 g_assert_true(insight_context_collect(dense->str,dense->len,"function_0",INSIGHT_SEARCH_CASE|INSIGHT_SEARCH_WORD,matches,4,&count,&e)); g_assert_no_error(e); g_assert_cmpuint(count,==,100000); g_assert_cmpuint(matches[3].line,==,4); g_assert_cmpuint(matches[3].start,==,42); g_string_free(dense,TRUE);
 g_print("Context search checks passed\n"); return 0;
}
