/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "source_mask.h"
#include "../core/occurrences.h"
#include <string.h>
static void check(const char *text,InsightCodeDialect dialect,gsize expected) {
 GError *error=NULL; gchar *mask=insight_source_code_mask(text,strlen(text),dialect,&error); g_assert_no_error(error); g_assert_nonnull(mask); g_assert_cmpuint(strlen(mask),==,strlen(text));
 for(gsize i=0;i<strlen(text);i++) if(text[i]=='\r' || text[i]=='\n') g_assert_cmpint(mask[i],==,text[i]);
 InsightOccurrence results[16]; size_t count=0; g_assert_true(insight_occurrences_find(mask,strlen(mask),"target",6,0,results,16,&count)); g_assert_cmpuint(count,==,expected);
 for(gsize i=0;i<count;i++) g_assert_cmpmem(text+results[i].start,6,"target",6);
 g_free(mask);
}
int main(void) {
 check("target(); // target\r\n/* target */ target(); \"target\\\" target\"",INSIGHT_CODE_C,2);
 check("target(); // target\\\n target\n target();",INSIGHT_CODE_C,2);
 check("1'000; target(); 0xA'B; target();",INSIGHT_CODE_C,2);
 check("R\"tag(target \" target)tag\"; target();",INSIGHT_CODE_C,1);
 check("target() # target\n'''target\n target'''\n\"\"\"target\"\"\";target()",INSIGHT_CODE_PYTHON,2);
 check("target() -- target\n--[=[target\n target]=]\n[==[target]==];target()",INSIGHT_CODE_LUA,2);
 check("`target ${target}`; // target\n target()",INSIGHT_CODE_JS,1);
 check("# target\necho 'target' \"target\"; target",INSIGHT_CODE_SHELL,1);
 check("\"кириллица target\" target /*unterminated target",INSIGHT_CODE_C,1);
 GError *error=NULL; g_assert_null(insight_source_code_mask("a\0b",3,INSIGHT_CODE_C,&error)); g_assert_nonnull(error); g_clear_error(&error);
 g_assert_null(insight_source_code_mask("\xff",1,INSIGHT_CODE_C,NULL));
 g_print("Source mask checks passed\n"); return 0;
}
