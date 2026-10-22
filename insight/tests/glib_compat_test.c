/* SPDX-License-Identifier: GPL-2.0-or-later */
#define INSIGHT_GLIB_COMPAT_FORCE_FALLBACK 1
#include "../desktop/glib_compat.h"

static void paths(void) {
 const gchar *inputs[]={"", ".", "..", "a/../b", "../a//b/./../c", "///tmp/../tmp/x", "//server/share/a/../b", "тест/../имя.c", "a\\b"};
 gchar *cwd=g_get_current_dir(),*base=g_build_filename(cwd,"compat","root",NULL);
 for(guint n=0;n<G_N_ELEMENTS(inputs);n++) {
  for(guint b=0;b<2;b++) {
   gchar *actual=insight_canonicalize_filename(inputs[n],b?base:NULL);
   g_assert_nonnull(actual);g_assert_true(g_path_is_absolute(actual));
#if GLIB_CHECK_VERSION(2, 58, 0)
   gchar *expected=g_canonicalize_filename(inputs[n],b?base:NULL);
   g_assert_cmpstr(actual,==,expected);g_free(expected);
#endif
   g_free(actual);
  }
 }
 gchar *actual=insight_canonicalize_filename("../lib/./file.c",base),*expected=g_build_filename(cwd,"compat","lib","file.c",NULL);
 g_assert_cmpstr(actual,==,expected);g_free(actual);g_free(expected);
 actual=insight_canonicalize_filename("",NULL);g_assert_cmpstr(actual,==,cwd);g_free(actual);
 actual=insight_canonicalize_filename("./",base);g_assert_cmpstr(actual,==,base);g_free(actual);g_free(base);g_free(cwd);
}
static void patterns(void) {
 GPatternSpec *spec=g_pattern_spec_new("файл-?.c");
 g_assert_true(insight_pattern_match_string(spec,"файл-я.c"));
 g_assert_false(insight_pattern_match_string(spec,"файл-яя.c"));g_pattern_spec_free(spec);
 spec=g_pattern_spec_new("*.c");g_assert_true(insight_pattern_match_string(spec,"unit.c"));g_assert_false(insight_pattern_match_string(spec,"unit.h"));g_pattern_spec_free(spec);
 spec=g_pattern_spec_new("*");g_assert_true(insight_pattern_match_string(spec,""));g_pattern_spec_free(spec);
}
int main(int argc,char **argv) {
 g_test_init(&argc,&argv,NULL);g_test_add_func("/glib-compat/lexical-paths",paths);g_test_add_func("/glib-compat/unicode-patterns",patterns);return g_test_run();
}
