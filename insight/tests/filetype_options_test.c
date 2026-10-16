/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/filetype_options.h"
int main(void)
{
 GKeyFile *key=g_key_file_new();InsightFiletypeOptions p,q;GError *error=NULL;
 g_assert_true(insight_filetype_options_read(&p,key,&error));g_assert_no_error(error);g_assert_false(p.enabled);
 g_assert_true(p.allow_completion);g_assert_true(p.allow_snippets);g_assert_true(p.typing_shortcuts);g_assert_true(p.enter_newline);
 p.allow_completion=FALSE;p.allow_snippets=FALSE;p.typing_shortcuts=FALSE;p.enter_newline=FALSE;
 p.enabled=TRUE;p.indent_type=2;p.tab_width=8;p.indent_width=2;p.margin=120;p.soft_wrap=TRUE;p.white_space=TRUE;
 g_assert_true(insight_filetype_options_write(&p,key,&error));g_assert_true(insight_filetype_options_read(&q,key,&error));g_assert_no_error(error);g_assert_cmpint(q.tab_width,==,8);g_assert_cmpint(q.indent_width,==,2);g_assert_cmpint(q.margin,==,120);g_assert_true(q.enabled);g_assert_true(q.soft_wrap);g_assert_true(q.white_space);
 g_assert_false(q.allow_completion);g_assert_false(q.allow_snippets);g_assert_false(q.typing_shortcuts);g_assert_false(q.enter_newline);
 for(guint i=0;i<4;i++) { const gchar *keys[]={"allow-completion","allow-snippets","typing-shortcuts","enter-newline"};g_key_file_remove_key(key,"editing",keys[i],NULL); }
 g_assert_true(insight_filetype_options_read(&q,key,&error));g_assert_true(q.allow_completion);g_assert_true(q.allow_snippets);g_assert_true(q.typing_shortcuts);g_assert_true(q.enter_newline);
 g_key_file_set_string(key,"editing","enter-newline","invalid");g_assert_false(insight_filetype_options_read(&q,key,&error));g_clear_error(&error);g_assert_false(q.enabled);g_key_file_remove_key(key,"editing","enter-newline",NULL);
 gchar *before=g_key_file_to_data(key,NULL,NULL);p.tab_width=0;g_assert_false(insight_filetype_options_write(&p,key,&error));g_clear_error(&error);gchar *after=g_key_file_to_data(key,NULL,NULL);g_assert_cmpstr(before,==,after);g_free(before);g_free(after);
 g_key_file_set_integer(key,"editing","margin",501);g_assert_false(insight_filetype_options_read(&q,key,&error));g_clear_error(&error);g_assert_false(q.enabled);g_assert_cmpint(q.margin,==,80);
 g_key_file_set_integer(key,"editing","margin",80);g_key_file_set_string(key,"editing","enabled","maybe");g_assert_false(insight_filetype_options_read(&q,key,&error));g_clear_error(&error);g_assert_false(q.enabled);
 g_key_file_set_boolean(key,"editing","enabled",TRUE);g_key_file_set_integer(key,"editing","version",999);g_assert_false(insight_filetype_options_read(&q,key,&error));g_clear_error(&error);g_assert_false(q.enabled);
 g_key_file_unref(key);g_print("File type editing options: inheritance, roundtrip, bounds, malformed values and nonmutating failed writes passed\n");return 0;
}
