/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/git_project.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *root;
static void git(const gchar *const *args) { GError *error=NULL;GBytes *b=insight_git_run(root,NULL,args,NULL,&error);g_assert_no_error(error);g_assert_nonnull(b);g_bytes_unref(b); }
static gchar *output(const gchar *const *args) { GError *e=NULL;GBytes *b=insight_git_run(root,NULL,args,NULL,&e);g_assert_no_error(e);gsize n;const gchar *p=g_bytes_get_data(b,&n);gchar *s=g_strndup(p,n);g_bytes_unref(b);return s; }
static void write_file(const gchar *path,const gchar *text) { gchar *p=g_build_filename(root,path,NULL);g_assert_true(g_file_set_contents(p,text,-1,NULL));g_free(p); }
static GPtrArray *status(void) { GError *error=NULL;GPtrArray *rows=insight_git_status(root,NULL,&error);g_assert_no_error(error);g_assert_nonnull(rows);return rows; }
static InsightGitChange *find(GPtrArray *rows,const gchar *path) { for(guint i=0;i<rows->len;i++) { InsightGitChange *c=g_ptr_array_index(rows,i);if(!strcmp(c->path,path)) return c; }g_assert_not_reached(); }
static void commit(GPtrArray *rows,const gchar *message) { GError *e=NULL;gboolean ok=insight_git_commit(root,rows,message,&e);if(e) g_printerr("%s: %s\n",message,e->message);g_assert_no_error(e);g_assert_true(ok); }
static void clean_tree(const gchar *path) { GDir *dir=g_dir_open(path,0,NULL);if(dir) { const gchar *name;while((name=g_dir_read_name(dir))) { gchar *p=g_build_filename(path,name,NULL);if(g_file_test(p,G_FILE_TEST_IS_DIR) && !g_file_test(p,G_FILE_TEST_IS_SYMLINK)) clean_tree(p);else { g_chmod(p,0600);g_unlink(p); }g_free(p); }g_dir_close(dir); }g_rmdir(path); }
int main(void)
{
 root=g_dir_make_tmp("insight-git-test-XXXXXX",NULL);g_assert_nonnull(root);
 const gchar *init[]={"init","--initial-branch=main",NULL};git(init);
 const gchar *name[]={"config","user.name","Insight Test",NULL},*email[]={"config","user.email","test@example.invalid",NULL};git(name);git(email);
 write_file("a.txt","first\n");write_file("b.txt","first\n");
 GPtrArray *rows=status();g_assert_cmpuint(rows->len,==,2);gchar *message=insight_git_message(rows);g_assert_nonnull(strstr(message,"2 added"));g_free(message);
 GPtrArray *chosen=g_ptr_array_new();g_ptr_array_add(chosen,find(rows,"a.txt"));commit(chosen,"Initial selected file");g_ptr_array_unref(chosen);g_ptr_array_unref(rows);
 const gchar *tree[]={"ls-tree","--name-only","HEAD",NULL};gchar *text=output(tree);g_assert_cmpstr(text,==,"a.txt\n");g_free(text);
 /* An unchecked addition staged by another client must stay staged. */
 const gchar *stage[]={"add","--","b.txt",NULL};git(stage);write_file("a.txt","second\n");
 rows=status();chosen=g_ptr_array_new();g_ptr_array_add(chosen,find(rows,"a.txt"));commit(chosen,"Only a");g_ptr_array_unref(chosen);g_ptr_array_unref(rows);
 const gchar *cached[]={"diff","--cached","--name-only",NULL};text=output(cached);g_assert_cmpstr(text,==,"b.txt\n");g_free(text);
 text=output(tree);g_assert_cmpstr(text,==,"a.txt\n");g_free(text);
 rows=status();commit(rows,"Add b");g_ptr_array_unref(rows);
 /* A cached removal commits a deletion, even though the disk file survives. */
 GPtrArray *paths=g_ptr_array_new();g_ptr_array_add(paths,"a.txt");GError *error=NULL;g_assert_true(insight_git_paths(root,paths,FALSE,NULL,&error));g_assert_no_error(error);g_ptr_array_unref(paths);
 rows=status();g_assert_cmpint(find(rows,"a.txt")->x,==,'D');commit(rows,"Stop tracking a");g_ptr_array_unref(rows);
 gchar *p=g_build_filename(root,"a.txt",NULL);g_assert_true(g_file_test(p,G_FILE_TEST_EXISTS));g_free(p);text=output(tree);g_assert_cmpstr(text,==,"b.txt\n");g_free(text);
 /* Literal pathspecs: wildcard characters are filename bytes. */
 write_file("literal[1].txt","literal\n");write_file("literal1.txt","other\n");
 paths=g_ptr_array_new();g_ptr_array_add(paths,"literal[1].txt");g_assert_true(insight_git_paths(root,paths,TRUE,NULL,&error));g_assert_no_error(error);g_ptr_array_unref(paths);
 text=output(cached);g_assert_nonnull(strstr(text,"literal[1].txt"));g_assert_null(strstr(text,"literal1.txt"));g_free(text);
 rows=status();chosen=g_ptr_array_new();g_ptr_array_add(chosen,find(rows,"literal[1].txt"));commit(chosen,"Literal filename");g_ptr_array_unref(chosen);g_ptr_array_unref(rows);
 /* Same XY status with different content is still a stale review. */
 write_file("b.txt","second\n");rows=status();chosen=g_ptr_array_new();g_ptr_array_add(chosen,find(rows,"b.txt"));write_file("b.txt","third\n");g_assert_false(insight_git_commit(root,chosen,"Stale",&error));g_assert_nonnull(error);g_clear_error(&error);g_ptr_array_unref(chosen);g_ptr_array_unref(rows);
 /* Failed hooks leave HEAD and the original index intact. */
 gchar *hook=g_build_filename(root,".git","hooks","pre-commit",NULL);g_assert_true(g_file_set_contents(hook,"#!/bin/sh\nexit 1\n",-1,NULL));g_chmod(hook,0700);
 rows=status();chosen=g_ptr_array_new();g_ptr_array_add(chosen,find(rows,"b.txt"));g_assert_false(insight_git_commit(root,chosen,"Rejected",&error));g_assert_nonnull(error);g_clear_error(&error);g_ptr_array_unref(chosen);g_ptr_array_unref(rows);g_unlink(hook);g_free(hook);
 const gchar *head[]={"log","-1","--format=%s",NULL};text=output(head);g_assert_cmpstr(text,==,"Literal filename\n");g_free(text);
 /* External index locks are never removed or bypassed. */
 p=g_build_filename(root,".git","index.lock",NULL);g_assert_true(g_file_set_contents(p,"owner",-1,NULL));rows=status();chosen=g_ptr_array_new();g_ptr_array_add(chosen,find(rows,"b.txt"));g_assert_false(insight_git_commit(root,chosen,"Locked",&error));g_clear_error(&error);g_assert_true(g_file_test(p,G_FILE_TEST_EXISTS));g_unlink(p);g_free(p);g_ptr_array_unref(chosen);g_ptr_array_unref(rows);
 clean_tree(root);g_free(root);g_print("Git project: selective/unborn commits, staged isolation, cached removal, literal paths, stale review, hooks and locks passed\n");return 0;
}
