/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/custom_command.h"
#include <glib/gstdio.h>
#include <string.h>
int main(void) {
 GHashTable *context=g_hash_table_new(g_str_hash,g_str_equal);g_hash_table_insert(context,"f","/tmp/source name;$(echo BAD).c");g_hash_table_insert(context,"j","/project dir");g_hash_table_insert(context,"J","/project data");g_hash_table_insert(context,"PROJECT_NAME","Пример");g_hash_table_insert(context,"1","--one two");g_hash_table_insert(context,"w","");
 GError *error=NULL;gchar **argv=insight_custom_argv("tool --file=%f '%j' %J %project_name% %1 %w %%",context,&error);g_assert_no_error(error);g_assert_cmpuint(g_strv_length(argv),==,8);g_assert_cmpstr(argv[1],==,"--file=/tmp/source name;$(echo BAD).c");g_assert_cmpstr(argv[2],==,"/project dir");g_assert_cmpstr(argv[3],==,"/project data");g_assert_cmpstr(argv[4],==,"Пример");g_assert_cmpstr(argv[5],==,"--one two");g_assert_cmpstr(argv[6],==,"");g_assert_cmpstr(argv[7],==,"%");g_strfreev(argv);
 const gchar *bad[]={"tool %missing%","tool %x","tool %","tool 'unterminated", "", "\377", "tool\nother"};for(guint i=0;i<G_N_ELEMENTS(bad);i++) {g_assert_null(insight_custom_argv(bad[i],context,&error));g_assert_nonnull(error);g_clear_error(&error);}
 gchar *text=insight_custom_expand("%j/%PROJECT_NAME%/100%%",context,&error);g_assert_no_error(error);g_assert_cmpstr(text,==,"/project dir/Пример/100%");g_free(text);
 /* A percent in a substituted filename is never expanded a second time. */
 g_hash_table_insert(context,"n","%PROJECT_NAME%");text=insight_custom_expand("%n",context,&error);g_assert_cmpstr(text,==,"%PROJECT_NAME%");g_free(text);
 GString *many=g_string_new("tool");for(guint i=0;i<129;i++) g_string_append(many," x");g_assert_null(insight_custom_argv(many->str,context,&error));g_clear_error(&error);g_string_free(many,TRUE);
 gchar *dir=g_dir_make_tmp("insight-custom-XXXXXX",&error);g_assert_no_error(error);gchar *path=g_build_filename(dir,"commands.conf",NULL);GPtrArray *list=insight_custom_commands_load(path,&error);g_assert_no_error(error);g_assert_cmpuint(list->len,==,0);
 InsightCustomCommand *c=g_new0(InsightCustomCommand,1);c->name=g_strdup("Compiler — проект");c->command=g_strdup("cc -fsyntax-only %f");c->directory=g_strdup("%j");g_ptr_array_add(list,c);g_assert_true(insight_custom_commands_save(path,list,&error));g_assert_no_error(error);GPtrArray *copy=insight_custom_commands_load(path,&error);g_assert_no_error(error);g_assert_cmpuint(copy->len,==,1);InsightCustomCommand *loaded=g_ptr_array_index(copy,0);g_assert_cmpstr(loaded->name,==,c->name);g_assert_cmpstr(loaded->command,==,c->command);g_assert_cmpstr(loaded->directory,==,c->directory);g_ptr_array_unref(copy);
 g_free(c->name);c->name=g_strdup("");g_assert_false(insight_custom_commands_save(path,list,&error));g_assert_nonnull(error);g_clear_error(&error);copy=insight_custom_commands_load(path,&error);g_assert_no_error(error);g_assert_cmpstr(((InsightCustomCommand *)g_ptr_array_index(copy,0))->name,==,"Compiler — проект");g_ptr_array_unref(copy);g_ptr_array_unref(list);
 g_assert_true(g_file_set_contents(path,"[commands]\ncount=65\n",-1,&error));g_assert_null(insight_custom_commands_load(path,&error));g_assert_nonnull(error);g_clear_error(&error);
 gchar *huge=g_strnfill(1024*1024+1,'x');g_assert_true(g_file_set_contents(path,huge,-1,&error));g_free(huge);g_assert_null(insight_custom_commands_load(path,&error));g_assert_nonnull(error);g_clear_error(&error);
 g_remove(path);g_rmdir(dir);g_free(path);g_free(dir);g_hash_table_unref(context);g_print("Custom command argument boundaries, Unicode, missing contexts, single-pass expansion, limits and atomic profile persistence passed\n");return 0;
}
