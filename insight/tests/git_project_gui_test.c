/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the production Git UI, with unrelated editor services supplied here. */
#include "../../src/insight-git.c"
#include <stdarg.h>
const gchar *insight_tr(const gchar *text) { return text; }
gchar *insight_i18n_printf(const gchar *format,...) { va_list args;va_start(args,format);gchar *text=g_strdup_vprintf(format,args);va_end(args);return text; }
GeanyMainWidgets main_widgets;
static GeanyApp test_app;
static gchar *opened;
GeanyApp *app=&test_app;
GPtrArray *documents_array;
gboolean document_save_file(GeanyDocument *doc,gboolean force) { (void)doc;(void)force;return TRUE; }
gboolean project_close(gboolean defaults) { (void)defaults;return TRUE; }
gboolean project_load_file_with_session(const gchar *file) { g_free(opened);opened=g_strdup(file);return TRUE; }
gchar *utils_get_locale_from_utf8(const gchar *text) { return g_strdup(text); }
gchar *utils_get_utf8_from_locale(const gchar *text) { return g_strdup(text); }
void ui_set_statusbar(gboolean log,const gchar *format,...) { (void)log;(void)format; }
GtkWidget *ui_lookup_widget(GtkWidget *widget,const gchar *name) { return g_object_get_data(G_OBJECT(widget),name); }
GtkWidget *toolbar_get_widget_by_name(const gchar *name) { return g_object_get_data(G_OBJECT(main_widgets.toolbar),name); }
void toolbar_item_ref(GtkToolItem *item) { (void)item; }
InsightProjectFiles *insight_project_files_new(void) { InsightProjectFiles *p=g_new0(InsightProjectFiles,1);p->files=g_ptr_array_new_with_free_func(g_free);return p; }
static guint refreshes,mode,responses;
static void refreshed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;refreshes++; }
static GtkWidget *named(GtkWidget *widget,const gchar *name)
{ if(!g_strcmp0(gtk_widget_get_name(widget),name)) return widget;if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static gboolean respond(gpointer data)
{
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-git-commit-dialog")) dialog=p->data;
 g_list_free(windows);
 if(!dialog || !gtk_widget_get_visible(dialog)) return G_SOURCE_CONTINUE;
 GtkWidget *view=named(dialog,"insight-git-commit-files"),*message=named(dialog,"insight-git-commit-message");g_assert_nonnull(view);g_assert_nonnull(message);
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,2);GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter_first(model,&iter));gboolean checked;gchar *path;gtk_tree_model_get(model,&iter,0,&checked,2,&path,-1);g_assert_true(checked);g_assert_cmpstr(path,==,"a.txt");g_free(path);
 gtk_tree_model_iter_next(model,&iter);gtk_tree_model_get(model,&iter,0,&checked,2,&path,-1);g_assert_true(checked);g_assert_cmpstr(path,==,"z.txt");g_free(path);
 if(mode) { GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(gtk_tree_view_get_column(GTK_TREE_VIEW(view),0)));g_signal_emit_by_name(cells->data,"toggled","1");g_list_free(cells);GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(message));GtkTextIter first,last;gtk_text_buffer_get_bounds(buffer,&first,&last);gchar *proposed=gtk_text_buffer_get_text(buffer,&first,&last,FALSE);g_assert_nonnull(strstr(proposed,"1 added"));g_free(proposed);gtk_text_buffer_set_text(buffer,"Edited selective message",-1); }
 responses++;gtk_dialog_response(GTK_DIALOG(dialog),mode ? GTK_RESPONSE_ACCEPT : GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
typedef struct { const gchar *root,*remote;gboolean clone; } CreateResponse;
static gboolean respond_create(gpointer data)
{
 CreateResponse *response=data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),response->clone ? "insight-git-clone" : "insight-git-create")) dialog=p->data;
 g_list_free(windows);if(!dialog || !gtk_widget_get_visible(dialog)) return G_SOURCE_CONTINUE;
 gtk_entry_set_text(GTK_ENTRY(named(dialog,"insight-git-project-name")),"Created by test");gtk_entry_set_text(GTK_ENTRY(named(dialog,"insight-git-project-path")),response->root);
 if(response->clone) gtk_entry_set_text(GTK_ENTRY(named(dialog,"insight-git-project-remote")),response->remote);
 gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static guint membership_responses;
