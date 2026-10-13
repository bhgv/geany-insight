/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "search_state.h"
#include <glib/gstdio.h>
#include <string.h>
int main(void) {
 GError *error=NULL; gchar *path=g_strdup_printf("search-state-%u.conf",g_random_int()),*cwd=g_get_current_dir(); InsightSearchState *state=insight_search_state_new();
 g_assert_true(insight_search_state_save(state,path,&error)); InsightSearchState *copy=insight_search_state_load(path,&error); g_assert_no_error(error); g_assert_cmpuint(copy->files->len,==,0); insight_search_state_free(copy);
 g_free(state->query); state->query=g_strdup("(документ OR \"read write\") !error"); g_free(state->mask); state->mask=g_strdup("*.c;*.h;*.py"); state->flags=3; state->scope=3; state->keywords=TRUE; state->variations=TRUE; state->context=50;
 g_ptr_array_add(state->files,g_build_filename(cwd,"папка; с пробелами","file.c",NULL)); g_ptr_array_add(state->files,g_build_filename(cwd,"other.h",NULL));
 g_assert_true(insight_search_state_save(state,path,&error)); copy=insight_search_state_load(path,&error); g_assert_no_error(error); g_assert_cmpstr(copy->query,==,state->query); g_assert_cmpstr(copy->mask,==,state->mask); g_assert_cmpuint(copy->context,==,50); g_assert_true(copy->keywords); g_assert_true(copy->variations); g_assert_cmpuint(copy->flags,==,3); g_assert_cmpuint(copy->scope,==,3); g_assert_cmpuint(copy->files->len,==,2); g_assert_cmpstr(g_ptr_array_index(copy->files,0),==,g_ptr_array_index(state->files,0)); insight_search_state_free(copy);
 GKeyFile *old=g_key_file_new(); g_assert_true(g_key_file_load_from_file(old,path,G_KEY_FILE_NONE,&error)); g_assert_true(g_key_file_remove_key(old,"search","variations",NULL)); gsize old_size; gchar *old_data=g_key_file_to_data(old,&old_size,NULL); g_assert_true(g_file_set_contents(path,old_data,old_size,&error)); copy=insight_search_state_load(path,&error); g_assert_no_error(error); g_assert_false(copy->variations); insight_search_state_free(copy); g_free(old_data); g_key_file_unref(old); g_assert_true(insight_search_state_save(state,path,&error));
 /* Invalid state must leave the previously saved selection intact. */
 state->context=51; g_assert_false(insight_search_state_save(state,path,&error)); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&error); copy=insight_search_state_load(path,&error); g_assert_no_error(error); g_assert_cmpuint(copy->context,==,50); insight_search_state_free(copy); state->context=3;
 g_ptr_array_add(state->files,g_strdup("relative.c")); g_assert_false(insight_search_state_save(state,path,&error)); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&error); g_ptr_array_remove_index(state->files,state->files->len-1);
 const char *invalid[]={"[search]\nversion=2\n","[search]\nversion=1\nquery=x\nmask=*\nflags=-1\nscope=0\ncontext=0\nkeywords=false\nfiles=\n","[search]\nversion=1\nquery=x\nmask=*\nflags=0\nscope=0\ncontext=0\nkeywords=garbage\nfiles=\n"};
 for(guint i=0;i<G_N_ELEMENTS(invalid);i++) { g_assert_true(g_file_set_contents(path,invalid[i],-1,&error)); copy=insight_search_state_load(path,&error); g_assert_null(copy); g_assert_nonnull(error); g_clear_error(&error); }
 const char nul[]="[search]\nversion=1\n\0trailing"; g_assert_true(g_file_set_contents(path,nul,sizeof nul-1,&error)); copy=insight_search_state_load(path,&error); g_assert_null(copy); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&error);
 insight_search_state_free(state); g_remove(path); g_free(path); g_free(cwd); g_print("Persistent search settings checks passed\n"); return 0;
}
