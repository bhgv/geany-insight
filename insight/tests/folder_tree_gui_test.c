/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/folder_tree.h"
#include <glib/gstdio.h>
#include <string.h>
const gchar *insight_tr(const gchar *text) { return text; }
static GtkWidget *window,*view;static GtkTreeStore *store;static const gchar *wanted;static GtkTreePath *found;
static gboolean find_row(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data)
{ (void)data;gchar *value;gtk_tree_model_get(model,iter,INSIGHT_FOLDER_PATH,&value,-1);gboolean equal=!g_strcmp0(value,wanted);g_free(value);if(equal) found=gtk_tree_path_copy(path);return equal; }
static GtkTreePath *row(const gchar *path)
{ wanted=path;found=NULL;gtk_tree_model_foreach(GTK_TREE_MODEL(store),find_row,NULL);return found; }
static gint state(const gchar *path)
{ GtkTreePath *p=row(path);if(!p) return -1;GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter(GTK_TREE_MODEL(store),&iter,p));gint value;gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,INSIGHT_FOLDER_STATE,&value,-1);gtk_tree_path_free(p);return value; }
static void wait_state(const gchar *path,gint expected)
{ gint64 end=g_get_monotonic_time()+5*G_TIME_SPAN_SECOND;while(state(path)!=expected && g_get_monotonic_time()<end) { while(g_main_context_iteration(NULL,FALSE));g_usleep(1000); }g_assert_cmpint(state(path),==,expected); }
static void expand(const gchar *path)
{ GtkTreePath *p=row(path);g_assert_nonnull(p);gtk_tree_view_expand_row(GTK_TREE_VIEW(view),p,FALSE);gtk_tree_path_free(p); }
static void action(const gchar *path,InsightFolderAction value)
{ GtkTreePath *p=row(path);g_assert_nonnull(p);gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),p);gtk_tree_path_free(p);insight_folder_tree_action(GTK_TREE_VIEW(view),value); }
static guint children(const gchar *path)
{ GtkTreePath *p=row(path);GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter(GTK_TREE_MODEL(store),&iter,p));gtk_tree_path_free(p);GtkTreeIter child;guint n=0;for(gboolean valid=gtk_tree_model_iter_children(GTK_TREE_MODEL(store),&child,&iter);valid;valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&child)) { gchar *value;gtk_tree_model_get(GTK_TREE_MODEL(store),&child,INSIGHT_FOLDER_PATH,&value,-1);if(value) n++;g_free(value); }return n; }
static void make_dir(const gchar *path) { g_assert_cmpint(g_mkdir_with_parents(path,0700),==,0); }
static void clean(const gchar *path) { GDir *dir=g_dir_open(path,0,NULL);if(dir) { const gchar *name;while((name=g_dir_read_name(dir))) { gchar *p=g_build_filename(path,name,NULL);if(g_file_test(p,G_FILE_TEST_IS_DIR) && !g_file_test(p,G_FILE_TEST_IS_SYMLINK)) clean(p);else g_unlink(p);g_free(p); }g_dir_close(dir); }g_rmdir(path); }
int main(int argc,char **argv)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !gtk_init_check(&argc,&argv)) return 77;
 gchar *root=g_dir_make_tmp("insight-folder-tree-XXXXXX",NULL),*branch=g_build_filename(root,"unvisited",NULL),*deep=g_build_filename(branch,"Дочерняя",NULL),*leaf=g_build_filename(deep,"leaf",NULL),*hidden=g_build_filename(branch,".hidden",NULL),*ghost=g_build_filename(root,"gone",NULL),*many=g_build_filename(root,"many",NULL);make_dir(leaf);make_dir(hidden);make_dir(ghost);make_dir(many);
 for(guint i=0;i<300;i++) { gchar *name=g_strdup_printf("folder-%03u",i),*p=g_build_filename(many,name,NULL);make_dir(p);g_free(name);g_free(p); }
 for(guint i=0;i<2100;i++) { gchar *name=g_strdup_printf("file-%04u.txt",i),*p=g_build_filename(root,name,NULL);g_assert_true(g_file_set_contents(p,"",0,NULL));g_free(name);g_free(p); }
 gchar *link=g_build_filename(branch,"cycle",NULL);GFile *link_file=g_file_new_for_path(link);gboolean linked=g_file_make_symbolic_link(link_file,root,NULL,NULL);g_object_unref(link_file);
 window=gtk_window_new(GTK_WINDOW_TOPLEVEL);store=gtk_tree_store_new(INSIGHT_FOLDER_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_STRING);view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Folders",gtk_cell_renderer_text_new(),"text",INSIGHT_FOLDER_NAME,NULL));insight_folder_tree_install(GTK_TREE_VIEW(view),store);gtk_container_add(GTK_CONTAINER(window),view);gtk_widget_show_all(window);
 insight_folder_tree_current(GTK_TREE_VIEW(view),root,FALSE,TRUE);wait_state(root,INSIGHT_FOLDER_READY);g_assert_cmpuint(children(root),==,3);g_assert_cmpint(state(branch),==,INSIGHT_FOLDER_UNKNOWN);
 expand(branch);wait_state(branch,INSIGHT_FOLDER_READY);g_assert_cmpuint(children(branch),==,1);g_assert_cmpint(state(deep),==,INSIGHT_FOLDER_UNKNOWN);g_assert_cmpint(state(hidden),==,-1);if(linked) g_assert_cmpint(state(link),==,-1);
 expand(deep);wait_state(deep,INSIGHT_FOLDER_READY);g_assert_cmpint(state(leaf),==,INSIGHT_FOLDER_UNKNOWN);expand(leaf);wait_state(leaf,INSIGHT_FOLDER_READY);g_assert_cmpuint(children(leaf),==,0);
 /* Refreshing a parent retains expanded descendants. */
 action(branch,INSIGHT_FOLDER_REFRESH);wait_state(branch,INSIGHT_FOLDER_READY);g_assert_cmpint(state(leaf),==,INSIGHT_FOLDER_READY);GtkTreePath *p=row(deep);g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),p));gtk_tree_path_free(p);
 /* Separate directory enumeration is independent of the 2048-file panel cap. */
 expand(many);wait_state(many,INSIGHT_FOLDER_LIMITED);g_assert_cmpuint(children(many),==,256);action(many,INSIGHT_FOLDER_MORE);wait_state(many,INSIGHT_FOLDER_READY);g_assert_cmpuint(children(many),==,300);
 action(many,INSIGHT_FOLDER_REFRESH);wait_state(many,INSIGHT_FOLDER_READY);g_assert_cmpuint(children(many),==,300);
 /* A removed row reports an error and can recover without changing root. */
 g_assert_cmpint(g_rmdir(ghost),==,0);expand(ghost);wait_state(ghost,INSIGHT_FOLDER_ERROR);make_dir(ghost);action(ghost,INSIGHT_FOLDER_REFRESH);wait_state(ghost,INSIGHT_FOLDER_READY);
 action(branch,INSIGHT_FOLDER_REFRESH);action(branch,INSIGHT_FOLDER_CANCEL);g_assert_cmpint(state(branch),==,INSIGHT_FOLDER_UNKNOWN);action(branch,INSIGHT_FOLDER_REFRESH);wait_state(branch,INSIGHT_FOLDER_READY);
 /* In-flight old rows cannot publish after a hidden-mode reset. */
 action(many,INSIGHT_FOLDER_REFRESH);insight_folder_tree_current(GTK_TREE_VIEW(view),root,TRUE,TRUE);wait_state(root,INSIGHT_FOLDER_READY);expand(branch);wait_state(branch,INSIGHT_FOLDER_READY);g_assert_cmpint(state(hidden),==,INSIGHT_FOLDER_UNKNOWN);
 action(branch,INSIGHT_FOLDER_COLLAPSE);p=row(branch);g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),p));gtk_tree_path_free(p);action(branch,INSIGHT_FOLDER_EXPAND);p=row(branch);g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),p));gtk_tree_path_free(p);
 insight_folder_tree_action(GTK_TREE_VIEW(view),INSIGHT_FOLDER_RESET);wait_state(root,INSIGHT_FOLDER_READY);g_assert_cmpint(state(branch),==,INSIGHT_FOLDER_UNKNOWN);
 action(many,INSIGHT_FOLDER_REFRESH);insight_folder_tree_shutdown(GTK_TREE_VIEW(view));gtk_widget_destroy(window);g_object_unref(store);gint64 end=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<end) { while(g_main_context_iteration(NULL,FALSE));g_usleep(1000); }
 clean(root);g_free(root);g_free(branch);g_free(deep);g_free(leaf);g_free(hidden);g_free(ghost);g_free(many);g_free(link);g_print("Lazy folder tree: unvisited/Unicode/deep/empty folders, retained expansion, pagination, file saturation, symlink exclusion, retry, cancel, reset, hidden supersession and shutdown passed\n");return 0;
}
