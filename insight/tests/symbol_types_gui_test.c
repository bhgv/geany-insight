/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*local,*project,*menu;static gchar *directory,*profile,*c_path,*py_path;static guint phase;
static GtkWidget *named(GtkWidget *root,const gchar *name) {
 if(!strcmp(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_MENU_ITEM(root)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(sub) { GtkWidget *found=named(sub,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static gboolean row(GtkTreeModel *m,const gchar *name,GtkTreeIter *result) { GtkTreeIter it;for(gboolean v=gtk_tree_model_get_iter_first(m,&it);v;v=gtk_tree_model_iter_next(m,&it)) { gchar *n=NULL;gtk_tree_model_get(m,&it,0,&n,-1);gboolean match=!g_strcmp0(n,name);g_free(n);if(match) { *result=it;return TRUE; } }return FALSE; }
static gboolean text(GtkWidget *root,const gchar *value) { if(GTK_IS_LABEL(root) && strstr(gtk_label_get_text(GTK_LABEL(root)),value)) return TRUE;if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));gboolean found=FALSE;for(GList *p=c;p && !found;p=p->next) found=text(p->data,value);g_list_free(c);return found; }return FALSE; }
static GtkWidget *types(GtkWidget *panel) { GtkWidget *button=named(panel,"insight-symbol-types");g_assert_true(GTK_IS_MENU_BUTTON(button));g_assert_null(gtk_button_get_label(GTK_BUTTON(button)));g_assert_true(GTK_IS_IMAGE(gtk_button_get_image(GTK_BUTTON(button))));return GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(button))); }
static void all(gboolean show) { GList *items=gtk_container_get_children(GTK_CONTAINER(menu));gtk_menu_item_activate(GTK_MENU_ITEM(g_list_nth_data(items,show ? 0 : 1)));g_list_free(items); }
static void category(guint number,gboolean active) { gchar *name=g_strdup_printf("insight-symbol-type-%u",number);GtkWidget *item=named(menu,name);g_assert_true(GTK_IS_CHECK_MENU_ITEM(item));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),active);g_free(name); }
static gboolean timeout(gpointer data) { (void)data;g_error("Symbol types GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(phase==0) { document_open_file(c_path,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!local) { local=named(window,"insight-local-symbols");project=named(window,"insight-project-symbols");menu=types(named(window,"insight-local-type-filters")); }
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(local));GtkTreeIter it;if(!gtk_widget_get_sensitive(local)) return G_SOURCE_CONTINUE;
 if(phase==1) {
  if(!row(model,"sample_function",&it)) return G_SOURCE_CONTINUE;
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,1);g_assert_false(row(model,"sample_variable",&it));g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(named(menu,"insight-symbol-type-0"))));g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(named(menu,"insight-symbol-type-5"))));
  GtkWidget *project_menu=types(named(window,"insight-project-type-filters"));g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(named(project_menu,"insight-symbol-type-1"))));g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(named(project_menu,"insight-symbol-type-0"))));all(TRUE);phase++;
 } else if(phase==2) {
  if(!row(model,"sample_member",&it)) return G_SOURCE_CONTINUE;
  g_assert_true(GTK_IS_LIST_STORE(model));g_assert_true(row(model,"sample_variable",&it));g_assert_true(row(model,"SampleStruct",&it));all(FALSE);phase++;
 } else if(phase==3) {
  GtkTreeModel *pm=gtk_tree_view_get_model(GTK_TREE_VIEW(project));GtkTreeIter structure;if(!gtk_widget_get_sensitive(project) || !row(pm,"Structures",&structure)) return G_SOURCE_CONTINUE;g_assert_true(GTK_IS_TREE_STORE(pm));g_assert_cmpint(gtk_tree_model_iter_n_children(pm,NULL),==,1);g_assert_cmpint(gtk_tree_model_iter_n_children(pm,&structure),==,1);
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);g_assert_true(text(named(window,"insight-local-type-filters"),"Symbols: 0 /"));category(5,TRUE);phase++;
 } else if(phase==4) {
  if(!row(model,"sample_member",&it)) return G_SOURCE_CONTINUE;
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,1);g_assert_false(row(model,"sample_function",&it));g_assert_false(row(model,"sample_variable",&it));gint caret=sci_get_current_position(doc->editor->sci);guint id=doc->id;gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(local)),&it);g_assert_cmpuint(document_get_current()->id,==,id);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"sample_function");phase++;
 } else if(phase==5) {
  if(!text(named(window,"insight-local-type-filters"),"Symbols: 0 / 1")) return G_SOURCE_CONTINUE;
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"");document_open_file(py_path,FALSE,NULL,NULL);phase++;
 } else if(phase==6) {
  if(!doc->tm_file || !doc->tm_file->tags_array) return G_SOURCE_CONTINUE;
  gboolean method=FALSE;for(guint i=0;i<doc->tm_file->tags_array->len;i++) { TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i);if(!strcmp(tag->name,"sample_method")) method=TRUE; }if(!method || !text(named(window,"insight-local-type-filters"),"Symbols: 0 / 3")) return G_SOURCE_CONTINUE;
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);category(0,TRUE);phase++;
 } else if(phase==7) {
  if(!row(model,"sample_method",&it)) return G_SOURCE_CONTINUE;
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,1);g_assert_false(row(model,"SampleClass",&it));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);phase++;
 } else {
  if(!text(named(window,"insight-local-type-filters"),"Символы: 1 / 3")) return G_SOURCE_CONTINUE;
  g_assert_true(text(menu,"Функции / методы"));GList *commands=gtk_container_get_children(GTK_CONTAINER(menu));g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(commands->data)),==,"Показать все типы символов");g_list_free(commands);g_print("Symbol types GUI passed: persisted independent file/project masks, icon-only controls, show/hide all, native C field filtering, name-query intersection and counters, preview-only selection, Python method mapping, live RU and saved masks\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("symbol-types-gui-%u",g_random_int());directory=g_build_filename(cwd,name,NULL);profile=g_build_filename(directory,"profile",NULL);c_path=g_build_filename(directory,"sample.c",NULL);py_path=g_build_filename(directory,"sample.py",NULL);g_free(cwd);g_free(name);gchar *settings_dir=g_build_filename(profile,"insight",NULL);g_assert_cmpint(g_mkdir_with_parents(settings_dir,0700),==,0);gchar *settings=g_build_filename(settings_dir,"workbench.conf",NULL);g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nproject-visible=true\nvisible=true\nfollow=true\nfile-symbol-types=1\nproject-symbol-types=2\nproject-symbols-grouped=true\n",-1,NULL));g_free(settings_dir);
 g_assert_true(g_file_set_contents(c_path,"int sample_function(void) { return 1; }\nint sample_variable;\nstruct SampleStruct { int sample_member; };\n#define SAMPLE_MACRO 1\n",-1,NULL));g_assert_true(g_file_set_contents(py_path,"class SampleClass:\n    def sample_method(self):\n        return 1\n",-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"symbol-types-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(200,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL));g_assert_cmpint(g_key_file_get_integer(key,"workbench","file-symbol-types",NULL),==,33);g_assert_cmpint(g_key_file_get_integer(key,"workbench","project-symbol-types",NULL),==,2);g_key_file_unref(key);g_free(settings);g_free(profile);g_free(directory);g_free(c_path);g_free(py_path);return result;
}
