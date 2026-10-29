/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/call_paths.h"
#include <string.h>
static GHashTable *files;
static gchar *load(const gchar *path,GCancellable *cancel,gpointer data) {(void)cancel;(void)data;return g_strdup(g_hash_table_lookup(files,path));}
static void add(InsightIndex *index,GPtrArray *paths,const gchar *path,const gchar *text,const InsightSymbol *tags,guint n) {g_hash_table_insert(files,g_strdup(path),g_strdup(text));g_ptr_array_add(paths,g_strdup(path));g_assert_true(insight_index_update(index,path,tags,n));}
int main(void) {
 files=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);InsightIndex *index=insight_index_new();GPtrArray *paths=g_ptr_array_new_with_free_func(g_free);
 InsightSymbol a[]={{.name="main",.kind="f",.line=1},{.name="left",.kind="f",.line=2},{.name="right",.kind="f",.line=3}};
 add(index,paths,"/project/main.c","int main(){struct opt x={.p=1}; left(); right();}\nvoid left(){ bridge(); }\nvoid right(){ bridge(); }\n",a,3);
 InsightSymbol b[]={{.name="bridge",.kind="f",.line=1},{.name="create_rule",.kind="f",.line=2}};
 add(index,paths,"/project/rules.c","void bridge(){ if (ready) create_rule(); }\nstatic void create_rule(){const char *s=\"fake()\"; /* phantom(); */}\n",b,2);
 InsightCallEndpoint from={"main",NULL,0},to={"create_rule",NULL,0};GError *error=NULL;InsightCallPaths *r=insight_call_paths_find(index,paths,&from,&to,FALSE,load,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(r);g_assert_cmpuint(r->paths->len,==,2);for(guint i=0;i<r->paths->len;i++) {GArray *path=g_ptr_array_index(r->paths,i);g_assert_cmpuint(path->len,==,3);guint first=g_array_index(path,guint,0),last=g_array_index(path,guint,2);InsightCallEdge e=g_array_index(r->edges,InsightCallEdge,first),z=g_array_index(r->edges,InsightCallEdge,last);g_assert_cmpuint(e.from,==,r->start);g_assert_cmpuint(z.to,==,r->target);g_assert_cmpuint(z.line,==,1);}insight_call_paths_free(r);
 /* A same-file static name hides a same-named function elsewhere. */
 InsightSymbol other[]={{.name="create_rule",.kind="f",.line=1}};add(index,paths,"/project/other.c","static void create_rule(){}\n",other,1);
 r=insight_call_paths_find(index,paths,&from,&to,FALSE,load,NULL,NULL,&error);g_assert_null(r);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_EXISTS);g_clear_error(&error);to.path="/project/rules.c";to.line=2;r=insight_call_paths_find(index,paths,&from,&to,FALSE,load,NULL,NULL,&error);g_assert_no_error(error);g_assert_cmpuint(r->paths->len,==,2);insight_call_paths_free(r);
 /* Recursion terminates; shortest direct path wins over a longer branch. */
 g_hash_table_replace(files,g_strdup("/project/main.c"),g_strdup("int main(){left();}\nvoid left(){main(); bridge();}\nvoid right(){}\n"));r=insight_call_paths_find(index,paths,&from,&to,FALSE,load,NULL,NULL,&error);g_assert_no_error(error);g_assert_cmpuint(r->paths->len,==,1);insight_call_paths_free(r);
 /* Member calls are not silently claimed as resolved direct calls. */
 g_hash_table_replace(files,g_strdup("/project/main.c"),g_strdup("int main(){ops->create_rule();}\nvoid left(){}\nvoid right(){}\n"));r=insight_call_paths_find(index,paths,&from,&to,FALSE,load,NULL,NULL,&error);g_assert_no_error(error);g_assert_cmpuint(r->paths->len,==,0);insight_call_paths_free(r);r=insight_call_paths_find(index,paths,&from,&to,TRUE,load,NULL,NULL,&error);g_assert_no_error(error);g_assert_cmpuint(r->paths->len,==,1);GArray *path=g_ptr_array_index(r->paths,0);g_assert_true(g_array_index(r->edges,InsightCallEdge,g_array_index(path,guint,0)).uncertain);insight_call_paths_free(r);
 GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);r=insight_call_paths_find(index,paths,&from,&to,FALSE,load,NULL,cancel,&error);g_assert_null(r);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&error);g_object_unref(cancel);
 g_hash_table_replace(files,g_strdup("/project/main.c"),g_strdup("int renamed(){left();}\nvoid left(){}\nvoid right(){}\n"));r=insight_call_paths_find(index,paths,&from,&to,FALSE,load,NULL,NULL,&error);g_assert_null(r);g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND);g_clear_error(&error);
 gchar *one=insight_call_digest("\357\273\277line\r\n"),*two=insight_call_digest("line\n");g_assert_cmpstr(one,==,two);g_free(one);g_free(two);
 insight_index_free(index);g_ptr_array_unref(paths);g_hash_table_unref(files);g_print("Call paths: intermediate functions, alternatives, static lookup, exact endpoints, cycles, uncertain member calls, cancellation and normalized digests passed\n");return 0;
}
