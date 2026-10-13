/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "keyword_search.h"
#include <string.h>
static gsize count(const char *text,const char *query,guint flags,guint context) {
 GError *error=NULL; InsightSearchMatch found[32]; gsize total=0;
 g_assert_true(insight_keyword_collect(text,strlen(text),query,flags,context,found,G_N_ELEMENTS(found),&total,&error)); g_assert_no_error(error);
 for(gsize i=0;i<MIN(total,G_N_ELEMENTS(found));i++) { g_assert_cmpuint(found[i].start,<=,found[i].end); g_assert_cmpuint(found[i].end,<=,strlen(text)); }
 return total;
}
int main(void) {
 g_assert_cmpuint(count("OpenDoc SaveRemote\nerror\nSaveRemote\nOpenDoc\n","doc remote !error",0,0),==,2);
 g_assert_cmpuint(count("cat\ndog\n","cat dog",0,0),==,0);
 g_assert_cmpuint(count("cat\ndog\n","cat dog",0,1),==,2);
 g_assert_cmpuint(count("cat food\nkitty food\ncat\n","(cat OR kitty) AND food",INSIGHT_SEARCH_WORD,0),==,4);
 g_assert_cmpuint(count("cat\ndog food\n","cat OR dog food",INSIGHT_SEARCH_WORD,0),==,3);
 g_assert_cmpuint(count("OPEN open\n","=OPEN",0,0),==,1);
 g_assert_cmpuint(count("Документ сохранить\n","док сохранить",0,0),==,2);
 g_assert_cmpuint(count("cat food\ncatfood\n","\"cat food\"",0,0),==,1);
 g_assert_cmpuint(count("foo12 bar\nfoo bar\n","?\"foo[0-9]+\" bar",0,0),==,2);
 g_assert_cmpuint(count("error\r\nclean\r\n","NOT error",0,0),==,2);
 g_assert_cmpuint(count("cat\n","NOT NOT cat",0,0),==,1);
 g_assert_cmpuint(count("neutral","cat OR NOT dog",0,0),==,1);
 g_assert_cmpuint(count("dog food","cat OR dog AND missing OR food",INSIGHT_SEARCH_WORD,0),==,1);
 g_assert_cmpuint(count("beta gamma","(alpha AND beta) OR gamma",INSIGHT_SEARCH_WORD,0),==,1);
 /* Exhaust every assignment for mixed positive/negative branches, independently
  * of the match-rendering implementation and without a synthetic EOF line. */
 for(guint bits=0;bits<8;bits++) {
  gchar *text=g_strdup_printf("neutral%s%s%s",bits&1 ? " alpha" : "",bits&2 ? " beta" : "",bits&4 ? " gamma" : "");
  gboolean a=(bits&1)!=0,b=(bits&2)!=0,c=(bits&4)!=0;
  g_assert_cmpint(count(text,"alpha OR NOT beta",INSIGHT_SEARCH_WORD,0)>0,==,a||!b);
  g_assert_cmpint(count(text,"NOT (alpha AND beta)",INSIGHT_SEARCH_WORD,0)>0,==,!(a&&b));
  g_assert_cmpint(count(text,"(alpha OR !beta) AND (gamma OR NOT alpha)",INSIGHT_SEARCH_WORD,0)>0,==,(a||!b)&&(c||!a));
  g_assert_cmpint(count(text,"NOT NOT alpha",INSIGHT_SEARCH_WORD,0)>0,==,a);
  g_free(text);
 }
 const char *bad[]={"","cat AND","(cat OR dog","\"unterminated","?\"[\"","cat)"};
 for(guint i=0;i<G_N_ELEMENTS(bad);i++) { GError *error=NULL; gsize total=0; g_assert_false(insight_keyword_collect("x",1,bad[i],0,0,NULL,0,&total,&error)); g_assert_nonnull(error); g_clear_error(&error); }
 InsightSearchMatch match; gsize total=0; g_assert_true(insight_keyword_collect("cat cat",7,"cat OR cat",0,0,&match,1,&total,NULL)); g_assert_cmpuint(total,==,2); g_assert_cmpuint(match.start,==,0);
 g_print("Keyword search checks passed\n"); return 0;
}
