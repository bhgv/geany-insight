/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "calculate.h"
#include <locale.h>
#include <string.h>
static void check(const gchar *expression,const gchar *expected) { GError *error=NULL;gchar *result=insight_calculate(expression,NULL,&error);g_assert_no_error(error);g_assert_cmpstr(result,==,expected);g_free(result); }
static void fail(const gchar *expression) { GError *error=NULL;guint offset=99999;gchar *result=insight_calculate(expression,&offset,&error);g_assert_null(result);g_assert_nonnull(error);g_assert_cmpuint(offset,<=,strlen(expression));g_clear_error(&error); }
int main(void) {
 check("112*14","1568");check("2 + 3 * 4","14");check("(2+3)*4","20");check("2^3^2","512");check("-2^2","-4");check("(-2)^2","4");check("2^-2","0.25");check("5%2","1");check("-5%2","-1");check("\t +.5 + 1.5e1\r\n","15.5");check("008 + 2.","10");check("-0","0");
 check("min(2+3,max(7,9))","5");check("pow(2,3)+sqrt(9)","11");check("abs(-2)+floor(2.8)+ceil(2.1)+round(-2.5)","4");check("sin(0)+cos(0)+tan(0)+log(1)+exp(0)","2");check("round(pi)","3");check("floor(e)","2");check("1/3","0.33333333333333331");
 const gchar *bad[]={""," ","1/0","1%-0","sqrt(-1)","log(0)","exp(10000)","(-1)^.5","1e309","1e-9999","1e308*1e308","2+","()","1,2","min(1)","max(1,2,3)","sqrt()","abs(1,2)","2pi","0x10","1_000","NaN","Inf","system(1)","1;2","2**3","1e+","π","\xff"};
 for(guint i=0;i<G_N_ELEMENTS(bad);i++) fail(bad[i]);
 GString *nested=g_string_new(NULL);for(guint i=0;i<64;i++) g_string_append_c(nested,'(');g_string_append_c(nested,'1');for(guint i=0;i<64;i++) g_string_append_c(nested,')');check(nested->str,"1");g_string_prepend(nested,"(");g_string_append_c(nested,')');fail(nested->str);g_string_free(nested,TRUE);
 GString *operations=g_string_new("1");for(guint i=0;i<1100;i++) g_string_append(operations,"+1");GError *limit_error=NULL;g_assert_null(insight_calculate(operations->str,NULL,&limit_error));g_assert_nonnull(strstr(limit_error->message,"2048"));g_clear_error(&limit_error);g_string_free(operations,TRUE);
 gchar *large=g_strnfill(4097,'1');fail(large);g_free(large);
 /* Parser and formatting always use a dot regardless of process locale. */
 setlocale(LC_NUMERIC,"");check("1.5+2.5","4");
 g_print("Calculate: precedence, right-associative powers, unary signs, finite decimals/functions, errors, zero/domain/range/nesting/input limits and locale passed\n");return 0;
}
