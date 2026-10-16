/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/tagmanager/tm_completion.h"
static void match(const gchar *prefix,const gchar *name,gboolean sensitive,gboolean expected) { TMCompletionPrefix *p=tm_completion_prefix_new(prefix,sensitive);g_assert_nonnull(p);g_assert_cmpint(tm_completion_prefix_matches(p,name),==,expected);tm_completion_prefix_free(p); }
int main(void) {
 match("ber","BerryCount",TRUE,FALSE);match("ber","berryCount",TRUE,TRUE);match("bEr","BerryCount",FALSE,TRUE);match("BERRY","berryCount",FALSE,TRUE);match("berryCountLong","berryCount",FALSE,FALSE);match("berry","other_berry",FALSE,FALSE);match("","Berry",FALSE,TRUE);
 match("ПРИ","привет",FALSE,TRUE);match("при","Привет",TRUE,FALSE);match("STRASS","Straße",FALSE,TRUE);match("σ","Σigma",FALSE,TRUE);match("k","Kelvin",FALSE,TRUE);match("e\314\201","éclair",FALSE,FALSE);
 g_assert_null(tm_completion_prefix_new(NULL,FALSE));g_assert_null(tm_completion_prefix_new("\377",FALSE));g_assert_null(tm_completion_prefix_new("a\nb",FALSE));gchar *long_prefix=g_strnfill(513,'a');g_assert_null(tm_completion_prefix_new(long_prefix,FALSE));long_prefix[512]=0;TMCompletionPrefix *p=tm_completion_prefix_new(long_prefix,FALSE);g_assert_nonnull(p);g_assert_false(tm_completion_prefix_matches(p,NULL));g_assert_false(tm_completion_prefix_matches(p,"\377"));tm_completion_prefix_free(p);g_free(long_prefix);
 gchar *name=g_strnfill(4097,'a');p=tm_completion_prefix_new("a",FALSE);g_assert_false(tm_completion_prefix_matches(p,name));name[4096]=0;g_assert_true(tm_completion_prefix_matches(p,name));tm_completion_prefix_free(p);g_free(name);g_print("Completion matching: exact case, ASCII/Unicode casefold, expansion, prefix-only, malformed UTF-8 and byte bounds passed\n");return 0;
}
