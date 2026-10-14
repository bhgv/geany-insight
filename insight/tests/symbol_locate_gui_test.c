/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*view,*entry,*button;static gchar *profile,*path;static guint phase;static gint preview_line;
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!strcmp(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_ITEM(root)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
static gboolean row(GtkTreeModel *m,const gchar *name,GtkTreeIter *result) { GtkTreeIter it;for(gboolean v=gtk_tree_model_get_iter_first(m,&it);v;v=gtk_tree_model_iter_next(m,&it)) { gchar *n=NULL;gtk_tree_model_get(m,&it,0,&n,-1);gboolean match=!g_strcmp0(n,name);g_free(n);if(match) { *result=it;return TRUE; }GtkTreeIter child;if(gtk_tree_model_iter_children(m,&child,&it)) do { gchar *n=NULL;gtk_tree_model_get(m,&child,0,&n,-1);gboolean match=!g_strcmp0(n,name);g_free(n);if(match) { *result=child;return TRUE; } }while(gtk_tree_model_iter_next(m,&child)); }return FALSE; }
static void selected(const gchar *expected) { GtkTreeIter it;GtkTreeSelection *selection=gtk_tree_view_get_selection(GTK_TREE_VIEW(view));g_assert_true(gtk_tree_selection_get_selected(selection,NULL,&it));gchar *name=NULL;gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),&it,0,&name,-1);g_assert_cmpstr(name,==,expected);g_free(name); }
static GtkWidget *popup(GtkWidget *target) { gtk_widget_grab_focus(target);GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(window));event->key.keyval=GDK_KEY_Menu;gdk_event_set_device(event,gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default())));gtk_main_do_event(event);gdk_event_free(event);GtkWidget *menu=g_object_get_data(G_OBJECT(target),"insight-symbol-popup");g_assert_nonnull(menu);gtk_menu_popdown(GTK_MENU(menu));return menu; }
static void locate(GeanyDocument *doc,gint line,const gchar *expected) { sci_set_current_position(doc->editor->sci,sci_get_position_from_line(doc->editor->sci,line),FALSE);gint caret=sci_get_current_position(doc->editor->sci);gtk_button_clicked(GTK_BUTTON(button));selected(expected);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);g_assert_cmpint(sci_get_current_line(SCINTILLA(named(window,"insight-context-code"))),==,preview_line); }
static gboolean timeout(gpointer data) { (void)data;g_error("Symbol locate GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(path,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!view) { view=named(window,"insight-local-symbols");entry=named(window,"insight-local-query");button=named(window,"insight-local-locate"); }
 if(!gtk_widget_get_sensitive(view)) return G_SOURCE_CONTINUE;
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter it;
 if(phase==1) {
  if(!row(model,"beta",&it)) return G_SOURCE_CONTINUE;
  g_assert_null(gtk_button_get_label(GTK_BUTTON(button)));g_assert_true(GTK_IS_IMAGE(gtk_button_get_image(GTK_BUTTON(button))));gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&it);phase++;
 } else if(phase==2) {
  preview_line=sci_get_current_line(SCINTILLA(named(window,"insight-context-code")));locate(doc,3,"alpha");locate(doc,5,"alpha");locate(doc,7,"last_variable");locate(doc,0,"last_variable");
  GtkWidget *menu=popup(view),*command=g_object_get_data(G_OBJECT(menu),"insight-symbol-command-13");g_assert_true(gtk_widget_get_visible(command));g_assert_true(gtk_widget_get_sensitive(command));sci_set_current_position(doc->editor->sci,sci_get_position_from_line(doc->editor->sci,6),FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(command));selected("beta");g_assert_cmpint(sci_get_current_line(SCINTILLA(named(window,"insight-context-code"))),==,preview_line);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-local-group-types")),TRUE);phase++;
 } else if(phase==3) {
  if(!GTK_IS_TREE_STORE(model) || !row(model,"Functions",&it)) return G_SOURCE_CONTINUE;
  GtkTreePath *group=gtk_tree_model_get_path(model,&it);gtk_tree_view_collapse_row(GTK_TREE_VIEW(view),group);locate(doc,3,"alpha");g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),group));gtk_tree_path_free(group);gtk_entry_set_text(GTK_ENTRY(entry),"beta");phase++;
 } else if(phase==4) {
  if(!row(model,"beta",&it) || row(model,"alpha",&it)) return G_SOURCE_CONTINUE;
  gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&it);preview_line=sci_get_current_line(SCINTILLA(named(window,"insight-context-code")));locate(doc,3,"beta");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)),==,"beta");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);phase++;
 } else {
  gchar *tip=gtk_widget_get_tooltip_text(button);g_assert_cmpstr(tip,==,"Найти символ у курсора");g_free(tip);GtkWidget *menu=popup(named(window,"insight-project-symbols")),*command=g_object_get_data(G_OBJECT(menu),"insight-symbol-command-13");g_assert_false(gtk_widget_get_visible(command));
  g_print("Symbol locate GUI passed: containing brace function, nearest preceding declaration, no earlier tag, toolbar and menu, grouped expansion, hidden result preserves filter/selection, editor and Context unchanged, RU tooltip and local-only menu\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("symbol-locate-gui-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL);g_free(cwd);g_free(name);profile=g_build_filename(directory,"profile",NULL);path=g_build_filename(directory,"sample.c",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);
 g_assert_true(g_file_set_contents(path,"// before declarations\nint alpha(void) {\n  // function body\n  int local_value = 1;\n  return local_value;\n}\nint beta(void) { return 2; }\nint last_variable;\n",-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"symbol-locate-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(200,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);g_free(path);return result;
}
