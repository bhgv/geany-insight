/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*flat,*tree,*query,*mode;static gchar *file;static guint phase,waits;static gint caret;
static const gchar *source="class Root {};\nclass Middle : public Root {};\nclass End : public Middle {};\nclass Other : public Root {};\n";
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
typedef struct { const gchar *name;gint depth;GtkTreePath *path; } Find;
static gboolean find(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) { Find *f=data;gchar *name=NULL;gtk_tree_model_get(model,iter,7,&name,-1);gboolean match=!g_strcmp0(name,f->name) && (!f->depth || gtk_tree_path_get_depth(path)==f->depth);g_free(name);if(match) f->path=gtk_tree_path_copy(path);return match; }
static GtkTreePath *row(GtkWidget *view,const gchar *name,gint depth) { Find f={name,depth,NULL};gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),find,&f);return f.path; }
static gboolean has(GtkWidget *view,const gchar *name,gint depth) { GtkTreePath *p=row(view,name,depth);if(!p) return FALSE;gtk_tree_path_free(p);return TRUE; }
static void select_row(GtkWidget *view,const gchar *name,gint depth) { GtkTreePath *p=row(view,name,depth);g_assert_nonnull(p);gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),p,NULL,FALSE);gtk_tree_path_free(p); }
static gint count(void) { return gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(flat)),NULL); }
static void click(const gchar *name) { gtk_button_clicked(GTK_BUTTON(named(window,name))); }
static void toggle(const gchar *name,gboolean value) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,name)),value); }
static void key(GtkWidget *widget,guint value) { GdkEventKey e={0};e.type=GDK_KEY_PRESS;e.keyval=value;e.window=gtk_widget_get_window(widget);gboolean handled=FALSE;g_signal_emit_by_name(widget,"key-press-event",&e,&handled);g_assert_true(handled); }
static void reveal(GtkWidget *widget) { for(GtkWidget *child=widget,*parent=gtk_widget_get_parent(child);parent;child=parent,parent=gtk_widget_get_parent(child)) if(GTK_IS_NOTEBOOK(parent)) gtk_notebook_set_current_page(GTK_NOTEBOOK(parent),gtk_notebook_page_num(GTK_NOTEBOOK(parent),child)); }
static void request(const gchar *owner,gint relation) { gtk_combo_box_set_active(GTK_COMBO_BOX(mode),relation);gtk_entry_set_text(GTK_ENTRY(query),owner);g_signal_emit_by_name(query,"activate"); }
static gboolean tick(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(file,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!flat) { flat=named(window,"insight-relations");tree=named(window,"insight-relations-tree");query=named(window,"insight-relation-query");mode=named(window,"insight-relation-mode");g_assert_nonnull(tree); }
 if(phase==1) { caret=sci_get_current_position(doc->editor->sci);request("Root",5);phase++; }
 else if(phase==2) {
  if(count()!=2 || !has(flat,"Other",1) || !has(flat,"Middle",1)) return G_SOURCE_CONTINUE;
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(flat))),7,GTK_SORT_DESCENDING);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-relation-filter")),"Other");g_assert_cmpint(count(),==,1);select_row(flat,"Other",1);click("insight-relation-node-remove");g_assert_cmpint(count(),==,0);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-relation-filter")),"");g_assert_cmpint(count(),==,1);g_assert_true(has(flat,"Middle",1));g_assert_false(has(flat,"Other",1));click("insight-relation-node-restore");phase++;
 }else if(phase==3) {
  if(count()!=2) return G_SOURCE_CONTINUE;toggle("insight-relation-graph-toggle",TRUE);select_row(flat,"Other",1);key(named(window,"insight-relation-graph"),GDK_KEY_Delete);g_assert_cmpint(count(),==,1);g_assert_false(has(flat,"Other",1));key(named(window,"insight-relation-graph"),GDK_KEY_F5);phase++;
 }else if(phase==4) {
  if(count()!=2) return G_SOURCE_CONTINUE;request("End",4);toggle("insight-relation-tree-toggle",TRUE);phase++;
 }else if(phase==5) {
  GtkTreePath *p=row(tree,"Middle",2);if(!p) return G_SOURCE_CONTINUE;gtk_tree_view_expand_row(GTK_TREE_VIEW(tree),p,FALSE);gtk_tree_path_free(p);phase++;
 }else if(phase==6) {
  if(!has(tree,"Root",3)) return G_SOURCE_CONTINUE;select_row(tree,"Middle",2);key(named(window,"insight-relation-tree-graph"),GDK_KEY_Delete);g_assert_false(has(tree,"Middle",2));g_assert_false(has(tree,"Root",3));g_assert_true(has(tree,"End",1));select_row(tree,"End",1);click("insight-relation-node-remove");g_assert_true(has(tree,"End",1));click("insight-relation-node-restore");phase++;
 }else if(phase==7) {
  if(!has(tree,"Root",3)) return G_SOURCE_CONTINUE;toggle("insight-relation-graph-toggle",FALSE);reveal(tree);select_row(tree,"Root",3);key(tree,GDK_KEY_Menu);GtkWidget *menu=g_object_get_data(G_OBJECT(tree),"insight-node-menu");g_assert_nonnull(menu);GtkWidget *remove=named(menu,"insight-relation-node-remove-menu");g_assert_nonnull(remove);g_assert_true(gtk_widget_get_sensitive(remove));gtk_menu_item_activate(GTK_MENU_ITEM(remove));gtk_menu_popdown(GTK_MENU(menu));g_assert_false(has(tree,"Root",3));g_assert_true(has(tree,"Middle",2));key(tree,GDK_KEY_F5);phase++;
 }else if(phase==8) {
  if(!has(tree,"Middle",2)) return G_SOURCE_CONTINUE;select_row(tree,"Middle",2);gtk_widget_grab_focus(tree);if(!gtk_widget_has_focus(tree)) return G_SOURCE_CONTINUE;key(window,GDK_KEY_Delete);waits=0;phase++;
 }else if(phase==9) {
  if(waits++<4) return G_SOURCE_CONTINUE;g_assert_false(has(tree,"Middle",2));g_assert_false(has(tree,"Root",3));g_assert_true(has(tree,"End",1));g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(query)),==,"End");g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);g_assert_cmpstr(text,==,source);g_free(text);g_assert_false(doc->changed);
  g_print("Native relation nodes: filtered/sorted deletion, shared graph model, branch deletion, protected root, outline menu, Delete/F5 restore, worker completion safety and unchanged source passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Relation node GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("insight-nodes-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL),*sd=g_build_filename(profile,"insight",NULL),*settings=g_build_filename(sd,"workbench.conf",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);file=g_build_filename(dir,"types.cpp",NULL);g_assert_true(g_file_set_contents(file,source,-1,NULL));g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nfollow=false\nrelation-follow=false\nproject-visible=true\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *argv[]={"nodes-gui","--new-instance","--no-session","--config",profile,NULL};guint step=g_timeout_add(250,tick,NULL),limit=g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);if(g_main_context_find_source_by_id(NULL,step)) g_source_remove(step);if(g_main_context_find_source_by_id(NULL,limit)) g_source_remove(limit);g_free(dir);g_free(profile);g_free(sd);g_free(settings);g_free(config);g_free(file);return result;
}
