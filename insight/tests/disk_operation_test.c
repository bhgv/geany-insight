/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "disk_operation.h"
#include <glib/gstdio.h>
#include <string.h>
static GFileInfo *snapshot(const gchar *path) {
 GError *error=NULL;GFile *file=g_file_new_for_path(path);
 GFileInfo *info=g_file_query_info(file,INSIGHT_DISK_IDENTITY_ATTRIBUTES,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,&error);
 g_assert_no_error(error);g_assert_nonnull(info);g_object_unref(file);return info;
}
static gboolean change(const gchar *from,const gchar *to,GFileInfo *info,gboolean trash,GCancellable *cancel,GError **error) {
 return insight_disk_operation(from,to,g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE),g_file_info_get_etag(info),trash,cancel,error);
}
int main(void) {
 GError *error=NULL;gchar *root=g_dir_make_tmp("insight-disk-XXXXXX",&error);g_assert_no_error(error);
 gchar *from=g_build_filename(root,"источник.txt",NULL),*to=g_build_filename(root,"переименован.txt",NULL),*folder=g_build_filename(root,"folder",NULL),*child=g_build_filename(folder,"child",NULL);
 g_assert_true(g_file_set_contents(from,"original",-1,&error));GFileInfo *info=snapshot(from);
 g_assert_true(g_file_set_contents(to,"existing",-1,&error));g_assert_false(change(from,to,info,FALSE,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_EXISTS);g_clear_error(&error);
 gchar *text=NULL;g_assert_true(g_file_get_contents(to,&text,NULL,&error));g_assert_cmpstr(text,==,"existing");g_free(text);g_assert_true(g_file_test(from,G_FILE_TEST_EXISTS));g_unlink(to);
 g_assert_false(change(from,from,info,FALSE,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
 GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);g_assert_false(change(from,to,info,FALSE,cancel,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&error);g_assert_true(g_file_test(from,G_FILE_TEST_EXISTS));g_object_unref(cancel);
 g_assert_false(insight_disk_operation(from,to,"stale-id",NULL,FALSE,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG);g_clear_error(&error);
 g_assert_false(insight_disk_operation(from,to,g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE),"stale-etag",FALSE,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG);g_clear_error(&error);
 g_assert_false(insight_disk_operation(from,to,NULL,NULL,FALSE,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
 g_assert_false(insight_disk_operation(from,"relative",NULL,NULL,FALSE,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
 g_assert_true(change(from,to,info,FALSE,NULL,&error));g_assert_no_error(error);g_assert_false(g_file_test(from,G_FILE_TEST_EXISTS));g_assert_true(g_file_get_contents(to,&text,NULL,&error));g_assert_cmpstr(text,==,"original");g_free(text);g_object_unref(info);
 g_assert_cmpint(g_mkdir(folder,0700),==,0);g_assert_true(g_file_set_contents(child,"nested",-1,&error));info=snapshot(folder);
 gchar *inside=g_build_filename(folder,"inside",NULL);g_assert_false(change(folder,inside,info,FALSE,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);g_free(inside);
 gchar *moved=g_build_filename(root,"moved folder",NULL);g_assert_true(change(folder,moved,info,FALSE,NULL,&error));g_assert_no_error(error);g_object_unref(info);
 gchar *moved_child=g_build_filename(moved,"child",NULL);g_assert_true(g_file_get_contents(moved_child,&text,NULL,&error));g_assert_cmpstr(text,==,"nested");g_free(text);
#ifndef G_OS_WIN32
 gchar *link=g_build_filename(root,"link",NULL),*renamed_link=g_build_filename(root,"renamed-link",NULL);GFile *file=g_file_new_for_path(link);g_assert_true(g_file_make_symbolic_link(file,to,NULL,&error));g_object_unref(file);info=snapshot(link);
 g_assert_true(change(link,renamed_link,info,FALSE,NULL,&error));g_assert_no_error(error);g_assert_true(g_file_test(renamed_link,G_FILE_TEST_IS_SYMLINK));g_assert_true(g_file_test(to,G_FILE_TEST_EXISTS));g_object_unref(info);g_unlink(renamed_link);g_free(link);g_free(renamed_link);
#endif
#ifndef G_OS_WIN32
 /* A backend either trashes our own fixture or leaves it intact on failure. */
 gchar *trash_path=g_build_filename(root,"trash-fixture.txt",NULL);
 g_assert_true(g_file_set_contents(trash_path,"trash fixture",-1,&error));info=snapshot(trash_path);
 gboolean trashed=change(trash_path,NULL,info,TRUE,NULL,&error);
 if(trashed) { g_assert_no_error(error);g_assert_false(g_file_test(trash_path,G_FILE_TEST_EXISTS)); }
 else { g_assert_nonnull(error);g_clear_error(&error);g_assert_true(g_file_test(trash_path,G_FILE_TEST_EXISTS));g_unlink(trash_path); }
 g_print("Trash backend fixture: %s\n",trashed ? "moved to system trash" : "refused without deleting source");g_object_unref(info);g_free(trash_path);
#endif
 /* Missing source with trash requested must fail, never delete anything else. */
 g_assert_false(insight_disk_operation(from,NULL,"missing",NULL,TRUE,NULL,&error));g_assert_nonnull(error);g_clear_error(&error);g_assert_true(g_file_test(to,G_FILE_TEST_EXISTS));
 g_unlink(to);g_unlink(moved_child);g_rmdir(moved);g_rmdir(root);g_free(root);g_free(from);g_free(to);g_free(folder);g_free(child);g_free(moved);g_free(moved_child);
 g_print("Disk operations: Unicode rename/move, no overwrite, snapshots, cancellation and symlinks passed\n");return 0;
}
