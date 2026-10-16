/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "../desktop/folder_tree.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *root,*branch,*deep,*added;static GtkWidget *window,*folders;static guint phase,document_id;
static GtkWidget *named(GtkWidget *w,const gchar *name)
{ if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(sub) { GtkWidget *p=named(sub,name);if(p) return p; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *p=NULL;for(GList *i=children;i && !p;i=i->next) p=named(i->data,name);g_list_free(children);return p; }return NULL; }
static const gchar *wanted;static GtkTreePath *found;
static gboolean find_row(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data)
{ (void)data;gchar *value;gtk_tree_model_get(model,iter,INSIGHT_FOLDER_PATH,&value,-1);gboolean equal=!g_strcmp0(value,wanted);g_free(value);if(equal) found=gtk_tree_path_copy(path);return equal; }
static GtkTreePath *row(const gchar *path) { wanted=path;found=NULL;gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(folders)),find_row,NULL);return found; }
static gboolean has(const gchar *path) { GtkTreePath *p=row(path);if(p) gtk_tree_path_free(p);return p!=NULL; }
static void unchanged(void)
{ g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(window,"insight-disk-root"))),==,root);g_assert_cmpuint(document_get_current()->id,==,document_id);GtkWidget *files=named(window,"insight-project-files");g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(files)),NULL),==,1); }
static gboolean step(gpointer data)
{
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 if(!phase) {
  window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));folders=named(window,"insight-disk-folders");GtkWidget *files=named(window,"insight-project-files");if(!folders || !files || gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(files)),NULL)!=1) return G_SOURCE_CONTINUE;
  gchar *path=g_build_filename(root,"indexed.c",NULL);doc=document_open_file(path,FALSE,NULL,NULL);g_assert_nonnull(doc);document_id=doc->id;g_free(path);GtkWidget *mode=named(window,"insight-folder-mode"),*page=mode;while(gtk_widget_get_parent(page) && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) page=gtk_widget_get_parent(page);GtkWidget *book=gtk_widget_get_parent(page);if(GTK_IS_NOTEBOOK(book)) gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),page));gtk_window_resize(GTK_WINDOW(window),1200,800);gtk_combo_box_set_active(GTK_COMBO_BOX(mode),1);GtkWidget *entry=named(window,"insight-disk-root");gtk_entry_set_text(GTK_ENTRY(entry),root);g_signal_emit_by_name(entry,"activate");phase=1;
 } else if(phase==1) {
  if(!has(branch)) return G_SOURCE_CONTINUE;
  unchanged();g_assert_false(has(deep));GtkTreePath *p=row(branch);gtk_tree_view_expand_row(GTK_TREE_VIEW(folders),p,FALSE);gtk_tree_path_free(p);phase=2;
 } else if(phase==2) {
  if(!has(deep)) return G_SOURCE_CONTINUE;
  unchanged();g_assert_cmpint(g_mkdir(added,0700),==,0);GtkTreePath *p=row(branch);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(folders),p,NULL,TRUE,0.5f,0);gtk_tree_path_free(p);phase=3;
 } else if(phase==3) {
  GtkTreePath *p=row(branch);GdkRectangle area;gtk_tree_view_get_cell_area(GTK_TREE_VIEW(folders),p,gtk_tree_view_get_column(GTK_TREE_VIEW(folders),0),&area);gtk_tree_path_free(p);if(area.height<1) return G_SOURCE_CONTINUE;
  GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);event->button.window=g_object_ref(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(folders)));event->button.button=3;event->button.x=area.x+5;event->button.y=area.y+5;gdk_event_set_device(event,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));gboolean handled=FALSE;g_signal_emit_by_name(folders,"button-press-event",event,&handled);g_assert_true(handled);gdk_event_free(event);unchanged();GtkWidget *menu=g_object_get_data(G_OBJECT(folders),"insight-disk-folder-popup");g_assert_nonnull(menu);gtk_menu_item_activate(GTK_MENU_ITEM(named(menu,"insight-disk-folder-menu-branch-refresh")));gtk_menu_popdown(GTK_MENU(menu));phase=4;
 } else if(phase==4) {
  if(!has(added)) return G_SOURCE_CONTINUE;
  unchanged();GtkTreePath *p=row(branch);g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(folders),p));gtk_tree_path_free(p);p=row(deep);gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(folders)),p);gtk_tree_path_free(p);phase=5;
 } else if(phase==5) {
  if(strcmp(gtk_entry_get_text(GTK_ENTRY(named(window,"insight-disk-root"))),deep)) return G_SOURCE_CONTINUE;
  g_assert_cmpuint(document_get_current()->id,==,document_id);g_print("Native lazy Folder Browser: unvisited expansion and branch refresh without navigation/editor changes; explicit folder navigation passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Native lazy folders timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 root=g_dir_make_tmp("insight-lazy-native-XXXXXX",NULL);branch=g_build_filename(root,"unvisited",NULL);deep=g_build_filename(branch,"Дочерняя",NULL);added=g_build_filename(branch,"new-folder",NULL);g_assert_cmpint(g_mkdir_with_parents(deep,0700),==,0);gchar *profile=g_build_filename(root,"profile",NULL),*config=g_build_filename(profile,"geany.conf",NULL),*project=g_build_filename(root,"test.geany",NULL),*indexed=g_build_filename(root,"indexed.c",NULL);g_assert_cmpint(g_mkdir(profile,0700),==,0);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(indexed,"int indexed;\n",-1,NULL));g_assert_true(g_file_set_contents(project,"[project]\nname=Lazy folders\nbase_path=.\n[insight-project-files]\nversion=1\nautomatic=false\nexclude-masks=\nfiles=indexed.c;\ntrees=\nexcluded-files=\nexcluded-trees=\n",-1,NULL));
 gchar *argv[]={"lazy-folder-native","--new-instance","--no-session","--config",profile,project,NULL};g_timeout_add(100,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,argv);g_free(root);g_free(branch);g_free(deep);g_free(added);g_free(profile);g_free(config);g_free(project);g_free(indexed);return result;
}
