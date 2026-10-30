/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "project_report.h"
#include <glib/gstdio.h>
#include <string.h>
static void expect(GError **error,GIOErrorEnum code) { g_assert_error(*error,G_IO_ERROR,code);g_clear_error(error); }
static void file_is(const gchar *path,const gchar *expected) { gchar *text=NULL;g_assert_true(g_file_get_contents(path,&text,NULL,NULL));g_assert_cmpstr(text,==,expected);g_free(text); }
int main(void)
{
    GError *error=NULL;gchar *root=g_dir_make_tmp("project-report-XXXXXX",&error);g_assert_no_error(error);
    gchar *project=g_build_filename(root,"demo.geany",NULL),*a=g_build_filename(root,"α.c",NULL),*empty=g_build_filename(root,"empty.h",NULL),*foreign=g_build_filename(root,"outside.c",NULL),*output=g_build_filename(root,"demo.RPT",NULL);
    g_assert_true(g_file_set_contents(project,"project settings",-1,NULL));g_assert_true(g_file_set_contents(a,"source",-1,NULL));
    InsightIndex *index=insight_index_new();InsightSymbol symbols[]={ {.name="zebra",.path=a,.kind="function",.scope="Box",.line=2,.signature="(int x)"}, {.name="alpha",.path=a,.kind="function",.scope="Box",.line=8}, {.name="β",.path=a,.kind="variable",.scope="",.line=1} };
    g_assert_true(insight_index_update(index,a,symbols,3));InsightSymbol outside={.name="foreign",.path=foreign,.kind="function",.line=1};g_assert_true(insight_index_update(index,foreign,&outside,1));
    GPtrArray *paths=g_ptr_array_new();g_ptr_array_add(paths,empty);g_ptr_array_add(paths,a);g_ptr_array_add(paths,a);
    InsightReportSnapshot *temporary=insight_report_snapshot_new("Open files",root,NULL,paths,index,0,FALSE,&error);
    g_assert_no_error(error);g_assert_nonnull(temporary);g_assert_null(temporary->project_file);g_assert_cmpuint(temporary->paths->len,==,2);
    g_assert_false(insight_project_report_save(temporary,output,"temporary",9,NULL,&error));expect(&error,G_IO_ERROR_NOT_SUPPORTED);g_assert_false(g_file_test(output,G_FILE_TEST_EXISTS));insight_report_snapshot_unref(temporary);
    InsightReportSnapshot *snapshot=insight_report_snapshot_new("Demo\nProject",root,project,paths,index,1,TRUE,&error);g_assert_no_error(error);g_assert_cmpuint(snapshot->paths->len,==,2);
    InsightReportOptions options={.include_symbols=TRUE,.line_numbers=TRUE,.relative_paths=TRUE};gsize length=0;gchar *text=insight_project_report_build(snapshot,&options,NULL,&length,&error);g_assert_no_error(error);g_assert_cmpuint(length,==,strlen(text));g_assert_nonnull(strstr(text,"Project Report: Demo\\nProject"));g_assert_nonnull(strstr(text,"Files: 2"));g_assert_nonnull(strstr(text,"Symbols: 3"));g_assert_nonnull(strstr(text,"File: empty.h"));g_assert_null(strstr(text,"foreign"));g_assert_true(strstr(text,"1\tvariable\tβ")<strstr(text,"2\tfunction\tzebra"));g_assert_true(strstr(text,"zebra")<strstr(text,"alpha"));g_assert_nonnull(strstr(text,"scope=Box\t(int x)"));g_free(text);
    options.sort_names=TRUE;options.line_numbers=FALSE;text=insight_project_report_build(snapshot,&options,NULL,NULL,&error);g_assert_true(strstr(text,"alpha")<strstr(text,"zebra"));g_assert_null(strstr(text,"  2\t"));g_free(text);
    options.kinds=g_hash_table_new(g_str_hash,g_str_equal);g_hash_table_add(options.kinds,"variable");text=insight_project_report_build(snapshot,&options,NULL,NULL,&error);g_assert_nonnull(strstr(text,"Symbols: 1"));g_assert_null(strstr(text,"zebra"));g_free(text);g_hash_table_remove_all(options.kinds);text=insight_project_report_build(snapshot,&options,NULL,NULL,&error);g_assert_nonnull(strstr(text,"Symbols: 0"));g_free(text);g_hash_table_unref(options.kinds);options.kinds=NULL;
    options.include_symbols=FALSE;text=insight_project_report_build(snapshot,&options,NULL,NULL,&error);g_assert_nonnull(strstr(text,"File: empty.h"));g_assert_null(strstr(text,"zebra"));g_free(text);
    options.file_list=TRUE;text=insight_project_report_build(snapshot,&options,NULL,NULL,&error);g_assert_cmpstr(text,==,"empty.h\nα.c\n");
    g_assert_true(insight_project_report_save(snapshot,output,text,strlen(text),NULL,&error));g_assert_no_error(error);file_is(output,text);
    g_assert_false(insight_project_report_save(snapshot,a,"bad",3,NULL,&error));expect(&error,G_IO_ERROR_PERMISSION_DENIED);file_is(a,"source");
    g_assert_false(insight_project_report_save(snapshot,project,"bad",3,NULL,&error));expect(&error,G_IO_ERROR_PERMISSION_DENIED);file_is(project,"project settings");
    GPtrArray *open=g_ptr_array_new();g_ptr_array_add(open,output);g_assert_false(insight_project_report_save(snapshot,output,"bad",3,open,&error));expect(&error,G_IO_ERROR_PERMISSION_DENIED);file_is(output,text);g_ptr_array_unref(open);
    g_assert_true(insight_project_report_save(snapshot,output,"new report",10,NULL,&error));file_is(output,"new report");
    g_assert_false(insight_project_report_save(snapshot,output,"\xff",1,NULL,&error));expect(&error,G_IO_ERROR_INVALID_DATA);file_is(output,"new report");
    g_assert_false(insight_project_report_save(snapshot,output,"x",INSIGHT_REPORT_LIMIT+1,NULL,&error));expect(&error,G_IO_ERROR_INVALID_ARGUMENT);
#ifndef G_OS_WIN32
    gchar *link=g_build_filename(root,"linked",NULL),*inside=g_build_filename(link,"out.RPT",NULL);GFile *file=g_file_new_for_path(link);g_assert_true(g_file_make_symbolic_link(file,root,NULL,&error));g_object_unref(file);g_assert_false(insight_project_report_save(snapshot,inside,"x",1,NULL,&error));expect(&error,G_IO_ERROR_NOT_DIRECTORY);g_remove(link);g_free(link);g_free(inside);
#endif
    GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);g_assert_null(insight_project_report_build(snapshot,&options,cancel,NULL,&error));expect(&error,G_IO_ERROR_CANCELLED);g_object_unref(cancel);g_free(text);
    insight_report_snapshot_unref(snapshot);
    /* Snapshots own their strings and retain the immutable index. */
    g_ptr_array_set_size(paths,0);gchar *strange=g_build_filename(root,"line\nfile.c",NULL);g_ptr_array_add(paths,strange);snapshot=insight_report_snapshot_new("Demo",root,project,paths,index,0,FALSE,&error);g_assert_no_error(error);text=insight_project_report_build(snapshot,&options,NULL,NULL,&error);g_assert_null(text);expect(&error,G_IO_ERROR_INVALID_DATA);options.file_list=FALSE;text=insight_project_report_build(snapshot,&options,NULL,NULL,&error);g_assert_nonnull(strstr(text,"line\\nfile.c"));g_free(text);insight_report_snapshot_unref(snapshot);g_free(strange);
    g_ptr_array_set_size(paths,0);gchar *semicolon=g_build_filename(root,";literal.c",NULL);g_ptr_array_add(paths,semicolon);snapshot=insight_report_snapshot_new("Demo",root,project,paths,index,0,FALSE,&error);options.file_list=TRUE;text=insight_project_report_build(snapshot,&options,NULL,NULL,&error);g_assert_cmpstr(text,==,"./;literal.c\n");g_free(text);insight_report_snapshot_unref(snapshot);g_free(semicolon);
    g_ptr_array_set_size(paths,0);g_ptr_array_add(paths,a);InsightSymbol bad={.name="\xff",.path=a,.kind="function",.line=1};g_assert_true(insight_index_update(index,a,&bad,1));snapshot=insight_report_snapshot_new("Demo",root,project,paths,index,0,FALSE,&error);options.file_list=FALSE;options.include_symbols=TRUE;g_assert_null(insight_project_report_build(snapshot,&options,NULL,NULL,&error));expect(&error,G_IO_ERROR_INVALID_DATA);insight_report_snapshot_unref(snapshot);
    gchar *signature=g_malloc0(65537);memset(signature,'s',65536);InsightSymbol many[70];for(guint i=0;i<70;i++) { many[i]=(InsightSymbol){.name="large",.path=a,.kind="function",.line=i+1,.signature=signature}; }g_assert_true(insight_index_update(index,a,many,70));g_free(signature);snapshot=insight_report_snapshot_new("Demo",root,project,paths,index,0,FALSE,&error);g_assert_null(insight_project_report_build(snapshot,&options,NULL,&length,&error));expect(&error,G_IO_ERROR_NO_SPACE);g_assert_cmpuint(length,==,0);insight_report_snapshot_unref(snapshot);
    /* Export must fit the existing MFL reader, not only the report limit. */
    g_ptr_array_set_size(paths,0);g_ptr_array_set_free_func(paths,g_free);gchar *tail=g_strnfill(8000,'x');
    for(guint i=0;i<300;i++) g_ptr_array_add(paths,g_strdup_printf("%s/file%u-%s.c",root,i,tail));g_free(tail);
    snapshot=insight_report_snapshot_new("Demo",root,project,paths,index,0,FALSE,&error);g_assert_no_error(error);options.file_list=TRUE;
    g_assert_null(insight_project_report_build(snapshot,&options,NULL,&length,&error));expect(&error,G_IO_ERROR_NO_SPACE);g_assert_cmpuint(length,==,0);insight_report_snapshot_unref(snapshot);
    insight_index_free(index);g_ptr_array_unref(paths);g_remove(output);g_remove(a);g_remove(project);g_rmdir(root);g_free(root);g_free(project);g_free(a);g_free(empty);g_free(foreign);g_free(output);
    g_print("Project report: deterministic paths/symbols, zero-tag files, scoped index, sort/types/line options, escaping, file list, bounded UTF-8, cancel and guarded create/replace/source/project/open/linked-parent writes passed\n");return 0;
}
