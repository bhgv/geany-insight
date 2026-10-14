/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*local,*project,*field,*direction;static gchar *profile,*path,*settings;static guint phase;static gint caret;
static GtkWidget *named(GtkWidget *root,const gchar *name) {
 if(!strcmp(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_MENU_ITEM(root)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(sub) { GtkWidget *found=named(sub,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static void sorted(GtkTreeModel *model,gint expected,GtkSortType order) { gint field=-1;GtkSortType direction;g_assert_true(gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(model),&field,&direction));g_assert_cmpint(field,==,expected);g_assert_cmpint(direction,==,order); }
static void first(GtkTreeModel *model,const gchar *expected) { GtkTreeIter it;g_assert_true(gtk_tree_model_get_iter_first(model,&it));gchar *name=NULL;gtk_tree_model_get(model,&it,0,&name,-1);g_assert_cmpstr(name,==,expected);g_free(name); }
static gboolean timeout(gpointer data) { (void)data;g_error("Symbol sorting GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(path,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!local) { local=named(window,"insight-local-symbols");project=named(window,"insight-project-symbols");GtkWidget *box=named(window,"insight-local-sort");field=named(box,"insight-symbol-sort-field");direction=named(box,"insight-symbol-sort-descending"); }
 if(!gtk_widget_get_sensitive(local) || !gtk_widget_get_sensitive(project)) return G_SOURCE_CONTINUE;
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(local));
 if(phase==1) {
  if(gtk_tree_model_iter_n_children(model,NULL)!=3) return G_SOURCE_CONTINUE;
  sorted(model,3,GTK_SORT_DESCENDING);first(model,"middle_variable");g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(field)),==,1);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(direction)));g_assert_null(gtk_button_get_label(GTK_BUTTON(direction)));g_assert_true(GTK_IS_IMAGE(gtk_button_get_image(GTK_BUTTON(direction))));
  GtkWidget *box=named(window,"insight-project-sort");g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(named(box,"insight-symbol-sort-field"))),==,4);
  caret=sci_get_current_position(doc->editor->sci);gtk_combo_box_set_active(GTK_COMBO_BOX(field),0);phase++;
 } else if(phase==2) {
  sorted(model,0,GTK_SORT_DESCENDING);first(model,"z_first");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(direction),FALSE);phase++;
 } else if(phase==3) {
  sorted(model,0,GTK_SORT_ASCENDING);first(model,"a_second");GtkTreeViewColumn *column=gtk_tree_view_get_column(GTK_TREE_VIEW(local),1);gtk_tree_view_column_clicked(column);phase++;
 } else if(phase==4) {
  sorted(model,3,GTK_SORT_ASCENDING);first(model,"z_first");g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(field)),==,1);gtk_tree_view_column_clicked(gtk_tree_view_get_column(GTK_TREE_VIEW(local),1));phase++;
 } else if(phase==5) {
  sorted(model,3,GTK_SORT_DESCENDING);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(direction)));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-local-group-types")),TRUE);gtk_combo_box_set_active(GTK_COMBO_BOX(field),0);phase++;
 } else if(phase==6) {
  if(!GTK_IS_TREE_STORE(model)) return G_SOURCE_CONTINUE;
  sorted(model,0,GTK_SORT_DESCENDING);first(model,"Variables");gtk_tree_view_column_clicked(gtk_tree_view_get_column(GTK_TREE_VIEW(local),0));phase++;
 } else if(phase==7) {
  sorted(model,0,GTK_SORT_ASCENDING);first(model,"Functions");g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(direction)));gtk_combo_box_set_active(GTK_COMBO_BOX(field),3);phase++;
 } else if(phase==8) {
  sorted(model,1,GTK_SORT_ASCENDING);gtk_combo_box_set_active(GTK_COMBO_BOX(field),4);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(direction),TRUE);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"second");phase++;
 } else if(phase==9) {
  if(gtk_tree_model_iter_n_children(model,NULL)!=1) return G_SOURCE_CONTINUE;
  sorted(model,2,GTK_SORT_DESCENDING);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);phase++;
 } else {
  g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);gchar *tip=gtk_widget_get_tooltip_text(direction);g_assert_cmpstr(tip,==,"По убыванию");g_free(tip);
  g_print("Symbol sorting GUI passed: saved independent orders, line/name/scope/path, both directions, flat/grouped header synchronization, query rebuild, icon-only control, RU tooltip, unchanged editor and settings write\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("symbol-sort-gui-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL);g_free(cwd);g_free(name);profile=g_build_filename(directory,"profile",NULL);path=g_build_filename(directory,"sample.c",NULL);gchar *sd=g_build_filename(profile,"insight",NULL);g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);settings=g_build_filename(sd,"workbench.conf",NULL);g_free(sd);
 g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nproject-visible=true\nfile-symbols-sort-column=3\nfile-symbols-sort-descending=true\nproject-symbols-sort-column=2\nproject-symbols-sort-descending=false\n",-1,NULL));
 g_assert_true(g_file_set_contents(path,"int z_first(void) { return 1; }\nint a_second(void) { return 2; }\nint middle_variable;\n",-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"symbol-sort-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(200,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);
 GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL));g_assert_cmpint(g_key_file_get_integer(key,"workbench","file-symbols-sort-column",NULL),==,2);g_assert_true(g_key_file_get_boolean(key,"workbench","file-symbols-sort-descending",NULL));g_assert_cmpint(g_key_file_get_integer(key,"workbench","project-symbols-sort-column",NULL),==,2);g_assert_false(g_key_file_get_boolean(key,"workbench","project-symbols-sort-descending",NULL));g_key_file_unref(key);g_free(directory);g_free(profile);g_free(path);g_free(settings);return result;
}
