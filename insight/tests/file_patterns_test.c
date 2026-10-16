/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "file_patterns.h"
#include <string.h>
int main(void) {
 GError *error=NULL;InsightFilePatterns *p=insight_file_patterns_new("Makefile; *.inc,Т?ст.h *",&error);g_assert_no_error(error);g_assert_nonnull(p);
 g_assert_true(insight_file_patterns_match(p,"/root/Makefile",INSIGHT_FILE_PATTERN_EXACT));g_assert_false(insight_file_patterns_match(p,"/Makefile/other",INSIGHT_FILE_PATTERN_EXACT));g_assert_true(insight_file_patterns_match(p,"/root/a.inc",INSIGHT_FILE_PATTERN_GLOB));g_assert_false(insight_file_patterns_match(p,"/dir.inc/other",INSIGHT_FILE_PATTERN_GLOB));g_assert_true(insight_file_patterns_match(p,"/root/Тест.h",INSIGHT_FILE_PATTERN_GLOB));g_assert_true(insight_file_patterns_match(p,"/root/unrecognized",INSIGHT_FILE_PATTERN_DEFAULT));g_assert_false(insight_file_patterns_match(p,"/root/unrecognized",INSIGHT_FILE_PATTERN_EXACT));g_assert_true(insight_file_patterns_match(p,"/root/any",INSIGHT_FILE_PATTERN_ANY));
#ifdef G_OS_WIN32
 g_assert_true(insight_file_patterns_match(p,"C:\\root\\MAKEFILE",INSIGHT_FILE_PATTERN_EXACT));
#else
 g_assert_false(insight_file_patterns_match(p,"/root/MAKEFILE",INSIGHT_FILE_PATTERN_EXACT));
#endif
 gchar **tokens=insight_file_patterns_tokens(p);g_assert_cmpuint(g_strv_length(tokens),==,4);g_assert_cmpstr(tokens[0],==,"Makefile");g_strfreev(tokens);insight_file_patterns_free(p);
 const gchar *bad[]={"../file","C:thing","root/file","root\\file","\"two names\"","\xff"};for(guint i=0;i<G_N_ELEMENTS(bad);i++) { p=insight_file_patterns_new(bad[i],&error);g_assert_null(p);g_assert_nonnull(error);g_clear_error(&error); }
 gchar *long_one=g_strnfill(256,'a');p=insight_file_patterns_new(long_one,&error);g_assert_null(p);g_clear_error(&error);g_free(long_one);GString *many=g_string_new(NULL);for(guint i=0;i<257;i++) g_string_append(many,"a;");p=insight_file_patterns_new(many->str,&error);g_assert_null(p);g_clear_error(&error);g_string_free(many,TRUE);long_one=g_strnfill(4097,' ');p=insight_file_patterns_new(long_one,&error);g_assert_null(p);g_clear_error(&error);g_free(long_one);p=insight_file_patterns_new("",&error);g_assert_no_error(error);g_assert_false(insight_file_patterns_match(p,"any",INSIGHT_FILE_PATTERN_ANY));insight_file_patterns_free(p);g_assert_false(insight_file_patterns_match(NULL,"any",-1));g_print("File patterns: Unicode, exact/glob/default stages, basename, platform case and validation bounds passed\n");return 0;
}
