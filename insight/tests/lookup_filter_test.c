#include "lookup_filter.h"
#include <glib.h>
static void check(const char *q,gboolean files,const char *name,const char *scope,const char *path,const char *kind,gboolean expected) {
 GError *error=NULL; InsightLookupFilter *f=insight_lookup_filter_new(q,files,&error); g_assert_no_error(error); g_assert_nonnull(f); g_assert_cmpint(insight_lookup_filter_match(f,name,scope,path,kind),==,expected); insight_lookup_filter_free(f);
}
int main(void) {
 check("doc write",FALSE,"WriteOpenDoc","","a.c","f",TRUE);
 check("doc read",FALSE,"WriteOpenDoc","","a.c","f",FALSE);
 check("\"write\"",FALSE,"Write","","a.c","f",TRUE);
 check("\"write\"",FALSE,"WriteDoc","","a.c","f",FALSE);
 check("?^Write.*Doc$",FALSE,"WriteDoc","","a.c","f",TRUE);
 check("Write(",FALSE,"WriteDoc","","a.c","v",FALSE);
 check("Write(",FALSE,"WriteDoc","","a.c","f",TRUE);
 check("scope=Widget @src write",FALSE,"WriteDoc","Widget::Impl","src/a.c","m",TRUE);
 check("scope=Other",FALSE,"WriteDoc","Widget","a.c","f",FALSE);
 check("*src/*.cpp",TRUE,"a.cpp","","root/src/a.cpp","",TRUE);
 check("src test",TRUE,"test.cpp","","src/test.cpp","",TRUE);
 check("тест",FALSE,"ТЕСТ","","a.c","f",TRUE);
 GError *error=NULL; InsightLookupFilter *f=insight_lookup_filter_new("?[",FALSE,&error); g_assert_null(f); g_assert_nonnull(error); g_clear_error(&error);
 g_assert_null(insight_lookup_filter_new("\xff",FALSE,NULL));
 g_print("Lookup filters passed\n"); return 0;
}
