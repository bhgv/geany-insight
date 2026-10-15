/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*view,*query,*mode,*filter;static gchar *source;static guint phase,step_source,timeout_source;static gint caret;
static GtkWidget *named(GtkWidget *root,const char *name) { if(!strcmp(gtk_widget_get_name(root),name)) return root;if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
typedef struct { const char *name;GtkTreePath *path;gint depth; } Find;
static gboolean find(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) { Find *f=data;gchar *name=NULL;gtk_tree_model_get(model,iter,7,&name,-1);gboolean match=!g_strcmp0(name,f->name) && (!f->depth || gtk_tree_path_get_depth(path)==f->depth);g_free(name);if(match) f->path=gtk_tree_path_copy(path);return match; }
static GtkTreePath *row(const char *name,gint depth) { Find f={name,NULL,depth};gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),find,&f);return f.path; }
static gboolean expand(const char *name,gint depth) { GtkTreePath *path=row(name,depth);if(!path) return FALSE;gtk_tree_view_expand_row(GTK_TREE_VIEW(view),path,FALSE);gtk_tree_path_free(path);return TRUE; }
static void request(const char *owner,gint relation) { gtk_combo_box_set_active(GTK_COMBO_BOX(mode),relation);gtk_entry_set_text(GTK_ENTRY(query),owner);g_signal_emit_by_name(query,"activate"); }
static gboolean has(const char *name,gint depth) { GtkTreePath *path=row(name,depth);if(!path) return FALSE;gtk_tree_path_free(path);return TRUE; }
static void graph_click(double x,double y) { GdkEventButton event={0};event.type=GDK_BUTTON_PRESS;event.button=1;event.x=x;event.y=y;gboolean handled=FALSE;g_signal_emit_by_name(named(window,"insight-relation-tree-graph"),"button-press-event",&event,&handled);g_assert_true(handled); }
static void graph_key(guint value,guint modifiers) { GdkEventKey event={0};event.type=GDK_KEY_PRESS;event.keyval=value;event.state=modifiers;gboolean handled=FALSE;g_signal_emit_by_name(named(window,"insight-relation-tree-graph"),"key-press-event",&event,&handled);g_assert_true(handled); }
static void show_graph(void) { GtkWidget *page=named(window,"insight-relation-tree-graph");while(gtk_widget_get_parent(page) && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) page=gtk_widget_get_parent(page);GtkWidget *book=gtk_widget_get_parent(page);g_assert_true(GTK_IS_NOTEBOOK(book));gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),page)); }
static gboolean drain_done(gpointer data) { g_main_loop_quit(data);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;static guint last=G_MAXUINT;if(last!=phase) { g_printerr("Relation tree phase %u\n",phase);last=phase; }GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(source,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!view) { view=named(window,"insight-relations-tree");query=named(window,"insight-relation-query");mode=named(window,"insight-relation-mode");filter=named(window,"insight-relation-filter");g_assert_nonnull(view); }
 if(phase==1) { caret=sci_get_current_position(doc->editor->sci);request("N::End",4);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-relation-tree-toggle")),TRUE);phase++; }
 else if(phase==2) { if(!expand("Middle",2)) return G_SOURCE_CONTINUE;phase++; }
 else if(phase==3) { if(!has("Root",3)) return G_SOURCE_CONTINUE;g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);GtkTreePath *path=row("Middle",2);gtk_tree_view_collapse_row(GTK_TREE_VIEW(view),path);g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),path));gtk_tree_view_expand_row(GTK_TREE_VIEW(view),path,FALSE);g_assert_true(has("Root",3));gtk_tree_path_free(path);gtk_entry_set_text(GTK_ENTRY(filter),"~rt");phase++; }
 else if(phase==4) { if(!has("Root",3) || !has("Middle",2)) return G_SOURCE_CONTINUE;gtk_entry_set_text(GTK_ENTRY(filter),"no-such-symbol");g_assert_false(has("Middle",2));gtk_entry_set_text(GTK_ENTRY(filter),"");g_assert_true(has("Root",3));GtkTreePath *path=row("Middle",2);gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE);gtk_tree_path_free(path);gtk_button_clicked(GTK_BUTTON(named(window,"insight-relation-definitions")));phase++; }
 else if(phase==5) { if(gtk_combo_box_get_active(GTK_COMBO_BOX(mode))!=1) return G_SOURCE_CONTINUE;gtk_button_clicked(GTK_BUTTON(named(window,"insight-relation-back")));phase++; }
 else if(phase==6) { if(!has("Root",3)) return G_SOURCE_CONTINUE;GtkTreePath *path=row("Middle",2);g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),path));gtk_tree_path_free(path);request("N::Root",5);phase++; }
 else if(phase==7) { if(!expand("Middle",2)) return G_SOURCE_CONTINUE;phase++; }
 else if(phase==8) { if(!has("End",3)) return G_SOURCE_CONTINUE;request("N::LoopA",4);phase++; }
 else if(phase==9) { if(!expand("LoopB",2)) return G_SOURCE_CONTINUE;phase++; }
 else if(phase==10) { GtkTreePath *path=row("LoopA",3);if(!path) return G_SOURCE_CONTINUE;GtkTreeIter iter;GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));g_assert_true(gtk_tree_model_get_iter(model,&iter,path));g_assert_false(gtk_tree_model_iter_has_child(model,&iter));gchar *description=NULL;gtk_tree_model_get(model,&iter,8,&description,-1);g_assert_nonnull(strstr(description,"Cycle"));g_free(description);gtk_tree_path_free(path);request("N::End",2);phase++; }
 else if(phase==11) { if(!has("end_member",2)) return G_SOURCE_CONTINUE;g_assert_false(has("root_member",2));request("N::Root",2);phase++; }
 else if(phase==12) { if(!expand("Inner",2)) return G_SOURCE_CONTINUE;phase++; }
 else if(phase==13) { if(!has("inner_member",3)) return G_SOURCE_CONTINUE;request("N::Wide",5);phase++; }
 else if(phase==14) { if(!has("Branch result limit reached (200)",2)) return G_SOURCE_CONTINUE;GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter root;g_assert_true(gtk_tree_model_get_iter_first(model,&root));g_assert_cmpint(gtk_tree_model_iter_n_children(model,&root),==,201);phase++; }
 else if(phase==15) { request("N::Deep00",4);phase++; }
 else if(phase==16) {
  static guint next=1;gchar *name=g_strdup_printf("Deep%02u",next);GtkTreePath *path=row(name,(gint)next+1);g_free(name);if(!path) return G_SOURCE_CONTINUE;
  if(next<32) { gtk_tree_view_expand_row(GTK_TREE_VIEW(view),path,FALSE);next++;gtk_tree_path_free(path);return G_SOURCE_CONTINUE; }
  gtk_tree_path_free(path);g_assert_true(has("Relation tree depth limit reached",34));phase++;
 }
 else if(phase==17) { request("N::End",4);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-relation-graph-toggle")),TRUE);gtk_window_resize(GTK_WINDOW(window),1200,850);show_graph();phase++; }
 else if(phase==18) { if(!has("Middle",2)) return G_SOURCE_CONTINUE;caret=sci_get_current_position(doc->editor->sci);graph_click(438,32);phase++; }
 else if(phase==19) { if(!has("Root",3)) return G_SOURCE_CONTINUE;g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);graph_click(268,28);graph_key(GDK_KEY_Left,0);GtkTreePath *path=row("Middle",2);g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),path));gtk_tree_path_free(path);graph_key(GDK_KEY_Right,0);phase++; }
 else if(phase==20) { if(!has("Root",3)) return G_SOURCE_CONTINUE;graph_key(GDK_KEY_c,GDK_CONTROL_MASK);GdkPixbuf *image=gtk_clipboard_wait_for_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(image);g_assert_cmpint(gdk_pixbuf_get_width(image),>,488);g_object_unref(image);gtk_combo_box_set_active(GTK_COMBO_BOX(named(window,"insight-relation-tree-graph-direction")),1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(window,"insight-relation-tree-graph-zoom")),125);phase++; }
 else if(phase==21) { request("N::Wide",5);phase++; }
 else if(phase==22) { if(!has("Wide_000",2)) return G_SOURCE_CONTINUE;g_assert_true(expand("Wide_000",2));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-relation-graph-toggle")),FALSE);g_print("Native expandable outline/graph passed: lazy branch toggles, hierarchy clipboard, active graph options, limits and shutdown with queued branch work\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 else { g_print("Expandable Relations GUI: lazy multilevel Bases/Derived/Members, no inherited-member leakage, cycles, branch limits, filtering ancestors, Back expansion history and persistence passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Relation tree GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("tree-gui-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL),*profile=g_build_filename(directory,"profile",NULL),*sd=g_build_filename(profile,"insight",NULL),*settings=g_build_filename(sd,"workbench.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);source=g_build_filename(directory,"types.cpp",NULL);
 g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nfollow=false\nrelation-follow=false\nproject-visible=true\n",-1,NULL));
 GString *code=g_string_new("namespace N {\nclass Root { public: int root_member; class Inner { public: int inner_member; }; };\nclass Middle : public Root {};\nclass End : public Middle { public: int end_member; };\nclass LoopA : public LoopB {};\nclass LoopB : public LoopA {};\nclass Wide {};\n");for(guint i=0;i<250;i++) g_string_append_printf(code,"class Wide_%03u : public Wide {};\n",i);for(guint i=0;i<34;i++) { if(i<33) g_string_append_printf(code,"class Deep%02u : public Deep%02u {};\n",i,i+1);else g_string_append_printf(code,"class Deep%02u {};\n",i); }g_string_append(code,"}\n");g_assert_true(g_file_set_contents(source,code->str,-1,NULL));g_string_free(code,TRUE);
 gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *argv[]={"tree-gui","--new-instance","--no-session","--config",profile,NULL};step_source=g_timeout_add(250,step,NULL);timeout_source=g_timeout_add_seconds(40,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);if(g_main_context_find_source_by_id(NULL,step_source)) g_source_remove(step_source);if(g_main_context_find_source_by_id(NULL,timeout_source)) g_source_remove(timeout_source);GMainLoop *drain=g_main_loop_new(NULL,FALSE);g_timeout_add(300,drain_done,drain);g_main_loop_run(drain);g_main_loop_unref(drain);GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL));g_assert_true(g_key_file_get_boolean(key,"workbench","relation-tree",NULL));g_assert_false(g_key_file_get_boolean(key,"workbench","relation-graph",NULL));g_assert_true(g_key_file_get_boolean(key,"workbench","graph-vertical",NULL));g_assert_cmpint(g_key_file_get_integer(key,"workbench","graph-zoom",NULL),==,125);g_key_file_unref(key);g_free(cwd);g_free(name);g_free(directory);g_free(profile);g_free(sd);g_free(settings);g_free(config);g_free(source);return result;
}
