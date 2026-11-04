/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/slang_completion.h"
#include <glib/gstdio.h>
#include <string.h>
static gpointer cancel_later(gpointer data) {g_usleep(100000);g_cancellable_cancel(data);return NULL;}
int main(int argc,char **argv) {
 if(argc>1 && !strcmp(argv[1],"--editor-complete")) {g_usleep(30000000);return 0;}
 g_assert_cmpint(argc,==,2);gchar *dir=g_dir_make_tmp("slang adapter XXXX-separator-XXXXXX",NULL);g_assert_nonnull(dir);
 gchar *leaf=g_build_filename(dir,"leaf.sv",NULL),*top=g_build_filename(dir,"top.sv",NULL);
 const gchar *text="module top; leaf u_leaf(); initial $display(u_leaf.data_); endmodule\n";
 g_assert_true(g_file_set_contents(leaf,"module leaf; logic old_signal; endmodule\n",-1,NULL));g_assert_true(g_file_set_contents(top,text,-1,NULL));
 GPtrArray *paths=g_ptr_array_new();g_ptr_array_add(paths,leaf);g_ptr_array_add(paths,top);
 GHashTable *buffers=g_hash_table_new(g_str_hash,g_str_equal);g_hash_table_insert(buffers,top,(gpointer)text);g_hash_table_insert(buffers,leaf,"module leaf; logic data_input, data_output; endmodule\n");
 guint offset=strstr(text,"data_")-text+5;GCancellable *cancel=g_cancellable_new();
 insight_slang_cache_clear();
 GPtrArray *items=insight_slang_complete(argv[1],top,text,offset,5,paths,buffers,cancel);g_assert_nonnull(items);g_assert_cmpuint(items->len,==,2);
 for(guint i=0;i<items->len;i++) {InsightClangCompletion *item=g_ptr_array_index(items,i);g_assert_true(g_str_has_prefix(item->name,"data_"));g_assert_cmpstr(item->type,==,"Variable");}g_ptr_array_unref(items);
 InsightSlangCacheStats before,after;insight_slang_cache_stats(&before);
 items=insight_slang_complete(argv[1],top,text,offset,5,paths,buffers,cancel);g_assert_nonnull(items);g_ptr_array_unref(items);insight_slang_cache_stats(&after);g_assert_cmpuint(after.hits,==,before.hits+1);g_assert_cmpuint(after.starts,==,before.starts);
 const gchar *original_overlay=g_hash_table_lookup(buffers,leaf);g_hash_table_replace(buffers,leaf,"module leaf; logic data_changed; endmodule\n");
 items=insight_slang_complete(argv[1],top,text,offset,5,paths,buffers,cancel);g_assert_nonnull(items);g_assert_cmpuint(items->len,==,1);g_assert_cmpstr(((InsightClangCompletion *)g_ptr_array_index(items,0))->name,==,"data_changed");g_ptr_array_unref(items);insight_slang_cache_stats(&after);g_assert_cmpuint(after.starts,==,before.starts+1);g_hash_table_replace(buffers,leaf,(gpointer)original_overlay);
 g_hash_table_replace(buffers,leaf,"`timescale 1ns/1ps\nmodule leaf; logic data_input, data_output; endmodule\n");insight_slang_cache_stats(&before);
 for(guint i=0;i<2;i++) {items=insight_slang_complete(argv[1],top,text,offset,5,paths,buffers,cancel);g_assert_nonnull(items);g_ptr_array_unref(items);}insight_slang_cache_stats(&after);g_assert_cmpuint(after.starts,==,before.starts+2);g_assert_cmpuint(after.hits,==,before.hits);g_assert_cmpuint(after.bypasses,==,before.bypasses+2);g_hash_table_replace(buffers,leaf,(gpointer)original_overlay);
 gchar *disk=NULL;g_assert_true(g_file_get_contents(leaf,&disk,NULL,NULL));g_assert_nonnull(strstr(disk,"old_signal"));g_free(disk);
 const gchar *navigation="module top; leaf u_leaf(); initial $display(u_leaf.data_input); endmodule\n";g_hash_table_replace(buffers,top,(gpointer)navigation);
 gchar *target=NULL;guint line=0,column=0;guint caret=strstr(navigation,"data_input")-navigation+strlen("data_input");
 g_assert_true(insight_slang_definition(argv[1],top,navigation,caret,paths,buffers,cancel,&target,&line,&column));g_assert_cmpstr(target,==,leaf);g_assert_cmpuint(line,==,1);g_assert_cmpuint(column,==,20);g_free(target);g_hash_table_replace(buffers,top,(gpointer)text);
 g_hash_table_remove(buffers,leaf);g_assert_true(g_file_set_contents(leaf,"module leaf; logic data_disk_one; endmodule\n",-1,NULL));
 items=insight_slang_complete(argv[1],top,text,offset,5,paths,buffers,cancel);g_assert_nonnull(items);g_assert_cmpstr(((InsightClangCompletion *)g_ptr_array_index(items,0))->name,==,"data_disk_one");g_ptr_array_unref(items);insight_slang_cache_stats(&before);
 g_assert_true(g_file_set_contents(leaf,"module leaf; logic data_disk_two; endmodule\n",-1,NULL));items=insight_slang_complete(argv[1],top,text,offset,5,paths,buffers,cancel);g_assert_nonnull(items);g_assert_cmpstr(((InsightClangCompletion *)g_ptr_array_index(items,0))->name,==,"data_disk_two");g_ptr_array_unref(items);insight_slang_cache_stats(&after);g_assert_cmpuint(after.starts,==,before.starts+1);g_hash_table_replace(buffers,leaf,(gpointer)original_overlay);
 for(guint position=0;position<24;position++) {gchar *unused=NULL;guint l=0,c=0;insight_slang_definition(argv[1],top,text,position,paths,buffers,cancel,&unused,&l,&c);g_free(unused);}insight_slang_cache_stats(&after);g_assert_cmpuint(after.entries,<=,8);g_assert_cmpuint(after.bytes,<=,4u*1024u*1024u);
 g_cancellable_cancel(cancel);g_assert_null(insight_slang_complete(argv[1],top,text,offset,5,paths,buffers,cancel));
 g_assert_null(insight_slang_complete("/missing-service",top,text,offset,5,paths,buffers,NULL));
 g_object_unref(cancel);cancel=g_cancellable_new();GThread *thread=g_thread_new("cancel-hdl",cancel_later,cancel);gint64 started=g_get_monotonic_time();
 gchar *self=g_strdup(argv[0]);g_assert_null(insight_slang_complete(self,top,text,offset,5,paths,buffers,cancel));g_free(self);g_thread_join(thread);g_assert_cmpint(g_get_monotonic_time()-started,<,3000000);
 g_object_unref(cancel);g_hash_table_unref(buffers);g_ptr_array_unref(paths);g_unlink(leaf);g_unlink(top);g_rmdir(dir);g_free(leaf);g_free(top);g_free(dir);
 g_print("HDL worker adapter: unsaved cross-file completion, cancellation and missing service fallback declaration navigation and bounded cache invalidation passed\n");return 0;
}
