/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "folder_layout.h"
#include <string.h>
int main(void) {
 g_assert_false(insight_folder_layout_vertical(TRUE,TRUE,900,400,TRUE));
 g_assert_true(insight_folder_layout_vertical(TRUE,FALSE,400,900,FALSE));
 g_assert_true(insight_folder_layout_vertical(TRUE,FALSE,250,100,FALSE));
 g_assert_true(insight_folder_layout_vertical(TRUE,FALSE,520,500,TRUE));
 g_assert_false(insight_folder_layout_vertical(TRUE,TRUE,500,520,FALSE));
 g_assert_false(insight_folder_layout_vertical(FALSE,FALSE,200,900,TRUE));
 g_assert_true(insight_folder_layout_vertical(FALSE,TRUE,900,200,FALSE));
 g_assert_true(insight_folder_layout_vertical(TRUE,FALSE,0,100,TRUE));
 g_assert_false(insight_folder_layout_vertical(TRUE,FALSE,G_MAXINT,1,TRUE));
 g_assert_true(insight_folder_layout_vertical(TRUE,FALSE,320,G_MAXINT,FALSE));
 gchar *cwd=g_get_current_dir(),*path=g_build_filename(cwd,"каталог","подпапка",NULL);
 GPtrArray *paths=insight_folder_ancestry(path,64);g_assert_nonnull(paths);g_assert_cmpuint(paths->len,>=,3);g_assert_cmpstr(g_ptr_array_index(paths,paths->len-1),==,path);
 for(guint i=1;i<paths->len;i++) { gchar *parent=g_path_get_dirname(g_ptr_array_index(paths,i));g_assert_cmpstr(parent,==,g_ptr_array_index(paths,i-1));g_free(parent); }
 gchar *root=g_strdup(g_ptr_array_index(paths,0));g_ptr_array_unref(paths);paths=insight_folder_ancestry(root,64);g_assert_cmpuint(paths->len,==,1);g_ptr_array_unref(paths);g_free(root);
 paths=insight_folder_ancestry(path,2);g_assert_cmpuint(paths->len,==,2);g_assert_cmpstr(g_ptr_array_index(paths,1),==,path);g_ptr_array_unref(paths);
 GString *deep=g_string_new(cwd);for(guint i=0;i<100;i++) g_string_append_printf(deep,"%cx",G_DIR_SEPARATOR);
 paths=insight_folder_ancestry(deep->str,1000);g_assert_cmpuint(paths->len,==,64);g_assert_cmpstr(g_ptr_array_index(paths,63),==,deep->str);g_ptr_array_unref(paths);g_string_free(deep,TRUE);
 g_assert_null(insight_folder_ancestry("relative",64));g_assert_null(insight_folder_ancestry(path,0));g_assert_null(insight_folder_ancestry(NULL,64));
 gchar *invalid=g_strconcat(cwd,G_DIR_SEPARATOR_S,"bad\nname",NULL);g_assert_null(insight_folder_ancestry(invalid,64));g_free(invalid);g_free(cwd);g_free(path);
 g_print("Folder layout: shape/manual/hysteresis and bounded Unicode ancestry passed\n");return 0;
}
