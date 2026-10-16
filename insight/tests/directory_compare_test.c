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
 o.max_entries=1;rows=insight_directory_compare(left,right,&o,NULL,&error);g_assert_null(rows);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE);g_clear_error(&error);
 GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);o=insight_directory_options();o.method=INSIGHT_DIR_DATE;
 rows=insight_directory_compare(left,right,&o,cancel,&error);g_assert_null(rows);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&error);g_object_unref(cancel);
 gchar *p=g_build_filename(sub,"child.txt",NULL);g_remove(p);g_free(p);g_rmdir(sub);g_free(sub);
 const gchar *names[]={"a.txt",".hidden",NULL};for(guint i=0;names[i];i++) { p=g_build_filename(left,names[i],NULL);g_remove(p);g_free(p); }
 p=g_build_filename(right,"b.txt",NULL);g_remove(p);g_free(p);g_rmdir(left);g_rmdir(right);g_rmdir(root);g_free(left);g_free(right);g_free(root);g_free(child);
 g_print("Directory metadata: union, recursion, hidden, limits and cancellation passed.\n");return 0;
}
