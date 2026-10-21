/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "symbol_edit.h"
#include <string.h>
static void check(const gchar *s,gsize a,gsize b,InsightSymbolEdit action,const gchar *clip,const gchar *eol,const gchar *expected,gsize first,gsize last) {
 InsightSymbolPlan plan={0};GError *e=NULL;g_assert_true(insight_symbol_plan(s,strlen(s),a,b,action,clip,eol,&plan,&e));g_assert_no_error(e);g_assert_cmpstr(plan.text,==,expected);g_assert_cmpuint(plan.selection_begin,==,first);g_assert_cmpuint(plan.selection_end,==,last);insight_symbol_plan_clear(&plan);
}
int main(void) {
 check("α\r\nlast",0,4,INSIGHT_SYMBOL_CUT,NULL,"\n","last",0,0);
 check("α",0,2,INSIGHT_SYMBOL_DUPLICATE,NULL,"\r\n","α\r\nα",4,6);
 check("a\nb\n",0,2,INSIGHT_SYMBOL_DUPLICATE,NULL,"\n","a\na\nb\n",2,4);
 check("a\rb",2,3,INSIGHT_SYMBOL_PASTE,"α","\r","a\rα\rb",2,4);
 check("a\r\n",0,3,INSIGHT_SYMBOL_PASTE,"β\n","\r\n","β\na\r\n",0,3);
 InsightSymbolPlan p={0};GError *e=NULL;
 g_assert_false(insight_symbol_plan("abc",3,1,3,INSIGHT_SYMBOL_CUT,NULL,"\n",&p,&e));g_clear_error(&e);
 g_assert_false(insight_symbol_plan("a\r\nb",4,0,2,INSIGHT_SYMBOL_CUT,NULL,"\n",&p,&e));g_clear_error(&e);
 g_assert_false(insight_symbol_plan("a",1,0,1,INSIGHT_SYMBOL_PASTE,"","\n",&p,&e));g_clear_error(&e);
 g_assert_false(insight_symbol_plan("a",1,0,1,INSIGHT_SYMBOL_PASTE,"\xff","\n",&p,&e));g_clear_error(&e);
 g_assert_false(insight_symbol_plan("a\0b",3,0,3,INSIGHT_SYMBOL_CUT,NULL,"\n",&p,&e));g_clear_error(&e);
 gchar *large=g_strnfill(INSIGHT_SYMBOL_TEXT_LIMIT+1,'x');g_assert_false(insight_symbol_plan("a",1,0,1,INSIGHT_SYMBOL_PASTE,large,"\n",&p,&e));g_clear_error(&e);g_free(large);
 g_print("Symbol plans: exact UTF-8/EOL, cut/duplicate/paste, separators, selections and bounds passed\n");return 0;
}
