/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "workspace_watch.h"
#include <glib/gstdio.h>
static guint deliveries;
static void on_change(gpointer data) { (void)data; deliveries++; }
static void pump(guint milliseconds) {
 gint64 until=g_get_monotonic_time()+milliseconds*1000;
 while(g_get_monotonic_time()<until) { while(g_main_context_iteration(NULL,FALSE)) {} g_usleep(1000); }
}
int main(void) {
 GError *error=NULL; gchar *template=g_strdup("watch-test-XXXXXX"); gchar *directory=g_mkdtemp(template); g_assert_nonnull(directory);
 gchar *root=g_canonicalize_filename(directory,NULL),*file=g_build_filename(root,"source.c",NULL),*renamed=g_build_filename(root,"renamed.c",NULL),*hidden=g_build_filename(root,".ignored",NULL);
 InsightWorkspaceWatch *watch=insight_workspace_watch_new(100,on_change,NULL);
 g_assert_true(insight_workspace_watch_add(watch,root,&error)); g_assert_no_error(error);
 g_assert_true(insight_workspace_watch_add(watch,root,&error)); g_assert_cmpuint(insight_workspace_watch_count(watch),==,1);
 g_assert_true(g_file_set_contents(file,"int a;",-1,&error)); pump(500); g_assert_cmpuint(deliveries,>,0);
 deliveries=0;
 g_assert_true(g_file_set_contents(file,"int b;",-1,&error));
 g_assert_true(g_file_set_contents(file,"int c;",-1,&error));
 g_assert_cmpint(g_rename(file,renamed),==,0); pump(500); g_assert_cmpuint(deliveries,==,1);
 deliveries=0; g_assert_cmpint(g_unlink(renamed),==,0); pump(500); g_assert_cmpuint(deliveries,>,0);
 deliveries=0; g_assert_true(g_file_set_contents(hidden,"ignored",-1,&error)); pump(300); g_assert_cmpuint(deliveries,==,0);
 insight_workspace_watch_clear(watch); g_assert_cmpuint(insight_workspace_watch_count(watch),==,0);
 g_assert_true(g_file_set_contents(file,"after clear",-1,&error)); pump(300); g_assert_cmpuint(deliveries,==,0);
 insight_workspace_watch_free(watch); g_unlink(file); g_unlink(hidden); g_rmdir(root); g_free(hidden); g_free(renamed); g_free(file); g_free(root); g_free(template);
 g_print("Workspace watcher checks passed\n"); return 0;
}
