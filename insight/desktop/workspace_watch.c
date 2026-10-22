/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "workspace_watch.h"
#include "glib_compat.h"
struct InsightWorkspaceWatch {
 GHashTable *directories,*paths;
 guint pending,delay;
 InsightWorkspaceChanged changed;
 gpointer data;
};
static void monitor_free(gpointer value) {
 GFileMonitor *monitor=value;
 g_file_monitor_cancel(monitor);
 g_object_unref(monitor);
}
static gboolean deliver(gpointer data) {
 InsightWorkspaceWatch *watch=data;
 watch->pending=0;
 /* The callback may clear or destroy the watcher. */
 if(watch->changed) watch->changed(watch->data);
 return G_SOURCE_REMOVE;
}
static gboolean hidden(GFile *file) {
 if(!file) return FALSE;
 gchar *name=g_file_get_basename(file);
 gboolean skip=name && (name[0]=='.' || !g_strcmp0(name,"node_modules") || !g_strcmp0(name,"__pycache__"));
 g_free(name); return skip;
}
static void changed(GFileMonitor *monitor,GFile *file,GFile *other,GFileMonitorEvent event,gpointer data) {
 (void)monitor; InsightWorkspaceWatch *watch=data;
 if(event==G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED || event==G_FILE_MONITOR_EVENT_PRE_UNMOUNT || (hidden(file) && (!other || hidden(other)))) return;
 GFile *items[]={file,other};for(guint n=0;n<2;n++) if(items[n]) { gchar *path=g_file_get_path(items[n]);if(path) g_hash_table_add(watch->paths,path); }
 if(watch->pending) g_source_remove(watch->pending);
 watch->pending=g_timeout_add(watch->delay,deliver,watch);
}
InsightWorkspaceWatch *insight_workspace_watch_new(guint delay,InsightWorkspaceChanged callback,gpointer data) {
 InsightWorkspaceWatch *watch=g_new0(InsightWorkspaceWatch,1);
 watch->paths=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 watch->directories=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,monitor_free);
 watch->delay=MAX(delay,10); watch->changed=callback; watch->data=data; return watch;
}
gboolean insight_workspace_watch_add(InsightWorkspaceWatch *watch,const char *directory,GError **error) {
 g_return_val_if_fail(watch && directory,FALSE);
 gchar *path=insight_canonicalize_filename(directory,NULL);
 if(g_hash_table_contains(watch->directories,path)) { g_free(path); return TRUE; }
 if(g_hash_table_size(watch->directories)>=1024) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Directory monitor limit reached (1024)"); g_free(path); return FALSE; }
 GFile *file=g_file_new_for_path(path);
 GFileMonitor *monitor=g_file_monitor_directory(file,G_FILE_MONITOR_WATCH_MOVES,NULL,error); g_object_unref(file);
 if(!monitor) { g_free(path); return FALSE; }
 g_signal_connect(monitor,"changed",G_CALLBACK(changed),watch);
 g_hash_table_insert(watch->directories,path,monitor); return TRUE;
}
void insight_workspace_watch_clear(InsightWorkspaceWatch *watch) {
 if(!watch) return;
 if(watch->pending) { g_source_remove(watch->pending); watch->pending=0; }
 g_hash_table_remove_all(watch->directories);g_hash_table_remove_all(watch->paths);
}
void insight_workspace_watch_free(InsightWorkspaceWatch *watch) {
 if(!watch) return;
 insight_workspace_watch_clear(watch); g_hash_table_unref(watch->directories);g_hash_table_unref(watch->paths); g_free(watch);
}
guint insight_workspace_watch_count(const InsightWorkspaceWatch *watch) { return watch ? g_hash_table_size(watch->directories) : 0; }

GPtrArray *insight_workspace_watch_take_changes(InsightWorkspaceWatch *watch) { GPtrArray *paths=g_ptr_array_new_with_free_func(g_free);GHashTableIter it;gpointer key;g_hash_table_iter_init(&it,watch->paths);while(g_hash_table_iter_next(&it,&key,NULL)) g_ptr_array_add(paths,g_strdup(key));g_hash_table_remove_all(watch->paths);return paths; }
