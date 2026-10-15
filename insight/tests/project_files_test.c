/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "project_files.h"
#include <string.h>
int main(void) {
 gchar *cwd=g_get_current_dir(),*root=g_build_filename(cwd,"project",NULL),*other=g_build_filename(cwd,"external",NULL),*a=g_build_filename(root,"src","a.c",NULL),*b=g_build_filename(root,"src2","b.c",NULL),*tree=g_build_filename(root,"src",NULL),*child=g_build_filename(tree,"keep",NULL),*keep=g_build_filename(child,"k.c",NULL),*external=g_build_filename(other,"shared.h",NULL);
 InsightProjectFiles *s=insight_project_files_new(); GError *error=NULL;
 g_assert_true(insight_project_files_contains(s,root,a)); g_assert_false(insight_project_files_contains(s,root,external));
 g_assert_true(insight_project_files_set(s,tree,TRUE,FALSE,&error)); g_assert_false(insight_project_files_contains(s,root,a)); g_assert_true(insight_project_files_contains(s,root,b));
 g_assert_true(insight_project_files_set(s,a,FALSE,TRUE,&error)); g_assert_true(insight_project_files_contains(s,root,a));
 g_assert_true(insight_project_files_set(s,a,FALSE,FALSE,&error)); g_assert_false(insight_project_files_contains(s,root,a));
 g_assert_true(insight_project_files_set(s,child,TRUE,TRUE,&error)); g_assert_true(insight_project_files_contains(s,root,keep));
 g_assert_true(insight_project_files_set(s,tree,TRUE,TRUE,&error)); g_assert_true(insight_project_files_contains(s,root,a)); g_assert_cmpuint(s->excluded_files->len,==,0); g_assert_cmpuint(s->excluded_trees->len,==,0);
 g_assert_true(insight_project_files_set(s,external,FALSE,TRUE,&error)); g_assert_true(insight_project_files_contains(s,root,external));
 g_free(s->masks); s->masks=g_strdup("*.h;src/*.c"); g_assert_false(insight_project_files_contains(s,root,external)); g_assert_false(insight_project_files_contains(s,root,a)); g_assert_true(insight_project_files_contains(s,root,b));
 s->automatic=FALSE; g_assert_false(insight_project_files_contains(s,root,b));
 GKeyFile *config=g_key_file_new(); g_key_file_set_string(config,"project","name","Existing project");
 g_assert_true(insight_project_files_write(s,config,root,&error)); g_assert_no_error(error);
 gchar *encoded=g_key_file_get_string(config,"insight-project-files","trees",NULL); g_assert_cmpstr(encoded,==,"src;"); g_free(encoded);
 gchar *moved=g_build_filename(cwd,"перенесённый проект",NULL),*moved_a=g_build_filename(moved,"src","a.c",NULL);
 InsightProjectFiles *copy=insight_project_files_read(config,moved,&error); g_assert_no_error(error); g_assert_false(copy->automatic); g_assert_false(insight_project_files_contains(copy,moved,moved_a));
 g_free(copy->masks); copy->masks=g_strdup(""); g_assert_true(insight_project_files_contains(copy,moved,moved_a)); g_assert_true(insight_project_files_contains(copy,moved,external)); insight_project_files_free(copy);
 encoded=g_key_file_get_string(config,"project","name",NULL); g_assert_cmpstr(encoded,==,"Existing project"); g_free(encoded);
 copy=insight_project_files_clone(s); g_assert_true(insight_project_files_set(copy,b,FALSE,TRUE,&error)); g_assert_true(insight_project_files_contains(copy,root,b)); g_assert_false(insight_project_files_contains(s,root,b)); insight_project_files_free(copy);
 g_assert_false(insight_project_files_set(s,"relative.c",FALSE,TRUE,&error)); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT); g_clear_error(&error);
 gchar *special=g_build_filename(root,"имя; с пробелами.c",NULL); g_assert_true(insight_project_files_set(s,special,FALSE,TRUE,&error)); g_assert_true(insight_project_files_write(s,config,root,&error)); copy=insight_project_files_read(config,root,&error); g_assert_no_error(error); g_assert_true(insight_project_files_contains(copy,root,special)); insight_project_files_free(copy); g_free(special);
 gchar *master=g_build_filename(root,"master files.txt",NULL);g_free(s->master_file);s->master_file=g_strdup(master);g_assert_true(insight_project_files_write(s,config,root,&error));encoded=g_key_file_get_string(config,"insight-project-files","master-file",NULL);g_assert_cmpstr(encoded,==,"master files.txt");g_free(encoded);copy=insight_project_files_read(config,moved,&error);g_assert_no_error(error);gchar *moved_master=g_build_filename(moved,"master files.txt",NULL);g_assert_cmpstr(copy->master_file,==,moved_master);insight_project_files_free(copy);g_free(moved_master);g_free(master);
 gchar *before=g_key_file_to_data(config,NULL,NULL); g_free(s->masks); s->masks=g_strnfill(4097,'x'); g_assert_false(insight_project_files_write(s,config,root,&error)); g_assert_nonnull(error); g_clear_error(&error); gchar *after=g_key_file_to_data(config,NULL,NULL); g_assert_cmpstr(before,==,after); g_free(before); g_free(after);
 g_key_file_set_integer(config,"insight-project-files","version",2); g_assert_null(insight_project_files_read(config,root,&error)); g_assert_nonnull(error); g_clear_error(&error);
 g_key_file_set_integer(config,"insight-project-files","version",1); const gchar *invalid_paths[]={"bad\nname.c"}; g_key_file_set_string_list(config,"insight-project-files","files",invalid_paths,1); g_assert_null(insight_project_files_read(config,root,&error)); g_assert_nonnull(error); g_clear_error(&error);
 insight_project_files_free(s); g_key_file_unref(config); g_free(cwd); g_free(root); g_free(other); g_free(a); g_free(b); g_free(tree); g_free(child); g_free(keep); g_free(external); g_free(moved); g_free(moved_a);
 g_print("Project membership checks passed\n"); return 0;
}
