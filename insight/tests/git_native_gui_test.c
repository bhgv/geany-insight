/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *root,*project;static guint phase;static GtkWidget *window;
static GtkWidget *named(GtkWidget *w,const gchar *name)
{ if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(sub) { GtkWidget *found=named(sub,name);if(found) return found; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static void git(const gchar *const *args)
{ GSubprocessLauncher *launcher=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE|G_SUBPROCESS_FLAGS_STDERR_PIPE);g_subprocess_launcher_set_cwd(launcher,root);GPtrArray *argv=g_ptr_array_new();g_ptr_array_add(argv,"git");for(guint i=0;args[i];i++) g_ptr_array_add(argv,(gpointer)args[i]);g_ptr_array_add(argv,NULL);GError *error=NULL;GSubprocess *p=g_subprocess_launcher_spawnv(launcher,(const gchar *const *)argv->pdata,&error);g_assert_no_error(error);g_assert_true(g_subprocess_wait_check(p,NULL,&error));g_assert_no_error(error);g_object_unref(p);g_object_unref(launcher);g_ptr_array_unref(argv); }
static void write_file(const gchar *name,const gchar *text) { gchar *p=g_build_filename(root,name,NULL);g_assert_true(g_file_set_contents(p,text,-1,NULL));g_free(p); }
static gboolean respond(gpointer data)
{
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-git-commit-dialog")) dialog=p->data;g_list_free(windows);if(!dialog || !gtk_widget_get_visible(dialog)) return G_SOURCE_CONTINUE;
 GtkWidget *view=named(dialog,"insight-git-commit-files");GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter iter;guint chosen=0;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *name;gboolean checked;gtk_tree_model_get(model,&iter,0,&checked,2,&name,-1);g_assert_true(checked);gboolean keep=!strcmp(name,"tracked.c");gtk_list_store_set(GTK_LIST_STORE(model),&iter,0,keep,-1);chosen+=keep;g_free(name); }g_assert_cmpuint(chosen,==,1);
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(dialog,"insight-git-commit-message")));gtk_text_buffer_set_text(buffer,"Native editor selective commit",-1);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static gboolean step(gpointer data)
{
 (void)data;if(phase) return G_SOURCE_CONTINUE;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));GtkWidget *files=named(window,"insight-project-files");if(!files) return G_SOURCE_CONTINUE;GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(files));if(gtk_tree_model_iter_n_children(model,NULL)!=1) return G_SOURCE_CONTINUE;GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter_first(model,&iter));gchar *name;gtk_tree_model_get(model,&iter,0,&name,-1);g_assert_cmpstr(name,==,"tracked.c");g_free(name);
 g_assert_nonnull(named(window,"insight-create-git-project"));g_assert_nonnull(named(window,"insight-create-from-git"));g_assert_nonnull(named(window,"insight-git-add-tool"));g_assert_nonnull(named(window,"insight-git-commit-tool"));
 GtkWidget *save=ui_lookup_widget(window,"menu_save_as1"),*commit=named(window,"insight-git-commit-menu");g_assert_nonnull(commit);GList *children=gtk_container_get_children(GTK_CONTAINER(gtk_widget_get_parent(save)));GList *save_link=g_list_find(children,save);g_assert_nonnull(save_link);g_assert_true(save_link->next->data==commit);g_list_free(children);
 phase=1;write_file("tracked.c","int changed;\n");g_timeout_add(20,respond,NULL);gtk_menu_item_activate(GTK_MENU_ITEM(commit));
 const gchar *verify[]={"log","-1","--format=%s",NULL};GSubprocessLauncher *launcher=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE);g_subprocess_launcher_set_cwd(launcher,root);GSubprocess *p=g_subprocess_launcher_spawn(launcher,NULL,"git",verify[0],verify[1],verify[2],NULL);gchar *out=NULL;g_assert_true(g_subprocess_communicate_utf8(p,NULL,NULL,&out,NULL,NULL));g_assert_cmpstr(out,==,"Native editor selective commit\n");g_free(out);g_object_unref(p);g_object_unref(launcher);
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Native Git GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("insight-git-native-XXXXXX",NULL);root=g_build_filename(dir,"repository",NULL);g_assert_cmpint(g_mkdir(root,0700),==,0);project=g_build_filename(dir,"test.geany",NULL);gchar *profile=g_build_filename(dir,"profile",NULL);
 const gchar *init[]={"init","--initial-branch=main",NULL},*name[]={"config","user.name","Test",NULL},*email[]={"config","user.email","test@example.invalid",NULL},*add[]={"add","tracked.c",NULL},*commit[]={"commit","-m","Initial",NULL};git(init);git(name);git(email);write_file("tracked.c","int first;\n");write_file("outside.txt","untracked\n");git(add);git(commit);
 gchar *text=g_strdup_printf("[project]\nname=Git native\nbase_path=%s\ngit_based=true\n",root);g_assert_true(g_file_set_contents(project,text,-1,NULL));g_free(text);
 gchar *argv[]={"git-native-gui","--new-instance","--no-session","--config",profile,project,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,argv);
 GKeyFile *config=g_key_file_new();g_assert_true(g_key_file_load_from_file(config,project,G_KEY_FILE_NONE,NULL));g_assert_true(g_key_file_get_boolean(config,"project","git_based",NULL));g_key_file_unref(config);g_free(root);g_free(project);g_free(profile);g_free(dir);g_print("Native editor Git integration: tracked membership, menu/toolbar placement, selective commit and saved binding passed\n");return result;
}
