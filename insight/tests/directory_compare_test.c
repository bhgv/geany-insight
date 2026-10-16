/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/directory_compare.h"
#include <glib/gstdio.h>
static void put(const gchar *root,const gchar *name,const gchar *text) {
 gchar *path=g_build_filename(root,name,NULL);GError *error=NULL;g_assert_true(g_file_set_contents(path,text,-1,&error));g_assert_no_error(error);g_free(path);
}
static InsightDirectoryEntry *find(GPtrArray *rows,const gchar *name) {
 for(guint i=0;i<rows->len;i++) { InsightDirectoryEntry *e=g_ptr_array_index(rows,i);if(!strcmp(e->relative,name)) return e; }return NULL;
}
int main(void) {
 GError *error=NULL;gchar *root=g_dir_make_tmp("insight-directories-XXXXXX",&error);g_assert_no_error(error);
 gchar *left=g_build_filename(root,"left",NULL),*right=g_build_filename(root,"right",NULL);g_assert_cmpint(g_mkdir(left,0700),==,0);g_assert_cmpint(g_mkdir(right,0700),==,0);
 put(left,"a.txt","left");put(right,"b.txt","right");put(left,".hidden","hidden");
 gchar *sub=g_build_filename(left,"sub",NULL);g_assert_cmpint(g_mkdir(sub,0700),==,0);put(sub,"child.txt","child");
 InsightDirectoryOptions o=insight_directory_options();o.method=INSIGHT_DIR_SIZE_DATE;
 GPtrArray *rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_nonnull(rows);
 g_assert_cmpuint(rows->len,==,4);g_assert_cmpint(find(rows,"a.txt")->status,==,INSIGHT_DIR_LEFT);g_assert_cmpint(find(rows,"b.txt")->status,==,INSIGHT_DIR_RIGHT);
 gchar *child=g_build_filename("sub","child.txt",NULL);g_assert_nonnull(find(rows,child));g_assert_null(find(rows,".hidden"));g_ptr_array_unref(rows);
 o.recursive=FALSE;o.hidden=TRUE;rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_nonnull(find(rows,".hidden"));g_assert_null(find(rows,child));g_ptr_array_unref(rows);
 put(left,"a.txt","same text");put(right,"a.txt","same text");
 o=insight_directory_options();rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_cmpint(find(rows,"a.txt")->status,==,INSIGHT_DIR_SAME);g_ptr_array_unref(rows);
 put(right,"a.txt","same size");rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_cmpint(find(rows,"a.txt")->status,==,INSIGHT_DIR_DIFFERENT);g_ptr_array_unref(rows);
 put(left,"a.txt","я b\r\nend\r");put(right,"a.txt","яb\nend\n");o.text_flags=3;
 rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_cmpint(find(rows,"a.txt")->status,==,INSIGHT_DIR_SAME);g_ptr_array_unref(rows);
 put(right,"a.txt","яb\nend");rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_cmpint(find(rows,"a.txt")->status,==,INSIGHT_DIR_DIFFERENT);g_ptr_array_unref(rows);
 gchar *binary=g_build_filename(right,"a.txt",NULL);g_assert_true(g_file_set_contents(binary,"a\0b",3,&error));g_assert_no_error(error);
 rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_cmpint(find(rows,"a.txt")->status,==,INSIGHT_DIR_SKIPPED);g_ptr_array_unref(rows);g_free(binary);
 o.text_flags=0;o.max_bytes=0;put(right,"a.txt","same size!!");put(left,"a.txt","same size!!");
 rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_cmpint(find(rows,"a.txt")->status,==,INSIGHT_DIR_SKIPPED);g_ptr_array_unref(rows);
 o=insight_directory_options();o.max_depth=0;
 rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_null(rows);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE);g_clear_error(&error);
 gchar *linked=g_build_filename(left,"loop",NULL);GFile *link=g_file_new_for_path(linked);
 if(g_file_make_symbolic_link(link,".",NULL,&error)) {
  o=insight_directory_options();rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_cmpint(find(rows,"loop")->status,==,INSIGHT_DIR_SKIPPED);g_ptr_array_unref(rows);
  rows=insight_directory_compare(linked,right,&o,NULL,&error);g_assert_null(rows);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY);g_clear_error(&error);g_assert_true(g_file_delete(link,NULL,&error));g_assert_no_error(error);
 } else g_clear_error(&error);
 g_object_unref(link);g_free(linked);
 gchar *a=g_build_filename(left,"a.txt",NULL),*b=g_build_filename(right,"a.txt",NULL);
 g_assert_true(g_file_set_contents(a,"a\0b",3,&error));g_assert_no_error(error);g_assert_true(g_file_set_contents(b,"a\0b",3,&error));g_assert_no_error(error);
 o=insight_directory_options();rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_cmpint(find(rows,"a.txt")->status,==,INSIGHT_DIR_SAME);g_ptr_array_unref(rows);
 put(right,"a.txt","different size and bytes");
 for(guint i=0;i<2;i++) {
  GFile *file=g_file_new_for_path(i ? b : a);GFileInfo *info=g_file_info_new();g_file_info_set_attribute_uint64(info,G_FILE_ATTRIBUTE_TIME_MODIFIED,1600000000);g_file_info_set_attribute_uint32(info,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,0);
  g_assert_true(g_file_set_attributes_from_info(file,info,G_FILE_QUERY_INFO_NONE,NULL,&error));g_assert_no_error(error);g_object_unref(info);g_object_unref(file);
 }
 o.method=INSIGHT_DIR_DATE;rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_cmpint(find(rows,"a.txt")->status,==,INSIGHT_DIR_SAME);g_ptr_array_unref(rows);
 o.method=INSIGHT_DIR_SIZE_DATE;rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_no_error(error);g_assert_cmpint(find(rows,"a.txt")->status,==,INSIGHT_DIR_DIFFERENT);g_ptr_array_unref(rows);g_free(a);g_free(b);
 o.max_entries=1;rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_null(rows);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE);g_clear_error(&error);
 GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);o=insight_directory_options();o.method=INSIGHT_DIR_DATE;
 rows=insight_directory_compare(left,right,&o,cancel,&error);g_assert_null(rows);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&error);g_object_unref(cancel);
 gchar *p=g_build_filename(sub,"child.txt",NULL);g_remove(p);g_free(p);g_rmdir(sub);g_free(sub);
 const gchar *names[]={"a.txt",".hidden",NULL};for(guint i=0;names[i];i++) { p=g_build_filename(left,names[i],NULL);g_remove(p);g_free(p); }
 p=g_build_filename(right,"a.txt",NULL);g_remove(p);g_free(p);p=g_build_filename(right,"b.txt",NULL);g_remove(p);g_free(p);g_rmdir(left);g_rmdir(right);g_rmdir(root);g_free(left);g_free(right);g_free(root);g_free(child);
 g_print("Directory snapshots: contents, text options, metadata modes, links, recursion, limits and cancellation passed.\n");return 0;
}
