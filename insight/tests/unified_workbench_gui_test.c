/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "documentprivate.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "search.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "sidebar.h"
#include <glib/gstdio.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
static GtkWidget *window;static gchar *path,*profile;static guint phase;
static const gchar *source="// needle first\nclass Box { public: int value; int run() { return value; } };\nint main() { Box box; return box.run(); }\n// needle last\n";
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_ITEM(root)) { GtkWidget *submenu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(submenu) { GtkWidget *found=named(submenu,name);if(found) return found; } }if(GTK_IS_MENU_BUTTON(root)) { GtkWidget *popover=GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(root)));if(popover) { GtkWidget *found=named(popover,name);if(found) return found; } }if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static gboolean find_tag(GtkTreeModel *model,GtkTreeIter *parent,const gchar *name,guint *depth,guint level) { GtkTreeIter iter;for(gboolean ok=gtk_tree_model_iter_children(model,&iter,parent);ok;ok=gtk_tree_model_iter_next(model,&iter)) { TMTag *tag=NULL;gtk_tree_model_get(model,&iter,SYMBOLS_COLUMN_TAG,&tag,-1);gboolean match=tag && !g_strcmp0(tag->name,name);if(tag) g_boxed_free(gtk_tree_model_get_column_type(model,SYMBOLS_COLUMN_TAG),tag);if(match) { *depth=level;return TRUE; }if(find_tag(model,&iter,name,depth,level+1)) return TRUE; }return FALSE; }
static gboolean timeout(gpointer data) { (void)data;g_error("Unified workbench timeout, phase %u",phase);return G_SOURCE_REMOVE; }
static void toggle(const gchar *name,gboolean active) { GtkWidget *widget=named(window,name);g_assert_nonnull(widget);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),active); }
static guint dialog_visit;
static gboolean command_dialog_step(gpointer data) {
 (void)data;GList *tops=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=tops;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),dialog_visit==1 || dialog_visit==3 ? "insight-lookup-references-dialog" : "insight-search-project-dialog")) { dialog=p->data;break; }g_list_free(tops);g_assert_nonnull(dialog);
 GtkWidget *method=named(dialog,"insight-search-command-method"),*query=named(dialog,"insight-search-command-query"),*scope=named(dialog,"insight-search-command-scope"),*sensitive=named(dialog,"insight-search-command-case");
 if(dialog_visit<2) {
  g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(method)),==,dialog_visit ? 3 : 2);g_assert_cmpint(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sensitive)),==,dialog_visit!=0);
  gtk_entry_set_text(GTK_ENTRY(query),dialog_visit ? "Box" : "needle");gtk_combo_box_set_active(GTK_COMBO_BOX(scope),2);if(!dialog_visit) { gtk_combo_box_set_active(GTK_COMBO_BOX(method),0);gtk_combo_box_set_active(GTK_COMBO_BOX(named(dialog,"insight-search-command-comments")),2); }
  const gchar *capture=g_getenv("INSIGHT_SEARCH_DIALOG_CAPTURE");if(capture && !dialog_visit) { GdkWindow *native=gtk_widget_get_window(dialog);GdkPixbuf *image=gdk_pixbuf_get_from_window(native,0,0,gdk_window_get_width(native),gdk_window_get_height(native));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,capture,"jpeg",NULL,"quality","90",NULL));g_object_unref(image); }
 } else { g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(method)),==,dialog_visit==3 ? 3 : 0);g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(scope)),==,2);g_assert_cmpint(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sensitive)),==,dialog_visit==3);if(dialog_visit==2) g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(query)),==,"needle"); }
 gtk_dialog_response(GTK_DIALOG(dialog),dialog_visit<2 ? GTK_RESPONSE_ACCEPT : GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
}
static void invoke_command(guint visit) { dialog_visit=visit;g_timeout_add(150,command_dialog_step,NULL);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,visit==1 || visit==3 ? "insight-lookup-references-command" : "insight-search-project-command"))); }
static void graph_window_key(guint keyval) {
 GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(window));event->key.keyval=keyval;event->key.state=GDK_CONTROL_MASK;gboolean handled=FALSE;g_signal_emit_by_name(window,"key-press-event",&event->key,&handled);g_assert_true(handled);gdk_event_free(event);
}
static void graph_shortcuts(GeanyDocument *doc) {
 GtkWidget *canvas=named(window,"insight-relation-graph"),*zoom=named(window,"insight-relation-graph-zoom");g_assert_nonnull(canvas);g_assert_nonnull(zoom);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-relation-graph-toggle")),TRUE);
 for(GtkWidget *child=canvas;child;child=gtk_widget_get_parent(child)) { GtkWidget *parent=gtk_widget_get_parent(child);if(parent && GTK_IS_NOTEBOOK(parent)) gtk_notebook_set_current_page(GTK_NOTEBOOK(parent),gtk_notebook_page_num(GTK_NOTEBOOK(parent),child));gtk_widget_show(child); }
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(zoom),100);gtk_widget_grab_focus(canvas);g_assert_true(gtk_widget_has_focus(canvas));gint editor_zoom=(gint)scintilla_send_message(doc->editor->sci,SCI_GETZOOM,0,0);
 graph_window_key(GDK_KEY_minus);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(zoom)),==,90);g_assert_cmpint(scintilla_send_message(doc->editor->sci,SCI_GETZOOM,0,0),==,editor_zoom);
 graph_window_key(GDK_KEY_0);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(zoom)),==,100);graph_window_key(GDK_KEY_plus);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(zoom)),==,110);g_assert_cmpint(scintilla_send_message(doc->editor->sci,SCI_GETZOOM,0,0),==,editor_zoom);
 graph_window_key(GDK_KEY_c);GdkPixbuf *copy=gtk_clipboard_wait_for_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(copy);g_object_unref(copy);
}
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { gtk_window_resize(GTK_WINDOW(window),1280,900);gtk_widget_hide(named(window,"insight-project-notebook"));document_open_file(path,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 GtkWidget *tree=named(window,"insight-file-structure"),*query=named(window,"insight-file-search-query"),*mode=named(window,"insight-file-symbol-mode");
 if(!tree || !mode || !doc->has_tags) return G_SOURCE_CONTINUE;
 if(phase==1) {
  guint box_depth=0,run_depth=0;GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));g_assert_true(find_tag(model,NULL,"Box",&box_depth,0));g_assert_true(find_tag(model,NULL,"run",&run_depth,0));g_assert_cmpuint(run_depth,>,box_depth);
  keybindings_send_command(GEANY_KEY_GROUP_FOCUS,GEANY_KEYS_FOCUS_SIDEBAR_SYMBOL_LIST);g_assert_true(gtk_widget_has_focus(tree));g_assert_true(gtk_widget_get_mapped(tree));gtk_paned_set_position(GTK_PANED(ui_lookup_widget(window,"hpaned1")),240);g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(mode)),==,0);g_assert_false(gtk_widget_get_visible(ui_lookup_widget(window,"entry_tagfilter")));g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(ui_lookup_widget(window,"notebook3"))),==,2);
  sci_set_current_position(doc->editor->sci,0,FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"find1")));g_assert_true(gtk_widget_get_visible(named(window,"insight-editor-search-panel")));gtk_entry_set_text(GTK_ENTRY(query),"needle");gtk_button_clicked(GTK_BUTTON(named(window,"insight-file-search-next")));g_assert_cmpint(sci_get_selection_start(doc->editor->sci),==,3);
  gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"find_next1")));gint last=(gint)(strrchr(source,'/')-source)+2;g_assert_cmpint(sci_get_selection_start(doc->editor->sci),==,last);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"find_previous1")));g_assert_cmpint(sci_get_selection_start(doc->editor->sci),==,3);
  toggle("insight-file-search-wrap",FALSE);sci_set_current_position(doc->editor->sci,sci_get_length(doc->editor->sci),FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"find_next1")));g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,sci_get_length(doc->editor->sci));
  toggle("insight-file-search-wrap",TRUE);toggle("insight-file-search-select",FALSE);sci_set_current_position(doc->editor->sci,0,FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"find_next1")));g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,3);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"find_next1")));g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,last);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"find_previous1")));g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,3);
  toggle("insight-file-search-select",TRUE);sci_set_current_position(doc->editor->sci,0,FALSE);toggle("insight-file-search-incremental",TRUE);gtk_entry_set_text(GTK_ENTRY(query),"Bo");g_assert_cmpint(sci_get_selection_start(doc->editor->sci),==,(gint)(strstr(source,"Box")-source));gtk_entry_set_text(GTK_ENTRY(query),"Box");g_assert_cmpint(sci_get_selection_start(doc->editor->sci),==,(gint)(strstr(source,"Box")-source));toggle("insight-file-search-incremental",FALSE);
  gtk_combo_box_set_active(GTK_COMBO_BOX(mode),1);g_assert_cmpstr(gtk_stack_get_visible_child_name(GTK_STACK(named(window,"insight-file-symbol-stack"))),==,"details");gtk_combo_box_set_active(GTK_COMBO_BOX(mode),0);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"run");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) { guint depth=0;g_assert_true(find_tag(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),NULL,"run",&depth,0));gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"");gtk_tree_view_expand_all(GTK_TREE_VIEW(tree));gtk_entry_set_text(GTK_ENTRY(query),"needle");gtk_button_clicked(GTK_BUTTON(named(window,"insight-file-search-next")));g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(named(window,"insight-file-search-history"))),NULL),==,3);phase++;return G_SOURCE_CONTINUE; }
 if(phase==3) {
  const gchar *capture=g_getenv("INSIGHT_UNIFIED_CAPTURE");if(capture) { GdkWindow *native=gtk_widget_get_window(window);GdkPixbuf *image=gdk_pixbuf_get_from_window(native,0,0,gdk_window_get_width(native),gdk_window_get_height(native));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,capture,"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  phase++;invoke_command(0);return G_SOURCE_CONTINUE;
 }
 if(phase==4 || phase==5) { GtkTreeModel *results=gtk_tree_view_get_model(GTK_TREE_VIEW(named(window,"insight-search-results")));if(gtk_tree_model_iter_n_children(results,NULL)!=2) return G_SOURCE_CONTINUE;GtkTreeIter first_iter;gulong first_line=0;gtk_tree_model_get_iter_first(results,&first_iter);gtk_tree_model_get(results,&first_iter,2,&first_line,-1);if(first_line!=(phase==4 ? 1 : 2)) return G_SOURCE_CONTINUE;if(phase==4) { phase++;invoke_command(1);return G_SOURCE_CONTINUE; }invoke_command(2);invoke_command(3);
  GtkTreePath *first=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_row_activated(GTK_TREE_VIEW(named(window,"insight-search-results")),first,gtk_tree_view_get_column(GTK_TREE_VIEW(named(window,"insight-search-results")),0));gtk_tree_path_free(first);g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,1);
  graph_shortcuts(doc);gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,source);g_free(text);g_assert_false(doc->changed);g_print("Unified File Search and File Symbols: native hierarchy, one page/query, F3 directions, wrap, caret-only, incremental, details mode, independent persistent search dialogs, result navigation and graph window shortcut routing passed.\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("unified-workbench-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL);g_free(cwd);g_free(name);profile=g_build_filename(directory,"profile",NULL);path=g_build_filename(directory,"sample.cpp",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);g_assert_true(g_file_set_contents(path,source,-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);gchar *argv[]={"unified-workbench","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);g_free(path);return result;
}
