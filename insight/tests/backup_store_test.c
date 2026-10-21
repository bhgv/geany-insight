/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "backup_store.h"
#include <glib/gstdio.h>
#include <string.h>
static void expect(GError **error,GIOErrorEnum code) { g_assert_error(*error,G_IO_ERROR,code);g_clear_error(error); }
static void bytes(const gchar *path,const gchar *expected) { gchar *text=NULL;gsize length;g_assert_true(g_file_get_contents(path,&text,&length,NULL));g_assert_cmpmem(text,length,expected,strlen(expected));g_free(text); }
int main(void)
{
    GError *error=NULL;gchar *config=g_dir_make_tmp("backup-store-XXXXXX",&error);g_assert_no_error(error);
    gchar *source=g_build_filename(config,"проект","main.c",NULL),*directory=insight_backup_directory(config,source,&error);g_assert_no_error(error);
    gchar *alias=g_build_filename(config,"проект","sub","..","main.c",NULL),*same=insight_backup_directory(config,alias,&error);g_assert_cmpstr(directory,==,same);g_free(alias);g_free(same);
    gchar *other=insight_backup_directory(config,"/elsewhere/main.c",&error);g_assert_cmpstr(directory,!=,other);g_free(other);
    g_assert_null(insight_backup_directory("relative",source,&error));expect(&error,G_IO_ERROR_INVALID_ARGUMENT);
    GPtrArray *list=insight_backup_list(directory,&error);g_assert_no_error(error);g_assert_cmpuint(list->len,==,0);g_ptr_array_free(list,TRUE);g_assert_false(g_file_test(directory,G_FILE_TEST_EXISTS));
    const gchar *content="\xef\xbb\xbf" "α\r\nsecond\rthird\n";gchar *saved=NULL,*empty=NULL;
    g_assert_true(insight_backup_save(directory,content,strlen(content),&saved,&error));g_assert_no_error(error);bytes(saved,content);
    g_assert_true(insight_backup_save(directory,"",0,&empty,&error));g_assert_no_error(error);g_assert_cmpstr(saved,!=,empty);
    list=insight_backup_list(directory,&error);g_assert_cmpuint(list->len,==,2);g_assert_no_error(error);
    for(guint i=0;i<list->len;i++) { InsightBackupEntry *entry=g_ptr_array_index(list,i);gsize size=99;gchar *text=insight_backup_read(directory,entry,&size,&error);g_assert_no_error(error);g_assert_nonnull(text);g_assert_cmpstr(text,==,entry->size ? content:"");g_assert_cmpuint(size,==,entry->size);g_free(text); }
    g_assert_cmpint(strcmp(((InsightBackupEntry *)g_ptr_array_index(list,0))->name,((InsightBackupEntry *)g_ptr_array_index(list,1))->name),>,0);
    InsightBackupEntry *entry=g_ptr_array_index(list,0);gchar *changed=g_build_filename(directory,entry->name,NULL);
    g_assert_true(g_file_set_contents(changed,"replacement",-1,&error));g_assert_null(insight_backup_read(directory,entry,NULL,&error));expect(&error,G_IO_ERROR_WRONG_ETAG);g_ptr_array_free(list,TRUE);g_remove(changed);g_free(changed);
    gchar invalid[]={'a',0,'b'};g_assert_false(insight_backup_save(directory,invalid,sizeof invalid,NULL,&error));expect(&error,G_IO_ERROR_INVALID_DATA);
    g_assert_false(insight_backup_save(directory,"\xff",1,NULL,&error));expect(&error,G_IO_ERROR_INVALID_DATA);
    g_assert_false(insight_backup_save(directory,"x",INSIGHT_BACKUP_TEXT_LIMIT+1u,NULL,&error));expect(&error,G_IO_ERROR_NO_SPACE);
    InsightBackupEntry traversal={.name="../outside",.etag="x"};g_assert_null(insight_backup_read(directory,&traversal,NULL,&error));expect(&error,G_IO_ERROR_INVALID_ARGUMENT);
    gchar *ignored=g_build_filename(directory,".pending-interrupted",NULL);g_assert_true(g_file_set_contents(ignored,"partial",-1,&error));list=insight_backup_list(directory,&error);g_assert_cmpuint(list->len,==,1);g_ptr_array_free(list,TRUE);g_remove(ignored);g_free(ignored);
#ifndef G_OS_WIN32
    gchar *moved=g_strconcat(directory,"-real",NULL);g_assert_cmpint(g_rename(directory,moved),==,0);GFile *link=g_file_new_for_path(directory);g_assert_true(g_file_make_symbolic_link(link,moved,NULL,&error));g_object_unref(link);
    g_assert_null(insight_backup_list(directory,&error));expect(&error,G_IO_ERROR_NOT_DIRECTORY);g_assert_false(insight_backup_save(directory,"x",1,NULL,&error));expect(&error,G_IO_ERROR_NOT_DIRECTORY);g_remove(directory);g_assert_cmpint(g_rename(moved,directory),==,0);g_free(moved);
    list=insight_backup_list(directory,&error);entry=g_ptr_array_index(list,0);changed=g_build_filename(directory,entry->name,NULL);g_remove(changed);link=g_file_new_for_path(changed);g_assert_true(g_file_make_symbolic_link(link,source,NULL,&error));g_object_unref(link);
    g_assert_null(insight_backup_read(directory,entry,NULL,&error));expect(&error,G_IO_ERROR_WRONG_ETAG);g_assert_null(insight_backup_list(directory,&error));expect(&error,G_IO_ERROR_INVALID_DATA);g_ptr_array_free(list,TRUE);g_remove(changed);g_free(changed);
#endif
    /* No eviction: reaching the count ceiling leaves every existing version intact. */
    list=insight_backup_list(directory,&error);guint count=list->len;g_ptr_array_free(list,TRUE);
    for(guint i=count;i<INSIGHT_BACKUP_COUNT_LIMIT;i++) g_assert_true(insight_backup_save(directory,"",0,NULL,&error));
    g_assert_false(insight_backup_save(directory,"",0,NULL,&error));expect(&error,G_IO_ERROR_NO_SPACE);list=insight_backup_list(directory,&error);g_assert_cmpuint(list->len,==,INSIGHT_BACKUP_COUNT_LIMIT);
    for(guint i=0;i<list->len;i++) { gchar *p=g_build_filename(directory,((InsightBackupEntry *)g_ptr_array_index(list,i))->name,NULL);g_remove(p);g_free(p); }g_ptr_array_free(list,TRUE);
    gchar *large=g_malloc(INSIGHT_BACKUP_TEXT_LIMIT);memset(large,'x',INSIGHT_BACKUP_TEXT_LIMIT);
    for(guint i=0;i<8;i++) g_assert_true(insight_backup_save(directory,large,INSIGHT_BACKUP_TEXT_LIMIT,NULL,&error));g_free(large);
    g_assert_false(insight_backup_save(directory,"x",1,NULL,&error));expect(&error,G_IO_ERROR_NO_SPACE);
    list=insight_backup_list(directory,&error);g_assert_cmpuint(list->len,==,8);for(guint i=0;i<list->len;i++) { gchar *p=g_build_filename(directory,((InsightBackupEntry *)g_ptr_array_index(list,i))->name,NULL);g_remove(p);g_free(p); }g_ptr_array_free(list,TRUE);
    g_rmdir(directory);gchar *root=g_path_get_dirname(directory);g_rmdir(root);g_rmdir(config);g_free(root);g_free(config);g_free(directory);g_free(source);g_free(saved);g_free(empty);
    g_print("Backup store: exact UTF-8/BOM/EOL/empty snapshots, lexical identities, unique newest-first versions, stale and symlink rejection, pending exclusion, count and byte ceilings passed\n");return 0;
}
