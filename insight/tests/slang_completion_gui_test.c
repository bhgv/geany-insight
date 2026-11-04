/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "keybindings.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *root,*profile,*leaf,*top;static GeanyDocument *doc;static guint phase;static gint source_position;
static GtkWidget *popup(void) {GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-completion-popup") && gtk_widget_get_visible(p->data)) found=p->data;g_list_free(windows);return found;}
static GtkWidget *named(GtkWidget *widget,const gchar *name) {if(!g_strcmp0(gtk_widget_get_name(widget),name)) return widget;if(GTK_IS_MENU_ITEM(widget)) {GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget));if(menu) {GtkWidget *found=named(menu,name);if(found) return found;}}if(GTK_IS_CONTAINER(widget)) {GList *children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found;}return NULL;}
static gboolean step(gpointer unused) {
 (void)unused;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!phase) {
  GeanyDocument *other=document_open_file(leaf,FALSE,NULL,NULL);g_assert_nonnull(other);
  sci_set_text(other->editor->sci,"module leaf; logic data_input, data_output; endmodule\n");
  doc=document_open_file(top,FALSE,NULL,NULL);g_assert_nonnull(doc);
  gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);gchar *prefix=strstr(text,"data_");g_assert_nonnull(prefix);sci_set_current_position(doc->editor->sci,prefix-text+5,FALSE);g_free(text);gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
  keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_AUTOCOMPLETE);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {
  GeanyDocument *current=document_get_current();if(!current || g_strcmp0(current->file_name,leaf)) return G_SOURCE_CONTINUE;
  g_assert_cmpint(sci_get_current_position(current->editor->sci),==,19);keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_GOTO_BACK);phase=3;return G_SOURCE_CONTINUE;
 }
 if(phase==3) {g_assert_true(document_get_current()==doc);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,source_position);keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_GOTO_FORWARD);phase=4;return G_SOURCE_CONTINUE;}
 if(phase==4) {
  g_assert_cmpstr(document_get_current()->file_name,==,leaf);g_assert_cmpint(sci_get_current_position(document_get_current()->editor->sci),==,19);
  GtkWidget *action=named(gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci)),"insight-navigate-hdl-references");g_assert_nonnull(action);gtk_menu_item_activate(GTK_MENU_ITEM(action));phase=5;return G_SOURCE_CONTINUE;
 }
 if(phase==5) {
  GtkWidget *window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci)),*view=named(window,"insight-relations"),*status=named(window,"insight-relation-status");g_assert_nonnull(view);g_assert_nonnull(status);
  if(!g_str_has_prefix(gtk_label_get_text(GTK_LABEL(status)),"slang:")) return G_SOURCE_CONTINUE;
  GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),>=,1);GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter_first(model,&iter));gchar *name=NULL,*source=NULL,*relation=NULL;gtk_tree_model_get(model,&iter,7,&name,1,&source,8,&relation,-1);g_assert_cmpstr(name,==,"data_input");g_assert_cmpstr(source,==,top);g_assert_nonnull(strstr(relation,leaf));g_free(name);g_free(source);g_free(relation);
  GtkTreePath *row=gtk_tree_model_get_path(model,&iter);gtk_tree_view_row_activated(GTK_TREE_VIEW(view),row,gtk_tree_view_get_column(GTK_TREE_VIEW(view),0));gtk_tree_path_free(row);g_assert_true(document_get_current()==doc);
  document_set_text_changed(document_find_by_filename(leaf),FALSE);document_set_text_changed(doc,FALSE);
  g_print("HDL native GUI: completion, declaration history and semantic Relations panel with unsaved buffers passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 if(!popup()) return G_SOURCE_CONTINUE;
 GtkWidget *list=named(popup(),"insight-completion-list");g_assert_nonnull(list);GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(list));g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,2);
 GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter_first(model,&iter));do {gchar *name=NULL,*origin=NULL;gtk_tree_model_get(model,&iter,0,&name,5,&origin,-1);g_assert_true(g_str_has_prefix(name,"data_"));g_assert_nonnull(strstr(origin,"slang"));g_free(name);g_free(origin);}while(gtk_tree_model_iter_next(model,&iter));
 gchar *disk=NULL;g_assert_true(g_file_get_contents(leaf,&disk,NULL,NULL));g_assert_nonnull(strstr(disk,"old_signal"));g_free(disk);
 sci_set_text(doc->editor->sci,"module top; leaf u_leaf(); initial $display(u_leaf.data_input); endmodule\n");
 gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);source_position=strstr(text,"data_input")-text+strlen("data_input");g_free(text);sci_set_current_position(doc->editor->sci,source_position,FALSE);
 keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_GOTO_TAGDEFINITION);phase=2;return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer unused) {(void)unused;g_error("HDL completion GUI timed out at phase %u",phase);return G_SOURCE_REMOVE;}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 root=g_dir_make_tmp("insight-hdl-gui-XXXXXX",NULL);profile=g_build_filename(root,"profile",NULL);g_mkdir_with_parents(profile,0700);leaf=g_build_filename(root,"leaf.sv",NULL);top=g_build_filename(root,"top.sv",NULL);
 gchar *prefs=g_build_filename(profile,"geany.conf",NULL);g_file_set_contents(prefs,"[geany]\nconfirm_exit=false\nauto_complete_symbols=true\n",-1,NULL);g_free(prefs);prefs=g_build_filename(profile,"insight-completion.conf",NULL);g_file_set_contents(prefs,"[completion]\ndetailed=true\nsemantic=true\n",-1,NULL);g_free(prefs);
 g_file_set_contents(leaf,"module leaf; logic old_signal; endmodule\n",-1,NULL);g_file_set_contents(top,"module top; leaf u_leaf(); initial $display(u_leaf.data_); endmodule\n",-1,NULL);
 gchar *args[]={"hdl-completion","--new-instance","--no-session","--config",profile,top,NULL};g_timeout_add(200,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(6,args);
}
