/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "renumber.h"
#include <string.h>
static void check(const gchar *input,gint mode,gint radix,const gchar *value,const gchar *expected,guint count) {
 GError *error=NULL;guint found=999;gchar *out=insight_renumber(input,mode,radix,value,&found,&error);g_assert_no_error(error);g_assert_cmpstr(out,==,expected);g_assert_cmpuint(found,==,count);g_free(out);
}
static void fail(const gchar *input,gint mode,gint radix,const gchar *value) {
 GError *error=NULL;guint found=999;gchar *out=insight_renumber(input,mode,radix,value,&found,&error);g_assert_null(out);g_assert_nonnull(error);g_assert_cmpuint(found,==,0);g_clear_error(&error);
}
int main(void) {
 check("4, 0x10, 020\r\nα 9\n",0,0,"ignored","4, 0x5, 06\r\nα 7\n",4);
 check("4, 0x10, 020",1,0,"10","10, 0xb, 014",3);
 check("4, 0x10, 020",2,1,"2","6, 18, 18",3);
 check("-2 +4 00 0XFF",1,2,"-2","-0x2 -0x1 0x0 0x1",4);
 check("1 2 3",1,3,"-01","-01 0 01",3);
 check("id12 12id _4 4_ α12 12α 1.2 .2 2. 1e3 2e-3 1.5e+2 0xgg 08 0b10 12U",0,1,NULL,"id12 12id _4 4_ α12 12α 1.2 .2 2. 1e3 2e-3 1.5e+2 0xgg 08 0b10 12U",0);
 check("0xFE-3",2,1,"0","254-3",2);
 check("\"4\" /* 8 */\t9",0,1,NULL,"\"4\" /* 5 */\t6",3);
 check("-9223372036854775808",2,2,"0","-0x8000000000000000",1);
 check("9223372036854775807",0,1,NULL,"9223372036854775807",1);
 check("0",1,1,"0x7fffffffffffffff","9223372036854775807",1);
 check("0 0",2,1,"-9223372036854775808","-9223372036854775808 -9223372036854775808",2);
 check("",0,0,NULL,"",0);
 fail("9223372036854775807 0",0,0,NULL);fail("9223372036854775808",0,0,NULL);
 fail("-9223372036854775808",2,1,"-1");fail("9223372036854775807",2,1,"1");
 fail("1",1,1,"08");fail("1",1,1,"");fail("1",1,1,"1 ");fail("1",1,1,"9223372036854775808");fail("1",3,0,NULL);fail("\xff",0,0,NULL);
 gchar *big=g_strnfill(8*1024*1024+1,' ');fail(big,0,0,NULL);g_free(big);
 /* Rectangular fragments share sequence state without joining token edges. */
 const gchar *source="α=4; keep=99\r\nβ=0x10; keep=88\r\nγ=020; keep=77\r\n";
 const gchar *p1=strstr(source,"4;"),*p2=strstr(source,"0x10"),*p3=strstr(source,"020");
 InsightRenumberRange ranges[]={{p1-source,p1-source+1,0,0,0},{p2-source,p2-source+4,0,0,0},{p3-source,p3-source+3,0,0,0}};
 GError *error=NULL;InsightRenumberPlan *plan=insight_renumber_plan(source,ranges,3,1,0,"10",&error);g_assert_no_error(error);g_assert_nonnull(plan);g_assert_cmpuint(plan->count,==,3);g_assert_cmpstr(plan->text,==,"α=10; keep=99\r\nβ=0xb; keep=88\r\nγ=014; keep=77\r\n");g_assert_cmpuint(g_array_index(plan->ranges,InsightRenumberRange,1).after_begin,==,(gsize)(strstr(plan->text,"0xb")-plan->text));insight_renumber_plan_free(plan);
 plan=insight_renumber_plan(source,ranges,3,0,1,NULL,&error);g_assert_no_error(error);g_assert_cmpstr(plan->text,==,"α=4; keep=99\r\nβ=5; keep=88\r\nγ=6; keep=77\r\n");insight_renumber_plan_free(plan);
 plan=insight_renumber_plan(source,ranges,3,2,1,"-2",&error);g_assert_no_error(error);g_assert_cmpstr(plan->text,==,"α=2; keep=99\r\nβ=14; keep=88\r\nγ=14; keep=77\r\n");insight_renumber_plan_free(plan);
 InsightRenumberRange separate[]={{0,1,0,0,0},{1,2,0,0,0}};plan=insight_renumber_plan("12",separate,2,1,1,"7",&error);g_assert_no_error(error);g_assert_cmpstr(plan->text,==,"78");g_assert_cmpuint(plan->count,==,2);insight_renumber_plan_free(plan);
 InsightRenumberRange empty[]={{1,1,0,0,0},{3,3,0,0,0}};plan=insight_renumber_plan("1\n2\n",empty,2,0,0,NULL,&error);g_assert_no_error(error);g_assert_cmpstr(plan->text,==,"1\n2\n");g_assert_cmpuint(plan->count,==,0);insight_renumber_plan_free(plan);
 g_assert_null(insight_renumber_plan("12",separate,2,1,1,"9223372036854775807",&error));g_assert_nonnull(error);g_clear_error(&error);
 InsightRenumberRange invalid[]={{0,2,0,0,0},{1,2,0,0,0}};g_assert_null(insight_renumber_plan("12",invalid,2,0,0,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);
 invalid[0]=(InsightRenumberRange){1,2,0,0,0};g_assert_null(insight_renumber_plan("α",invalid,1,0,0,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);
 invalid[0]=(InsightRenumberRange){0,2,0,0,0};g_assert_null(insight_renumber_plan("1\r\n",invalid,1,0,0,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);
 g_assert_null(insight_renumber_plan("1",ranges,4097,0,0,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);
 gchar *near=g_strnfill(8*1024*1024,' ');near[0]='1';InsightRenumberRange one={0,1,0,0,0};g_assert_null(insight_renumber_plan(near,&one,1,1,1,"10",&error));g_assert_nonnull(error);g_clear_error(&error);g_free(near);
 g_print("Renumber: modes, mixed radices, signs, Unicode/EOL, skipped candidates, signed boundaries and atomic failures passed\n");return 0;
}
