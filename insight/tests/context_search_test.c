#include "context_search.h"
#include <glib.h>
static void find(const char *s,const char *q,gsize from,gboolean forward,guint flags,gsize expected,gboolean wrap) {
 gsize a,b; gboolean w; GError *error=NULL; g_assert_true(insight_context_find(s,strlen(s),q,from,forward,flags,&a,&b,&w,&error)); g_assert_no_error(error); g_assert_cmpuint(a,==,expected); g_assert_cmpint(w,==,wrap);
}
int main(void) {
 find("foo Foo food","foo",4,TRUE,0,4,FALSE); find("foo Foo food","foo",4,TRUE,INSIGHT_SEARCH_CASE,8,FALSE);
 find("foo Foo food","foo",8,TRUE,INSIGHT_SEARCH_WORD,0,TRUE); find("foo Foo food","foo",4,FALSE,0,0,FALSE);
 find("foo Foo food","foo",0,FALSE,0,8,TRUE); find("a\nb","^b",0,TRUE,INSIGHT_SEARCH_REGEX,2,FALSE);
 find("тест ТЕСТ","тест",1,TRUE,0,9,FALSE); find("_foo foo1 foo","foo",0,TRUE,INSIGHT_SEARCH_WORD,10,FALSE);
 find("a\nb","^",1,TRUE,INSIGHT_SEARCH_REGEX,2,FALSE);
 gsize a,b; gboolean w; GError *e=NULL; g_assert_false(insight_context_find("a",1,"[",0,TRUE,INSIGHT_SEARCH_REGEX,&a,&b,&w,&e)); g_assert_nonnull(e); g_clear_error(&e);
 g_assert_false(insight_context_find("a",1,"missing",0,TRUE,0,&a,&b,&w,NULL)); g_print("Context search checks passed\n"); return 0;
}