static gboolean respond_membership(gpointer data)
{
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-git-membership")) dialog=p->data;
 g_list_free(windows);if(!dialog || !gtk_widget_get_visible(dialog)) return G_SOURCE_CONTINUE;
 GtkWidget *view=named(dialog,"insight-git-membership-files");g_assert_nonnull(view);GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));
 if(!membership_responses++) { g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,1);GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter_first(model,&iter));gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&iter);gtk_dialog_response(GTK_DIALOG(dialog),2);return G_SOURCE_CONTINUE; }
 /* The nested progress window can dispatch this timer while the old list is
  * still visible; wait until the production dialog has refreshed it. */
 if(gtk_tree_model_iter_n_children(model,NULL)) return G_SOURCE_CONTINUE;
 gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
}
static void run_git(const gchar *const *args) { GError *e=NULL;GBytes *b=insight_git_run(git_root,NULL,args,NULL,&e);g_assert_no_error(e);g_assert_nonnull(b);g_bytes_unref(b); }
int main(int argc,char **argv)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !gtk_init_check(&argc,&argv)) return 77;
 main_widgets.window=gtk_window_new(GTK_WINDOW_TOPLEVEL);main_widgets.toolbar=gtk_toolbar_new();documents_array=g_ptr_array_new();
 GtkWidget *project=gtk_menu_item_new_with_label("Project"),*menu=gtk_menu_new(),*file=gtk_menu_new();gtk_menu_item_set_submenu(GTK_MENU_ITEM(project),menu);g_object_ref_sink(project);g_object_ref_sink(file);g_object_set_data(G_OBJECT(main_widgets.window),"menu_project1",project);g_object_set_data(G_OBJECT(main_widgets.window),"file1_menu",file);
 GtkToolItem *save=gtk_tool_button_new(NULL,"Save");gtk_toolbar_insert(GTK_TOOLBAR(main_widgets.toolbar),save,-1);g_object_set_data(G_OBJECT(main_widgets.toolbar),"Save",save);g_object_ref_sink(main_widgets.toolbar);
 insight_git_install(G_CALLBACK(refreshed));insight_git_toolbar();g_assert_cmpint(gtk_toolbar_get_n_items(GTK_TOOLBAR(main_widgets.toolbar)),==,3);g_assert_cmpstr(gtk_widget_get_name(GTK_WIDGET(gtk_toolbar_get_nth_item(GTK_TOOLBAR(main_widgets.toolbar),1))),==,"insight-git-commit-tool");
 gchar *root=g_dir_make_tmp("insight-git-gui-XXXXXX",NULL);g_assert_nonnull(root);GKeyFile *config=g_key_file_new();g_key_file_set_boolean(config,"project","git_based",TRUE);insight_git_bind(config,root);g_assert_true(insight_git_active());
 const gchar *init[]={"init","--initial-branch=main",NULL},*name[]={"config","user.name","Test",NULL},*email[]={"config","user.email","test@example.invalid",NULL};run_git(init);run_git(name);run_git(email);
 gchar *a=g_build_filename(root,"a.txt",NULL),*z=g_build_filename(root,"z.txt",NULL);g_assert_true(g_file_set_contents(a,"a",-1,NULL));g_assert_true(g_file_set_contents(z,"z",-1,NULL));
 g_timeout_add(10,respond,NULL);commit_command(NULL,NULL);g_assert_cmpuint(responses,==,1);
 const gchar *head[]={"rev-parse","--verify","HEAD",NULL};GBytes *b=insight_git_run(root,NULL,head,NULL,NULL);g_assert_null(b);
 mode=1;g_timeout_add(10,respond,NULL);commit_command(NULL,NULL);g_assert_cmpuint(responses,==,2);
 const gchar *tree[]={"ls-tree","--name-only","HEAD",NULL};GError *e=NULL;b=insight_git_run(root,NULL,tree,NULL,&e);g_assert_no_error(e);gsize n;const gchar *p=g_bytes_get_data(b,&n);g_assert_cmpmem(p,n,"a.txt\n",6);g_bytes_unref(b);
 const gchar *log[]={"log","-1","--format=%s",NULL};b=insight_git_run(root,NULL,log,NULL,&e);g_assert_no_error(e);p=g_bytes_get_data(b,&n);g_assert_cmpmem(p,n,"Edited selective message\n",sizeof("Edited selective message\n")-1);g_bytes_unref(b);g_assert_cmpuint(refreshes,==,2);
 InsightProjectFiles *policy=insight_git_membership(&e);g_assert_no_error(e);g_assert_cmpuint(policy->files->len,==,1);g_ptr_array_unref(policy->files);g_free(policy);
 gchar *created=g_strconcat(root,"-created",NULL);CreateResponse creation={created,NULL,FALSE};g_timeout_add(10,respond_create,&creation);create_project(NULL,NULL);g_assert_nonnull(opened);GKeyFile *saved=g_key_file_new();g_assert_true(g_key_file_load_from_file(saved,opened,G_KEY_FILE_NONE,NULL));g_assert_true(g_key_file_get_boolean(saved,"project","git_based",NULL));g_key_file_unref(saved);
 gchar *cloned=g_strconcat(root,"-cloned",NULL);CreateResponse cloning={cloned,root,TRUE};g_timeout_add(10,respond_create,&cloning);create_project(NULL,GINT_TO_POINTER(1));g_assert_true(g_str_has_prefix(opened,cloned));saved=g_key_file_new();g_assert_true(g_key_file_load_from_file(saved,opened,G_KEY_FILE_NONE,NULL));g_assert_true(g_key_file_get_boolean(saved,"project","git_based",NULL));g_key_file_unref(saved);gchar *cloned_file=g_build_filename(cloned,"a.txt",NULL);g_assert_true(g_file_test(cloned_file,G_FILE_TEST_EXISTS));g_free(cloned_file);g_free(created);g_free(cloned);g_free(opened);
 g_timeout_add(10,respond_membership,NULL);insight_git_manage(NULL,NULL);g_assert_true(g_file_test(a,G_FILE_TEST_EXISTS));GPtrArray *tracked=insight_git_files(root,NULL,&e);g_assert_no_error(e);g_assert_cmpuint(tracked->len,==,0);g_ptr_array_unref(tracked);
 insight_git_close();g_assert_false(insight_git_active());gtk_widget_destroy(main_widgets.window);gtk_widget_destroy(main_widgets.toolbar);g_object_unref(main_widgets.toolbar);gtk_widget_destroy(project);g_object_unref(project);gtk_widget_destroy(file);g_object_unref(file);g_ptr_array_unref(documents_array);g_key_file_unref(config);g_free(root);g_free(a);g_free(z);
 g_print("Production Git GUI: menu/toolbar, sorted checked files, proposal editing, cancel, selective commit and Git membership passed\n");return 0;
}
