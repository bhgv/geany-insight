/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-relation-graph.h"
#include <string.h>
static GtkWidget *named(GtkWidget *root,const char *name) { if(!strcmp(gtk_widget_get_name(root),name)) return root;if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
static void selected(GtkTreeView *tree,const char *name) { GtkTreeModel *model;GtkTreeIter iter;g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(tree),&model,&iter));gchar *actual=NULL;gtk_tree_model_get(model,&iter,0,&actual,-1);g_assert_cmpstr(actual,==,name);g_free(actual); }
static void click(GtkWidget *canvas,double x,double y) { GdkEventButton event={0};event.type=GDK_BUTTON_PRESS;event.button=1;event.x=x;event.y=y;gboolean handled=FALSE;g_signal_emit_by_name(canvas,"button-press-event",&event,&handled);g_assert_true(handled); }
static void key(GtkWidget *canvas,guint value,guint modifiers) { GdkEventKey event={0};event.type=GDK_KEY_PRESS;event.keyval=value;event.state=modifiers;gboolean handled=FALSE;g_signal_emit_by_name(canvas,"key-press-event",&event,&handled);g_assert_true(handled); }
static void connector(GtkWidget *canvas,gint x,gint y) {
 key(canvas,GDK_KEY_c,GDK_CONTROL_MASK);GdkPixbuf *image=gtk_clipboard_wait_for_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(image);g_assert_cmpint(gdk_pixbuf_get_width(image),>,x);g_assert_cmpint(gdk_pixbuf_get_height(image),>,y);
 guchar *pixel=gdk_pixbuf_get_pixels(image)+y*gdk_pixbuf_get_rowstride(image)+x*gdk_pixbuf_get_n_channels(image);g_assert_cmpint(pixel[0]-pixel[2],>,20);g_object_unref(image);
}
int main(int argc,char **argv) {
 if(!gtk_init_check(&argc,&argv)) return 77;
 GtkTreeStore *store=gtk_tree_store_new(3,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG);GtkTreeIter root,parent,child,sibling;
 gtk_tree_store_append(store,&root,NULL);gtk_tree_store_set(store,&root,0,"Query",2,(gulong)0,-1);
 gtk_tree_store_append(store,&parent,&root);gtk_tree_store_set(store,&parent,0,"Parent",1,"types.cpp",2,(gulong)1,-1);
 gtk_tree_store_append(store,&child,&parent);gtk_tree_store_set(store,&child,0,"Child",1,"types.cpp",2,(gulong)2,-1);
 gtk_tree_store_append(store,&sibling,&root);gtk_tree_store_set(store,&sibling,0,"Sibling",1,"types.cpp",2,(gulong)3,-1);
 GtkWidget *window=gtk_offscreen_window_new(),*box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2),*query=gtk_entry_new(),*tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));gtk_entry_set_text(GTK_ENTRY(query),"Query");gtk_container_add(GTK_CONTAINER(window),box);gtk_box_pack_start(GTK_BOX(box),query,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),tree,FALSE,FALSE,0);gtk_tree_view_append_column(GTK_TREE_VIEW(tree),gtk_tree_view_column_new_with_attributes("Name",gtk_cell_renderer_text_new(),"text",0,NULL));
 GtkTreePath *root_path=gtk_tree_path_new_from_indices(0,-1),*parent_path=gtk_tree_path_new_from_indices(0,0,-1);gtk_tree_view_expand_row(GTK_TREE_VIEW(tree),root_path,FALSE);
 GtkWidget *graph=insight_relation_graph_new_tree(GTK_TREE_VIEW(tree),GTK_ENTRY(query),0,1,2);gtk_box_pack_start(GTK_BOX(box),graph,TRUE,TRUE,0);gtk_widget_show_all(window);for(guint i=0;i<100 && gtk_events_pending();i++) gtk_main_iteration();GtkWidget *canvas=named(graph,"insight-relation-tree-graph");g_assert_nonnull(canvas);
 click(canvas,268,28);selected(GTK_TREE_VIEW(tree),"Parent");click(canvas,438,32);g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tree),parent_path));click(canvas,508,92);selected(GTK_TREE_VIEW(tree),"Child");connector(canvas,468,64);
 key(canvas,GDK_KEY_Left,0);selected(GTK_TREE_VIEW(tree),"Parent");key(canvas,GDK_KEY_Left,0);g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tree),parent_path));click(canvas,268,92);selected(GTK_TREE_VIEW(tree),"Sibling");
 key(canvas,GDK_KEY_Home,0);key(canvas,GDK_KEY_Right,0);g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tree),parent_path));key(canvas,GDK_KEY_Down,0);selected(GTK_TREE_VIEW(tree),"Child");
 insight_relation_graph_set_options(graph,TRUE,100);click(canvas,248,196);selected(GTK_TREE_VIEW(tree),"Child");connector(canvas,220,160);
 click(canvas,108,136);g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tree),parent_path));click(canvas,248,116);selected(GTK_TREE_VIEW(tree),"Sibling");
 click(canvas,108,136);insight_relation_graph_set_options(graph,TRUE,150);click(canvas,248*1.5,196*1.5);selected(GTK_TREE_VIEW(tree),"Child");
 insight_relation_graph_set_options(graph,FALSE,100);click(canvas,198,32);g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tree),root_path));click(canvas,198,32);g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(tree),root_path));
 gtk_widget_destroy(graph);gtk_tree_view_collapse_all(GTK_TREE_VIEW(tree));gtk_tree_store_clear(store);gtk_widget_destroy(window);gtk_tree_path_free(root_path);gtk_tree_path_free(parent_path);g_object_unref(store);
 g_print("Tree graph GUI: nested hit paths, real parent connectors in clipboard image, toggle controls, keyboard, orientations, zoom, root collapse and safe destruction passed\n");return 0;
}
