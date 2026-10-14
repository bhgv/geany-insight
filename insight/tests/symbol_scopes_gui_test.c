/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*local,*scope_toggle,*query;static gchar *source,*python;static guint phase;
static GtkWidget *named(GtkWidget *root,const char *name) { if(!strcmp(gtk_widget_get_name(root),name)) return root;if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=c;p && !found;p=p->next) found=named(p->data,name);g_list_free(c);return found; }return NULL; }
typedef struct { const char *name;GtkTreePath *path;gboolean group; } Find;
static gboolean find(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *it,gpointer data) { Find *f=data;gchar *name=NULL;gulong line=0;gtk_tree_model_get(model,it,0,&name,3,&line,-1);gboolean match=!g_strcmp0(name,f->name) && (f->group ? !line : line!=0);g_free(name);if(match) f->path=gtk_tree_path_copy(path);return match; }
static GtkTreePath *row(const char *name,gboolean group) { Find f={name,NULL,group};gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(local)),find,&f);return f.path; }
static void ancestry(GtkTreePath *path,const char *inner,const char *middle,const char *outer) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(local));const char *expected[]={inner,middle,outer};
 for(guint n=0;n<3;n++) { g_assert_true(gtk_tree_path_up(path));GtkTreeIter it;g_assert_true(gtk_tree_model_get_iter(model,&it,path));gchar *name=NULL;gulong line=1;gtk_tree_model_get(model,&it,0,&name,3,&line,-1);g_assert_cmpstr(name,==,expected[n]);g_assert_cmpuint(line,==,0);g_free(name); }
}
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(source,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!local) { local=named(window,"insight-local-symbols");query=named(window,"insight-local-query");scope_toggle=named(window,"insight-local-scope-hierarchy");g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scope_toggle)));g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(window,"insight-project-scope-hierarchy")))); }
 if(!gtk_widget_get_sensitive(local)) return G_SOURCE_CONTINUE;
 GtkTreePath *path=NULL;
 if(phase==1) { path=row("run",FALSE);if(!path) return G_SOURCE_CONTINUE;g_assert_cmpint(gtk_tree_path_get_depth(path),==,4);ancestry(path,"Widget","Inner","Outer");gtk_tree_path_free(path);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-local-group-types")),TRUE);phase++; }
 else if(phase==2) { path=row("run",FALSE);if(!path || gtk_tree_path_get_depth(path)!=5) { if(path) gtk_tree_path_free(path);return G_SOURCE_CONTINUE; }ancestry(path,"Widget","Inner","Outer");gtk_tree_path_free(path);
  path=row("Outer",TRUE);g_assert_nonnull(path);gtk_tree_view_set_cursor(GTK_TREE_VIEW(local),path,NULL,FALSE);gtk_tree_path_free(path);
  path=row("Widget",TRUE);g_assert_nonnull(path);gtk_tree_view_collapse_row(GTK_TREE_VIEW(local),path);gtk_tree_path_free(path);
  gtk_entry_set_text(GTK_ENTRY(query),"run");phase++; }
 else if(phase==3) { path=row("run",FALSE);if(!path || gtk_tree_path_get_depth(path)!=5) { if(path) gtk_tree_path_free(path);return G_SOURCE_CONTINUE; }ancestry(path,"Widget","Inner","Outer");gtk_tree_path_free(path);
  path=row("Widget",TRUE);g_assert_nonnull(path);g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(local),path));gtk_tree_path_free(path);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope_toggle),FALSE);phase++; }
 else if(phase==4) { path=row("run",FALSE);if(!path || gtk_tree_path_get_depth(path)!=2) { if(path) gtk_tree_path_free(path);return G_SOURCE_CONTINUE; }gtk_tree_path_free(path);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-local-group-types")),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scope_toggle),TRUE);gtk_entry_set_text(GTK_ENTRY(query),"");document_open_file(python,FALSE,NULL,NULL);phase++; }
 else if(phase==5) { path=row("method",FALSE);if(!path) return G_SOURCE_CONTINUE;g_assert_cmpint(gtk_tree_path_get_depth(path),==,3);gtk_tree_path_up(path);GtkTreeIter it;GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(local));g_assert_true(gtk_tree_model_get_iter(model,&it,path));gchar *name=NULL;gtk_tree_model_get(model,&it,0,&name,-1);g_assert_cmpstr(name,==,"Nested");g_free(name);gtk_tree_path_free(path);phase++; }
 else { g_print("Scope hierarchy GUI: C++ namespace/class ancestors, type grouping, filtered ancestors, flat toggle, independent persisted panels, Python nested classes passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Scope hierarchy GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("scopes-gui-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL),*profile=g_build_filename(directory,"profile",NULL),*sd=g_build_filename(profile,"insight",NULL),*settings=g_build_filename(sd,"workbench.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);
 source=g_build_filename(directory,"types.cpp",NULL);python=g_build_filename(directory,"types.py",NULL);
 g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nfollow=false\nproject-visible=true\nfile-symbols-scopes=true\nproject-symbols-scopes=false\n",-1,NULL));
 g_assert_true(g_file_set_contents(source,"namespace Outer { namespace Inner {\nclass Widget { public: int run() { return 1; } };\n}}\n",-1,NULL));g_assert_true(g_file_set_contents(python,"class Container:\n    class Nested:\n        def method(self):\n            return 1\n",-1,NULL));
 gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *argv[]={"scopes-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);
 GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL));g_assert_true(g_key_file_get_boolean(key,"workbench","file-symbols-scopes",NULL));g_assert_false(g_key_file_get_boolean(key,"workbench","project-symbols-scopes",NULL));g_key_file_unref(key);g_free(cwd);g_free(name);g_free(directory);g_free(profile);g_free(sd);g_free(settings);g_free(config);g_free(source);g_free(python);return result;
}
