/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*view,*query,*mode,*filter;static gchar *path;static guint phase;static gint caret;
static const gchar *source="class Root { public: int value; };\nclass Child : public Root {};\nint use() { Root r; return r.value; }\n";
static GtkWidget *named(GtkWidget *root,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static void click(const gchar *name) { GtkWidget *w=named(window,name);g_assert_nonnull(w);gtk_button_clicked(GTK_BUTTON(w)); }
static void toggle(const gchar *name,gboolean active) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,name)),active); }
static gboolean root_row(void) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter iter;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *name=NULL;gtk_tree_model_get(model,&iter,7,&name,-1);gboolean match=!g_strcmp0(name,"Root");g_free(name);if(match) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&iter);return TRUE; } }return FALSE;
}
static void geometry(gboolean vertical,gint zoom) {
 gtk_combo_box_set_active(GTK_COMBO_BOX(named(window,"insight-relation-graph-direction")),vertical ? 1 : 0);gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(window,"insight-relation-graph-zoom")),zoom);
}
static void check(const gchar *text,gint relation,const gchar *display_filter,gboolean graph,gboolean vertical,gint zoom) {
 g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(query)),==,text);g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(mode)),==,relation);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(filter)),==,display_filter);g_assert_cmpint(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(window,"insight-relation-graph-toggle"))),==,graph);g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(named(window,"insight-relation-graph-direction"))),==,vertical ? 1 : 0);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(named(window,"insight-relation-graph-zoom"))),==,zoom);g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(window,"insight-relations-follow"))));
}
static GtkWidget *history_menu(void) {
 GtkWidget *button=named(window,"insight-relation-history");GtkWidget *menu=GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(button)));g_assert_nonnull(menu);g_signal_emit_by_name(menu,"show");return menu;
}
static gboolean tick(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(path,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!view) { view=named(window,"insight-relations");query=named(window,"insight-relation-query");mode=named(window,"insight-relation-mode");filter=named(window,"insight-relation-filter");g_assert_nonnull(view); }
 if(phase==1) {
  caret=sci_get_current_position(doc->editor->sci);gtk_combo_box_set_active(GTK_COMBO_BOX(mode),4);gtk_entry_set_text(GTK_ENTRY(query),"Child");g_signal_emit_by_name(query,"activate");gtk_entry_set_text(GTK_ENTRY(filter),"Root");toggle("insight-relation-graph-toggle",TRUE);geometry(TRUE,125);phase++;
 }else if(phase==2) {
  if(!root_row()) return G_SOURCE_CONTINUE;click("insight-relation-definitions");phase++;
 }else if(phase==3) {
  if(!root_row()) return G_SOURCE_CONTINUE;check("Root",1,"",TRUE,TRUE,125);geometry(FALSE,75);toggle("insight-relation-graph-toggle",FALSE);gtk_entry_set_text(GTK_ENTRY(filter),"roo");click("insight-relation-definitions");phase++;
 }else if(phase==4) {
  if(!root_row()) return G_SOURCE_CONTINUE;gtk_entry_set_text(GTK_ENTRY(filter),"Root");geometry(TRUE,150);click("insight-relation-back");check("Root",1,"roo",FALSE,FALSE,75);g_assert_true(gtk_widget_get_sensitive(named(window,"insight-relation-forward")));click("insight-relation-forward");check("Root",1,"Root",FALSE,TRUE,150);click("insight-relation-back");check("Root",1,"roo",FALSE,FALSE,75);click("insight-relation-back");check("Child",4,"Root",TRUE,TRUE,125);phase++;
 }else if(phase==5) {
  GtkWidget *menu=history_menu(),*item=named(menu,"insight-relation-history-forward-2");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));check("Root",1,"Root",FALSE,TRUE,150);g_assert_false(gtk_widget_get_sensitive(named(window,"insight-relation-forward")));click("insight-relation-back");check("Root",1,"roo",FALSE,FALSE,75);phase++;
 }else if(phase==6) {
  if(!root_row()) return G_SOURCE_CONTINUE;click("insight-relation-definitions");g_assert_false(gtk_widget_get_sensitive(named(window,"insight-relation-forward")));phase++;
 }else if(phase==7) {
  if(!root_row()) return G_SOURCE_CONTINUE;
  /* Many drill-downs keep the current rows while workers are replaced. */
  for(guint i=0;i<36;i++) { g_assert_true(root_row());click("insight-relation-definitions"); }
  GtkWidget *menu=history_menu();g_assert_nonnull(named(menu,"insight-relation-history-back-32"));g_assert_null(named(menu,"insight-relation-history-back-33"));g_assert_null(named(menu,"insight-relation-history-forward-1"));GtkWidget *item=named(menu,"insight-relation-history-clear");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));g_assert_false(gtk_widget_get_sensitive(named(window,"insight-relation-back")));g_assert_false(gtk_widget_get_sensitive(named(window,"insight-relation-forward")));menu=history_menu();g_assert_false(gtk_widget_get_sensitive(named(menu,"insight-relation-history-clear")));
  g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);g_assert_cmpstr(text,==,source);g_free(text);g_assert_false(doc->changed);
  g_print("Native relation history: backward/forward, multi-step menu, state/graph geometry restore, forward-branch invalidation, 32-visit bound, clear and unchanged editor passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Relation history timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("insight-history-XXXXXX",NULL),*profile=g_build_filename(directory,"profile",NULL),*sd=g_build_filename(profile,"insight",NULL),*settings=g_build_filename(sd,"workbench.conf",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);path=g_build_filename(directory,"types.cpp",NULL);g_assert_true(g_file_set_contents(path,source,-1,NULL));g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nfollow=false\nrelation-follow=false\nproject-visible=true\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 gchar *argv[]={"history-gui","--new-instance","--no-session","--config",profile,NULL};guint step=g_timeout_add(250,tick,NULL),limit=g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);if(g_main_context_find_source_by_id(NULL,step)) g_source_remove(step);if(g_main_context_find_source_by_id(NULL,limit)) g_source_remove(limit);g_free(directory);g_free(profile);g_free(sd);g_free(settings);g_free(config);g_free(path);return result;
}
