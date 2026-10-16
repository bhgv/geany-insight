/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "project_files.h"
#include <string.h>
static void master_variables(const gchar *root,const gchar *moved) {
 GError *error=NULL;InsightProjectFiles *s=insight_project_files_new();gchar *project=g_build_filename(root,"Проект.geany",NULL),*moved_project=g_build_filename(moved,"Перенос.geany",NULL),*expected=g_build_filename(root,"master.txt",NULL);
 g_assert_true(insight_project_files_set_master(s,"%project_source_dir%/master.txt",root,project,&error));g_assert_no_error(error);g_assert_cmpstr(s->master_file,==,expected);g_assert_cmpstr(s->master_expression,==,"%project_source_dir%/master.txt");
 g_assert_true(insight_project_files_set_master(s,s->master_expression,root,project,&error));g_assert_cmpstr(s->master_expression,==,"%project_source_dir%/master.txt");
 GKeyFile *key=g_key_file_new();g_assert_true(insight_project_files_write(s,key,root,&error));gchar *raw=g_key_file_get_string(key,"insight-project-files","master-file-expression",NULL);g_assert_cmpstr(raw,==,s->master_expression);g_free(raw);InsightProjectFiles *copy=insight_project_files_read_project(key,moved,moved_project,&error);g_assert_no_error(error);gchar *relocated=g_build_filename(moved,"master.txt",NULL);g_assert_cmpstr(copy->master_file,==,relocated);g_free(relocated);insight_project_files_free(copy);
 g_assert_true(insight_project_files_set_master(s,"%PROJECT_DATA_DIR%/%PROJECT_NAME%.txt",root,project,&error));g_free(expected);expected=g_build_filename(root,"Проект.txt",NULL);g_assert_cmpstr(s->master_file,==,expected);g_assert_true(insight_project_files_write(s,key,root,&error));copy=insight_project_files_read_project(key,moved,moved_project,&error);g_assert_no_error(error);relocated=g_build_filename(moved,"Перенос.txt",NULL);g_assert_cmpstr(copy->master_file,==,relocated);g_free(relocated);insight_project_files_free(copy);
 g_assert_false(insight_project_files_set_master(s,"%UNKNOWN%/file",root,project,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA);g_clear_error(&error);g_assert_cmpstr(s->master_file,==,expected);g_assert_false(insight_project_files_set_master(s,"%PROJECT_DATA_DIR%/file",root,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);g_assert_false(insight_project_files_set_master(s,"%PROJECT_SOURCE_DIR/file",root,project,&error));g_assert_nonnull(error);g_clear_error(&error);
 GString *many=g_string_new(NULL);for(guint i=0;i<33;i++) g_string_append(many,"%PROJECT_NAME%/");g_assert_false(insight_project_files_set_master(s,many->str,root,project,&error));g_assert_nonnull(error);g_clear_error(&error);g_string_free(many,TRUE);g_assert_cmpstr(s->master_file,==,expected);
 gchar *huge=g_strnfill(16384,'a');huge[0]=G_DIR_SEPARATOR;g_assert_false(insight_project_files_set_master(s,"%PROJECT_SOURCE_DIR%/extra",huge,project,&error));g_assert_nonnull(error);g_clear_error(&error);g_free(huge);
 const gchar *literal="100% done%file.txt";gchar *escaped=insight_project_files_escape_master(literal);g_assert_true(insight_project_files_set_master(s,escaped,root,project,&error));g_free(expected);expected=g_build_filename(root,literal,NULL);g_assert_cmpstr(s->master_file,==,expected);g_assert_true(insight_project_files_write(s,key,root,&error));copy=insight_project_files_read_project(key,root,project,&error);g_assert_no_error(error);g_assert_cmpstr(copy->master_file,==,expected);insight_project_files_free(copy);g_free(escaped);
 /* Legacy literal percent paths never acquire variable interpretation. */
 g_key_file_remove_key(key,"insight-project-files","master-file-expression",NULL);copy=insight_project_files_read(key,root,&error);g_assert_no_error(error);g_assert_cmpstr(copy->master_file,==,expected);g_assert_cmpstr(copy->master_expression,==,"");insight_project_files_free(copy);
 g_assert_true(insight_project_files_set_master(s,"",root,project,&error));g_assert_true(insight_project_files_write(s,key,root,&error));g_assert_false(g_key_file_has_key(key,"insight-project-files","master-file-expression",NULL));g_assert_false(g_key_file_has_key(key,"insight-project-files","master-file",NULL));
 g_free(project);g_free(moved_project);g_free(expected);insight_project_files_free(s);g_key_file_unref(key);
}
static void environment_variables(const gchar *root,const gchar *moved) {
 const gchar *name="INSIGHT_TEST_MFL_ROOT_7B12";gchar *previous=g_strdup(g_getenv(name));
 GError *error=NULL;InsightProjectFiles *s=insight_project_files_new();GKeyFile *key=g_key_file_new();
 g_assert_true(g_setenv(name,root,TRUE));
 g_assert_true(insight_project_files_set_master(s,"%insight_test_mfl_root_7b12%/имя.txt",root,NULL,&error));g_assert_no_error(error);
 gchar *expected=g_build_filename(root,"имя.txt",NULL);g_assert_cmpstr(s->master_file,==,expected);g_free(expected);
 g_assert_true(insight_project_files_write(s,key,root,&error));g_assert_no_error(error);
 g_assert_true(g_setenv(name,moved,TRUE));
 InsightProjectFiles *copy=insight_project_files_read(key,root,&error);g_assert_no_error(error);g_assert_nonnull(copy);
 expected=g_build_filename(moved,"имя.txt",NULL);g_assert_cmpstr(copy->master_file,==,expected);g_free(expected);insight_project_files_free(copy);
 /* Expansion values are literal: inserted percent syntax is never evaluated. */
 gchar *literal=g_build_filename(root,"%PROJECT_NAME%",NULL);g_assert_true(g_setenv(name,literal,TRUE));
 g_assert_true(insight_project_files_set_master(s,"%INSIGHT_TEST_MFL_ROOT_7B12%/m.txt",root,NULL,&error));
 expected=g_build_filename(literal,"m.txt",NULL);g_assert_cmpstr(s->master_file,==,expected);g_free(literal);
 const gchar *invalid[]={"","bad\npath","bad\rpath"
#ifndef G_OS_WIN32
  ,"\xff"
#endif
 };
 for(guint i=0;i<G_N_ELEMENTS(invalid);i++) {
  g_assert_true(g_setenv(name,invalid[i],TRUE));
  g_assert_false(insight_project_files_set_master(s,"%INSIGHT_TEST_MFL_ROOT_7B12%/m.txt",root,NULL,&error));
  g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA);g_clear_error(&error);g_assert_cmpstr(s->master_file,==,expected);
 }
 gchar *huge=g_strnfill(16385,'x');g_assert_true(g_setenv(name,huge,TRUE));g_free(huge);
 g_assert_false(insight_project_files_set_master(s,"%INSIGHT_TEST_MFL_ROOT_7B12%",root,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);
 g_unsetenv(name);g_assert_false(insight_project_files_set_master(s,"%INSIGHT_TEST_MFL_ROOT_7B12%",root,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);g_assert_cmpstr(s->master_file,==,expected);
#ifndef G_OS_WIN32
 const gchar *lower="insight_test_mfl_root_7b12";gchar *old_lower=g_strdup(g_getenv(lower));
 g_assert_true(g_setenv(name,root,TRUE));g_assert_true(g_setenv(lower,moved,TRUE));
 g_assert_false(insight_project_files_set_master(s,"%INSIGHT_TEST_MFL_ROOT_7B12%",root,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);
 g_assert_true(g_setenv(lower,root,TRUE));g_assert_true(insight_project_files_set_master(s,"%INSIGHT_TEST_MFL_ROOT_7B12%",root,NULL,&error));g_assert_cmpstr(s->master_file,==,root);
 if(old_lower) g_setenv(lower,old_lower,TRUE);else g_unsetenv(lower);g_free(old_lower);
#endif
 if(previous) g_setenv(name,previous,TRUE);else g_unsetenv(name);g_free(previous);g_free(expected);
 g_assert_true(insight_project_files_set_master(s,"%temp_dir%/m.txt",root,NULL,&error));g_assert_no_error(error);
 expected=g_build_filename(g_get_tmp_dir(),"m.txt",NULL);g_assert_cmpstr(s->master_file,==,expected);g_free(expected);
 const struct { const gchar *name,*value; } folders[]={
  {"DESKTOP_DIR",g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP)},
  {"MYDOCUMENTS_DIR",g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS)},
#ifdef G_OS_WIN32
  {"APPDATA_DIR",g_getenv("APPDATA")},{"LOCAL_APPDATA_DIR",g_getenv("LOCALAPPDATA")},
  {"PROGRAMFILES_DIR",g_getenv("ProgramFiles")},{"PROGRAMFILESX86_DIR",g_getenv("ProgramFiles(x86)")},
  {"WINDOWS_DIR",g_getenv("SystemRoot")}
#else
  {"APPDATA_DIR",g_get_user_config_dir()},{"LOCAL_APPDATA_DIR",g_get_user_data_dir()}
#endif
 };
 for(guint i=0;i<G_N_ELEMENTS(folders);i++) {
  gchar *expression=g_strdup_printf("%%%s%%/m.txt",folders[i].name);
  if(folders[i].value && *folders[i].value) {
   g_assert_true(insight_project_files_set_master(s,expression,root,NULL,&error));g_assert_no_error(error);
   gchar *joined=g_build_filename(folders[i].value,"m.txt",NULL);expected=g_canonicalize_filename(joined,root);
   g_assert_cmpstr(s->master_file,==,expected);g_free(expected);g_free(joined);
  } else { g_assert_false(insight_project_files_set_master(s,expression,root,NULL,&error));g_assert_nonnull(error);g_clear_error(&error); }
  g_free(expression);
 }
 /* A process variable cannot override an unavailable reserved project name. */
 gchar *old_project=g_strdup(g_getenv("PROJECT_DATA_DIR"));g_assert_true(g_setenv("PROJECT_DATA_DIR",root,TRUE));
 g_assert_false(insight_project_files_set_master(s,"%PROJECT_DATA_DIR%/m.txt",root,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);
 if(old_project) g_setenv("PROJECT_DATA_DIR",old_project,TRUE);else g_unsetenv("PROJECT_DATA_DIR");g_free(old_project);
 insight_project_files_free(s);g_key_file_unref(key);
}
static void program_directory(const gchar *root,const gchar *moved) {
#if defined(G_OS_WIN32) || defined(__linux__)
 GError *error=NULL;InsightProjectFiles *s=insight_project_files_new();
#ifdef __linux__
 /* /proc can be absent in containers. The variable must fail closed there. */
 gchar *probe=g_file_read_link("/proc/self/exe",NULL);
 if(!probe) {
  g_assert_false(insight_project_files_set_master(s,"%PROGRAM_DIR%/master.txt",root,NULL,&error));
  g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA);g_clear_error(&error);
  g_assert_cmpstr(s->master_file,==,"");insight_project_files_free(s);return;
 }
 g_free(probe);
#endif
 gchar *old=g_strdup(g_getenv("PROGRAM_DIR"));
 g_assert_true(g_setenv("PROGRAM_DIR",moved,TRUE));
 g_assert_true(insight_project_files_set_master(s,"%program_dir%/master.txt",root,NULL,&error));
 g_assert_no_error(error);g_assert_true(g_path_is_absolute(s->master_file));
 gchar *resolved=g_strdup(s->master_file),*spoof=g_build_filename(moved,"master.txt",NULL);
 g_assert_cmpstr(resolved,!=,spoof);g_free(spoof);
#ifdef __linux__
 gchar *executable=g_file_read_link("/proc/self/exe",&error);g_assert_no_error(error);
 gchar *directory=g_path_get_dirname(executable),*expected=g_build_filename(directory,"master.txt",NULL);
 g_assert_cmpstr(resolved,==,expected);g_free(expected);g_free(directory);g_free(executable);
#endif
 /* Changing the project root must not relocate the executable directory. */
 g_assert_true(insight_project_files_set_master(s,s->master_expression,moved,NULL,&error));
 g_assert_no_error(error);g_assert_cmpstr(s->master_file,==,resolved);
 GKeyFile *key=g_key_file_new();g_assert_true(insight_project_files_write(s,key,root,&error));
 InsightProjectFiles *copy=insight_project_files_read(key,moved,&error);g_assert_no_error(error);
 g_assert_nonnull(copy);g_assert_cmpstr(copy->master_file,==,resolved);
 g_assert_cmpstr(copy->master_expression,==,"%program_dir%/master.txt");
 insight_project_files_free(copy);g_key_file_unref(key);insight_project_files_free(s);g_free(resolved);
 if(old) g_setenv("PROGRAM_DIR",old,TRUE);else g_unsetenv("PROGRAM_DIR");g_free(old);
#else
 (void)root;(void)moved;
#endif
}
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
 program_directory(root,moved);environment_variables(root,moved);master_variables(root,moved);insight_project_files_free(s); g_key_file_unref(config); g_free(cwd); g_free(root); g_free(other); g_free(a); g_free(b); g_free(tree); g_free(child); g_free(keep); g_free(external); g_free(moved); g_free(moved_a);
 g_print("Project membership checks passed\n"); return 0;
}
