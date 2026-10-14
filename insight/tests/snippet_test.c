/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "snippet.h"
#include <gio/gio.h>
#include <string.h>
static void expansion(void) {
 GHashTable *values=g_hash_table_new(g_str_hash,g_str_equal); g_hash_table_insert(values,"name","имя"); g_hash_table_insert(values,"selection","$name$\r\nтекст");
 GError *error=NULL; gsize cursor=999; gchar *text=insight_snippet_expand("$$name$$:$name$\n$cursor$$selection$:$name$$cursor$",values,"\r\n",&cursor,&error);
 g_assert_no_error(error); g_assert_cmpstr(text,==,"$name$:имя\r\n$name$\r\nтекст:имя"); g_assert_cmpuint(cursor,==,strlen("$name$:имя\r\n")); g_free(text);
 text=insight_snippet_expand("$name$",values,"\n",&cursor,&error); g_assert_cmpuint(cursor,==,strlen("имя")); g_free(text);
 text=insight_snippet_expand("$x$",values,"\n",&cursor,&error); g_assert_null(text); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&error);
 text=insight_snippet_expand("hello",values,"bad",&cursor,&error); g_assert_null(text); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT); g_clear_error(&error);
 GPtrArray *variables=insight_snippet_variables("$name$ $$escape$$ $cursor$ $name$ $second$ $9bad$ end$",&error); g_assert_no_error(error); g_assert_cmpuint(variables->len,==,2); g_assert_cmpstr(g_ptr_array_index(variables,0),==,"name"); g_assert_cmpstr(g_ptr_array_index(variables,1),==,"second"); g_ptr_array_unref(variables);
 g_hash_table_unref(values);
}
static void limits(void) {
 GError *error=NULL; GString *input=g_string_new(NULL); for(guint i=0;i<65;i++) g_string_append_printf(input,"$variable%u$",i); GPtrArray *vars=insight_snippet_variables(input->str,&error); g_assert_null(vars); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE); g_clear_error(&error); g_string_free(input,TRUE);
 gchar *large=g_strnfill(65536,'x'); GHashTable *values=g_hash_table_new(g_str_hash,g_str_equal); g_hash_table_insert(values,"x",large); gsize cursor=99;
 gchar *out=insight_snippet_expand("$x$$x$$x$$x$$x$",values,"\n",&cursor,&error); g_assert_null(out); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE); g_clear_error(&error); g_assert_cmpuint(cursor,==,99); g_hash_table_unref(values); g_free(large);
}
int main(int argc,char **argv) { g_test_init(&argc,&argv,NULL); g_test_add_func("/snippet/expansion",expansion); g_test_add_func("/snippet/limits",limits); return g_test_run(); }
