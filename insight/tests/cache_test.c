/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "index_cache.h"
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
 g_assert_cmpint(argc,==,2);
 gchar *directory=g_build_filename(argv[1],"cache-XXXXXX",NULL);
 g_assert_nonnull(g_mkdtemp(directory));
 gchar *path=g_build_filename(directory,"project.idx",NULL);
 InsightIndex *disk=insight_index_new(); g_assert_nonnull(disk);
 InsightSymbol a[]={ {"saved",NULL,"Scope","f",10,NULL,NULL,NULL} };
 g_assert(insight_index_update(disk,"/project/a.cpp",a,1));
 GError *error=NULL;
 g_assert(insight_cache_save(path,disk,"/project",&error)); g_assert_no_error(error);
 char *root=NULL; InsightIndex *loaded=insight_cache_load(path,&root,&error);
 g_assert_no_error(error); g_assert_nonnull(loaded); g_assert_cmpstr(root,==,"/project"); free(root);
 const InsightSymbol *out[1]; g_assert_cmpuint(insight_index_find(loaded,"saved",1,out,1),==,1);
 insight_index_free(loaded);
 /* Live document overlay never changes the separately stored disk snapshot. */
 InsightIndex *live=insight_index_clone(disk); g_assert_nonnull(live);
 InsightSymbol edit[]={ {"unsaved",NULL,NULL,NULL,20,NULL,NULL,NULL} };
 g_assert(insight_index_update(live,"/project/a.cpp",edit,1));
 loaded=insight_cache_load(path,&root,&error); g_assert_no_error(error);
 g_assert_cmpuint(insight_index_find(loaded,"unsaved",1,NULL,0),==,0);
 free(root); insight_index_free(loaded); insight_index_free(live);
 /* Serialization failure must leave an already stored cache intact. */
 g_assert(!insight_cache_save(path,NULL,"/project",&error)); g_assert_nonnull(error); g_clear_error(&error);
 loaded=insight_cache_load(path,&root,&error); g_assert_no_error(error);
 g_assert_cmpuint(insight_index_find(loaded,"saved",1,NULL,0),==,1);
 free(root); insight_index_free(loaded);
 g_assert(insight_index_update(disk,"/project/a.cpp",edit,1));
 g_assert(insight_cache_save(path,disk,"/project",&error)); g_assert_no_error(error);
 loaded=insight_cache_load(path,&root,&error); g_assert_no_error(error);
 g_assert_cmpuint(insight_index_find(loaded,"unsaved",1,NULL,0),==,1);
 free(root); insight_index_free(loaded);
 g_assert(g_file_set_contents(path,"corrupt",7,&error)); g_assert_no_error(error);
 root=(char *)1; loaded=insight_cache_load(path,&root,&error);
 g_assert_null(loaded); g_assert_null(root); g_assert_nonnull(error); g_clear_error(&error);
 g_assert_cmpuint(insight_index_find(disk,"unsaved",1,NULL,0),==,1);
 g_assert_cmpint(g_unlink(path),==,0);
 loaded=insight_cache_load(path,&root,&error);
 g_assert_null(loaded); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND); g_clear_error(&error);
 g_assert_cmpint(g_rmdir(directory),==,0);
 g_free(path); g_free(directory); insight_index_free(disk);
 g_print("cache: all checks passed\n"); return 0;
}
