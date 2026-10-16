/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/directory_operation.h"
#include <glib/gstdio.h>
#include <string.h>
#define ATTRS "standard::type,standard::is-symlink,standard::size,time::modified,time::modified-usec,id::file,etag::value"
static GFileInfo *snapshot(const gchar *root,const gchar *relative) {
 gchar *path=g_build_filename(root,relative,NULL);GFile *file=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(file,ATTRS,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,NULL);g_object_unref(file);g_free(path);return info;
}
static void put(const gchar *root,const gchar *name,const gchar *bytes,gsize size) {
 gchar *path=g_build_filename(root,name,NULL);g_assert_true(g_file_set_contents(path,bytes,size,NULL));g_free(path);
}
static void contents(const gchar *root,const gchar *name,const gchar *expected,gsize size) {
 gchar *path=g_build_filename(root,name,NULL),*bytes=NULL;gsize n=0;g_assert_true(g_file_get_contents(path,&bytes,&n,NULL));g_assert_cmpuint(n,==,size);g_assert_cmpint(memcmp(bytes,expected,size),==,0);g_free(bytes);g_free(path);
}
static void clear(GFileInfo **info) { g_clear_object(&info[0]);g_clear_object(&info[1]); }
int main(void) {
 GError *error=NULL;gchar *root=g_dir_make_tmp("insight-directory-operations-XXXXXX",&error);g_assert_no_error(error);
 gchar *left=g_build_filename(root,"left",NULL),*right=g_build_filename(root,"right",NULL);g_assert_cmpint(g_mkdir(left,0700),==,0);g_assert_cmpint(g_mkdir(right,0700),==,0);const gchar *roots[]={left,right};GFileInfo *info[2]={NULL,NULL};
 put(left,"a.bin","a\0b\r\n",5);info[0]=snapshot(left,"a.bin");
 g_assert_true(insight_directory_operation(roots,"a.bin",info,INSIGHT_DIR_COPY_LR,NULL,&error));g_assert_no_error(error);contents(right,"a.bin","a\0b\r\n",5);
 /* An absent target snapshot cannot overwrite a newly appeared destination. */
 g_assert_false(insight_directory_operation(roots,"a.bin",info,INSIGHT_DIR_COPY_LR,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);contents(right,"a.bin","a\0b\r\n",5);
 info[1]=snapshot(right,"a.bin");put(left,"a.bin","new",3);
 g_assert_false(insight_directory_operation(roots,"a.bin",info,INSIGHT_DIR_COPY_LR,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);contents(right,"a.bin","a\0b\r\n",5);clear(info);
 info[0]=snapshot(left,"a.bin");info[1]=snapshot(right,"a.bin");put(right,"a.bin","changed target",14);
 g_assert_false(insight_directory_operation(roots,"a.bin",info,INSIGHT_DIR_COPY_LR,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);contents(right,"a.bin","changed target",14);clear(info);
 info[0]=snapshot(left,"a.bin");info[1]=snapshot(right,"a.bin");
 g_assert_true(insight_directory_operation(roots,"a.bin",info,INSIGHT_DIR_COPY_LR,NULL,&error));g_assert_no_error(error);contents(right,"a.bin","new",3);clear(info);
 put(right,"a.bin","right",5);info[0]=snapshot(left,"a.bin");info[1]=snapshot(right,"a.bin");g_assert_true(insight_directory_operation(roots,"a.bin",info,INSIGHT_DIR_COPY_RL,NULL,&error));g_assert_no_error(error);contents(left,"a.bin","right",5);clear(info);
 info[0]=snapshot(left,"a.bin");info[1]=snapshot(right,"a.bin");GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);
 for(guint action=0;action<4;action++) { g_assert_false(insight_directory_operation(roots,"a.bin",info,action,cancel,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&error); }g_object_unref(cancel);
 g_assert_false(insight_directory_operation(roots,"../a.bin",info,0,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
 const gchar *same_roots[]={left,left};GFileInfo *same_info[]={info[0],info[0]};g_assert_false(insight_directory_operation(same_roots,"a.bin",same_info,0,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
 /* Trash refuses a stale identity and never falls back to deletion. */
 put(left,"a.bin","changed source",14);g_assert_false(insight_directory_operation(roots,"a.bin",info,INSIGHT_DIR_TRASH_L,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);contents(left,"a.bin","changed source",14);clear(info);
 gchar *path=g_build_filename(left,"huge",NULL);GFile *file=g_file_new_for_path(path);GFileOutputStream *stream=g_file_create(file,G_FILE_CREATE_PRIVATE,NULL,&error);g_assert_no_error(error);g_assert_nonnull(stream);g_assert_true(g_seekable_truncate(G_SEEKABLE(stream),64*1024*1024+1,NULL,&error));g_assert_no_error(error);g_assert_true(g_output_stream_close(G_OUTPUT_STREAM(stream),NULL,&error));g_assert_no_error(error);g_object_unref(stream);info[0]=snapshot(left,"huge");
 g_assert_false(insight_directory_operation(roots,"huge",info,0,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE);g_clear_error(&error);clear(info);g_assert_true(g_file_delete(file,NULL,&error));g_assert_no_error(error);g_object_unref(file);g_free(path);
 gchar *sub=g_build_filename(left,"sub",NULL);file=g_file_new_for_path(sub);
 if(g_file_make_symbolic_link(file,right,NULL,&error)) { info[0]=snapshot(left,"sub/a.bin");g_assert_nonnull(info[0]);g_assert_false(insight_directory_operation(roots,"sub/a.bin",info,0,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY);g_clear_error(&error);clear(info);g_assert_true(g_file_delete(file,NULL,&error));g_assert_no_error(error); }else g_clear_error(&error);g_object_unref(file);g_free(sub);
 for(guint side=0;side<2;side++) {
  put(roots[side],"trash-fixture.txt","trash",5);info[side]=snapshot(roots[side],"trash-fixture.txt");gboolean trashed=insight_directory_operation(roots,"trash-fixture.txt",info,INSIGHT_DIR_TRASH_L+side,NULL,&error);gchar *fixture=g_build_filename(roots[side],"trash-fixture.txt",NULL);
  if(trashed) { g_assert_no_error(error);g_assert_false(g_file_test(fixture,G_FILE_TEST_EXISTS)); }
  else { g_assert_nonnull(error);g_clear_error(&error);contents(roots[side],"trash-fixture.txt","trash",5);g_remove(fixture); }
  g_print("Directory trash side %u: %s\n",side,trashed ? "moved to system trash" : "refused without deleting source");g_free(fixture);clear(info);
 }
 gchar *a=g_build_filename(left,"a.bin",NULL),*b=g_build_filename(right,"a.bin",NULL);g_remove(a);g_remove(b);g_free(a);g_free(b);g_rmdir(left);g_rmdir(right);g_rmdir(root);g_free(left);g_free(right);g_free(root);
 g_print("Directory operations: exact binary copy in both directions, conditional replacement, stale source/target, same file, cancellation, limits, traversal and parent symlinks passed.\n");return 0;
}
