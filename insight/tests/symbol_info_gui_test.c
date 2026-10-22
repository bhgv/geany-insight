/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "../desktop/layout_state.h"
#include <glib/gstdio.h>
#include <string.h>

#undef SSM
#define SSM(sci,msg,w,l) scintilla_send_message((sci),(msg),(w),(l))
static guint phase,primary_id,untitled_id;
static gchar *profile,*project,*primary,*external;
static GtkWidget *window;
static GtkWidget *named(GtkWidget *root,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_MENU_ITEM(root)) { GtkWidget *submenu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(submenu) { GtkWidget *found=named(submenu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }
 return NULL;
}
static GtkWidget *button(GtkWidget *root,const gchar *label) {
 if(GTK_IS_BUTTON(root) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(root)),label)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=button(p->data,label);g_list_free(children);return found; }return NULL;
}
static GtkWidget *toplevel(const gchar *name,gboolean by_title) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(by_title ? gtk_window_get_title(GTK_WINDOW(p->data)) : gtk_widget_get_name(p->data),name)) { found=p->data;break; }
 g_list_free(windows);return found;
}
static void click(GtkWidget *root,const gchar *label) { GtkWidget *w=button(root,label);g_assert_nonnull(w);g_assert_true(gtk_widget_get_sensitive(w));g_signal_emit_by_name(w,"clicked"); }
static GtkWidget *info_window(void) { GtkWidget *w=toplevel("insight-symbol-info-window",FALSE);g_assert_nonnull(w);return w; }
static void show_info(GeanyDocument *doc,const gchar *word) {
 gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1),*at=strstr(text,word);g_assert_nonnull(at);SSM(doc->editor->sci,SCI_GOTOPOS,at-text+1,0);g_free(text);
 GtkWidget *command=named(window,"insight-symbol-info-command");g_assert_nonnull(command);gtk_menu_item_activate(GTK_MENU_ITEM(command));
}
static gchar *details(GtkWidget *info) { GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(info,"insight-symbol-info-details")));GtkTextIter first,last;gtk_text_buffer_get_bounds(buffer,&first,&last);return gtk_text_buffer_get_text(buffer,&first,&last,FALSE); }
static void nominal_sizes(void) {
 const gchar *code="struct MetricStruct {\n const char *value = \"}\";\n};\nclass MetricClass {\npublic:\n void method() {}\n};\nunion MetricUnion { int x; double y; };\nenum class MetricEnum {\n One, Two\n};\nnamespace MetricSpace {\n struct Item {};\n}\nclass ForwardOnly;\nstruct SameFirst {}; struct SameSecond {};\n";
 GeanyDocument *doc=document_new_file(NULL,filetypes_lookup_by_name("C++"),code);g_assert_nonnull(doc);
 /* New documents adopt the platform EOL before Symbol Info is opened.
  * Verify that the inspected document preserves its actual initialized bytes. */
 gchar *initial=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);
 const gchar *names[]={"MetricStruct","MetricClass","MetricUnion","MetricEnum","MetricSpace"};const guint sizes[]={3,4,1,3,3};
 for(guint i=0;i<G_N_ELEMENTS(names);i++) {
  show_info(doc,names[i]);GtkWidget *info=info_window();gchar *metadata=details(info),*expected=g_strdup_printf("Definition size (lines): %u",sizes[i]);g_test_message("%s",metadata);g_assert_nonnull(strstr(metadata,expected));g_free(expected);g_free(metadata);
  if(!i) { gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);click(info,"Обновить");metadata=details(info);g_assert_nonnull(strstr(metadata,"Размер определения (строк): 3"));g_free(metadata);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);click(info,"Refresh"); }
  g_assert_false(SSM(doc->editor->sci,SCI_CANUNDO,0,0));click(info,"Close");
 }
 gchar *unchanged=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);g_assert_cmpstr(unchanged,==,initial);g_free(unchanged);g_free(initial);
 const gchar *unknown[]={"ForwardOnly","SameSecond"};for(guint i=0;i<G_N_ELEMENTS(unknown);i++) { show_info(doc,unknown[i]);GtkWidget *info=info_window();gchar *metadata=details(info);g_assert_null(strstr(metadata,"Definition size (lines):"));g_free(metadata);click(info,"Close"); }
 document_set_text_changed(doc,FALSE);g_assert_true(document_close(doc));
 doc=document_new_file(NULL,filetypes_lookup_by_name("Java"),"public interface MetricInterface {\n void call();\n}\n");g_assert_nonnull(doc);show_info(doc,"MetricInterface");GtkWidget *java_info=info_window();gchar *java_metadata=details(java_info);g_test_message("%s",java_metadata);g_assert_nonnull(strstr(java_metadata,"Definition size (lines): 3"));g_free(java_metadata);click(java_info,"Close");document_set_text_changed(doc,FALSE);g_assert_true(document_close(doc));
 doc=document_new_file(NULL,filetypes_lookup_by_name("Python"),"class MetricPython:\n    \"\"\"body\"\"\"\n    def method(self):\n        return 1\n\nnext_value = 2\n");g_assert_nonnull(doc);show_info(doc,"MetricPython");GtkWidget *info=info_window();gchar *metadata=details(info);g_test_message("%s",metadata);g_assert_nonnull(strstr(metadata,"Definition size (lines): 4"));g_free(metadata);click(info,"Close");document_set_text_changed(doc,FALSE);g_assert_true(document_close(doc));
}
static gboolean cancel_dialog(gpointer data) {
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(GTK_IS_MESSAGE_DIALOG(p->data) || GTK_IS_FILE_CHOOSER(p->data)) { dialog=p->data;break; }
 g_list_free(windows);if(!dialog) return G_SOURCE_CONTINUE;gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
static gboolean select_doc(GtkWidget *view,guint id) {
 gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)));
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter iter;gboolean valid=gtk_tree_model_get_iter_first(model,&iter);
 while(valid) { guint row=0;gtk_tree_model_get(model,&iter,0,&row,-1);if(row==id) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&iter);return TRUE; }valid=gtk_tree_model_iter_next(model,&iter); }return FALSE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Symbol Info GUI timed out at phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));if(!window) return G_SOURCE_CONTINUE;
 if(phase==0) {
  /* main_lib's Windows CLI path accepts CRT locale arguments, whereas this
   * fixture is UTF-8. Open through the UTF-8-aware document API on both hosts. */
  doc=document_open_file(primary,FALSE,filetypes_lookup_by_name("C"),NULL);g_assert_nonnull(doc);
  primary_id=doc->id;show_info(doc,"helper");GtkWidget *info=info_window(),*code=named(info,"insight-symbol-info-code");g_assert_nonnull(code);gchar *metadata=details(info);g_assert_nonnull(strstr(metadata,"Function size (lines): 1"));g_free(metadata);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);click(info,"Обновить");metadata=details(info);g_assert_nonnull(strstr(metadata,"Размер функции (строк): 1"));g_free(metadata);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);click(info,"Refresh");
  gchar *text=sci_get_contents(SCINTILLA(code),sci_get_length(SCINTILLA(code))+1);g_assert_nonnull(strstr(text,"int helper"));g_free(text);g_assert_true(SSM(SCINTILLA(code),SCI_GETREADONLY,0,0));g_assert_cmpuint(document_get_current()->id,==,primary_id);phase++;
 } else if(phase==1) {
  GtkWidget *info=info_window();gtk_entry_set_text(GTK_ENTRY(named(info,"insight-symbol-info-query")),"external_symbol");click(info,"Refresh");
  GtkComboBox *choice=GTK_COMBO_BOX(named(info,"insight-symbol-info-candidates"));GtkTreeModel *model=gtk_combo_box_get_model(choice);GtkTreeIter iter;gboolean valid=gtk_tree_model_get_iter_first(model,&iter);gint index=0,found=-1,declaration=-1;
  while(valid) { gchar *path=NULL;gtk_tree_model_get(model,&iter,1,&path,-1);if(!g_strcmp0(path,external)) found=index;if(!g_strcmp0(path,primary)) declaration=index;g_free(path);index++;valid=gtk_tree_model_iter_next(model,&iter); }
  if(found<0) return G_SOURCE_CONTINUE;
  g_assert_cmpint(declaration,>=,0);gtk_combo_box_set_active(choice,declaration);gchar *metadata=details(info);g_assert_null(strstr(metadata,"Function size (lines):"));g_free(metadata);gtk_combo_box_set_active(choice,found);click(info,"Refresh");metadata=details(info);g_assert_nonnull(strstr(metadata,external));g_assert_nonnull(strstr(metadata,"Function size (lines): 1"));g_free(metadata);g_assert_null(document_find_by_filename(external));click(info,"Leave file open");g_assert_nonnull(document_find_by_filename(external));g_assert_cmpuint(document_get_current()->id,==,primary_id);g_assert_false(gtk_widget_get_sensitive(button(info,"Leave file open")));gtk_entry_set_text(GTK_ENTRY(named(info,"insight-symbol-info-query")),"alias_object");click(info,"Refresh");metadata=details(info);g_assert_nonnull(strstr(metadata,"Nominal type: InfoCell (alias hops: 1)"));g_assert_null(strstr(metadata,"Function size (lines):"));g_free(metadata);click(info,"Close");phase++;
 } else if(phase==2) {
  nominal_sizes();
  doc=document_new_file(NULL,filetypes_lookup_by_name("C"),"int ephemeral(int n) {\n return n + 1;\n}\n");g_assert_nonnull(doc);untitled_id=doc->id;show_info(doc,"ephemeral");
  GtkWidget *info=info_window();GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(info,"insight-symbol-info-details")));GtkTextIter first,last;gtk_text_buffer_get_bounds(buffer,&first,&last);gchar *text=gtk_text_buffer_get_text(buffer,&first,&last,FALSE);g_assert_nonnull(strstr(text,"untitled:"));g_assert_nonnull(strstr(text,"Function size (lines): 3"));g_free(text);g_assert_false(gtk_widget_get_sensitive(button(info,"Leave file open")));
  SSM(doc->editor->sci,SCI_APPENDTEXT,11,(sptr_t)"// changed\n");document_set_text_changed(doc,TRUE);gint position=sci_get_current_position(doc->editor->sci);click(info,"Jump");g_assert_true(gtk_widget_get_visible(info));g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,position);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(info,"insight-symbol-info-status"))),"Source changed"));
  click(info,"Refresh");click(info,"Jump");g_assert_null(toplevel("insight-symbol-info-window",FALSE));
  gchar *folder=g_path_get_dirname(primary),*returns_path=g_build_filename(folder,"template-returns.cpp",NULL);g_free(folder);g_assert_true(g_file_set_contents(returns_path,"struct T { int field; };\ntemplate<class T> T *info_dependent(T *p);\ntemplate<class T> ::T *info_global(T *p);\nT *info_ordinary(int T);\n",-1,NULL));
  GeanyDocument *returns=document_open_file(returns_path,FALSE,filetypes_lookup_by_name("C++"),NULL);g_assert_nonnull(returns);g_free(returns_path);show_info(returns,"info_dependent");info=info_window();gchar *metadata=details(info);g_assert_null(strstr(metadata,"Nominal type:"));g_free(metadata);
  const gchar *results[]={"info_global","info_ordinary"};for(guint ri=0;ri<G_N_ELEMENTS(results);ri++) { gtk_entry_set_text(GTK_ENTRY(named(info,"insight-symbol-info-query")),results[ri]);click(info,"Refresh");metadata=details(info);g_assert_nonnull(strstr(metadata,"Nominal type: T (alias hops: 0)"));g_free(metadata); }
  click(info,"Close");g_assert_true(document_close(returns));phase++;
 } else if(phase==3) {
  GtkWidget *menu=named(window,"insight-window-menu");g_assert_nonnull(menu);g_signal_emit_by_name(menu,"show");GtkWidget *command=named(window,"insight-window-list-command");g_assert_nonnull(command);gtk_menu_item_activate(GTK_MENU_ITEM(command));GtkWidget *list=toplevel("Window List",TRUE);g_assert_nonnull(list);
  GtkWidget *view=named(list,"insight-window-list");g_assert_nonnull(view);
  gint initial_column=-1,default_width=0,default_height=0,x=0,y=0; GtkSortType initial_direction; gtk_window_get_default_size(GTK_WINDOW(list),&default_width,&default_height); g_assert_cmpint(default_width,==,740); g_assert_cmpint(default_height,==,390); g_assert_true(gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(view))),&initial_column,&initial_direction)); g_assert_cmpint(initial_column,==,2); g_assert_cmpint(initial_direction,==,GTK_SORT_ASCENDING); g_assert_cmpint(gtk_tree_view_column_get_fixed_width(gtk_tree_view_get_column(GTK_TREE_VIEW(view),1)),==,180);
  gtk_window_get_position(GTK_WINDOW(list),&x,&y); GdkMonitor *monitor=gdk_display_get_monitor_at_point(gtk_widget_get_display(list),x,y); g_assert_nonnull(monitor); GdkRectangle area; gdk_monitor_get_workarea(monitor,&area); g_assert_cmpint(x,>=,area.x); g_assert_cmpint(y,>=,area.y); g_assert_cmpint(x,<,area.x+area.width); g_assert_cmpint(y,<,area.y+area.height);
  g_assert_true(select_doc(view,primary_id));g_assert_cmpuint(document_get_current()->id,==,untitled_id);
  GtkWidget *context=named(window,"insight-context-code");g_assert_nonnull(context);gchar *text=sci_get_contents(SCINTILLA(context),sci_get_length(SCINTILLA(context))+1);g_assert_nonnull(strstr(text,"int helper"));g_free(text);
  g_assert_true(select_doc(view,untitled_id));g_timeout_add(20,cancel_dialog,NULL);click(list,"Close file");g_assert_nonnull(document_find_by_id(untitled_id));g_timeout_add(20,cancel_dialog,NULL);click(list,"Save");g_assert_null(document_find_by_id(untitled_id)->file_name);
  g_assert_true(select_doc(view,primary_id));click(list,"Activate");g_assert_cmpuint(document_get_current()->id,==,primary_id);
  gtk_entry_set_text(GTK_ENTRY(named(list,"insight-window-filter")),"~hlpr");g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,1);gtk_window_resize(GTK_WINDOW(list),760,450); GtkTreeViewColumn *file_column=gtk_tree_view_get_column(GTK_TREE_VIEW(view),1),*path_column=gtk_tree_view_get_column(GTK_TREE_VIEW(view),5); gtk_tree_view_column_set_fixed_width(file_column,210); gtk_tree_view_move_column_after(GTK_TREE_VIEW(view),path_column,NULL); gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(view))),1,GTK_SORT_DESCENDING); phase++;
 } else if(phase==4) {
  GtkWidget *list=toplevel("Window List",TRUE); g_assert_nonnull(list); gtk_widget_destroy(list); GtkWidget *command=named(window,"insight-window-list-command"); gtk_menu_item_activate(GTK_MENU_ITEM(command)); list=toplevel("Window List",TRUE); g_assert_nonnull(list); GtkWidget *view=named(list,"insight-window-list");
  g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(list,"insight-window-filter"))),==,"~hlpr"); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,1);
  gint sort=-1; GtkSortType direction; g_assert_true(gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(view))),&sort,&direction)); g_assert_cmpint(sort,==,1); g_assert_cmpint(direction,==,GTK_SORT_DESCENDING);
  GtkTreeViewColumn *first=gtk_tree_view_get_column(GTK_TREE_VIEW(view),0); g_assert_cmpint(gtk_tree_view_column_get_sort_column_id(first),==,2); g_assert_true(gtk_tree_view_column_get_reorderable(first));
  GtkTreeViewColumn *file=gtk_tree_view_get_column(GTK_TREE_VIEW(view),2); g_assert_cmpint(gtk_tree_view_column_get_fixed_width(file),==,210);
  GdkEvent *event=gdk_event_new(GDK_DELETE); gboolean handled=FALSE; g_signal_emit_by_name(list,"delete-event",event,&handled); gdk_event_free(event); g_assert_true(handled); g_assert_false(gtk_widget_get_visible(list)); gtk_menu_item_activate(GTK_MENU_ITEM(command)); g_assert_true(gtk_widget_get_visible(list));
  GtkWidget *ru=named(window,"insight-interface-ru"),*en=named(window,"insight-interface-en"); g_assert_nonnull(ru); g_assert_nonnull(en); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ru),TRUE); GtkTreeIter first_row; GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view)); g_assert_true(gtk_tree_model_get_iter_first(model,&first_row)); gchar *state=NULL; gtk_tree_model_get(model,&first_row,3,&state,-1); g_assert_cmpstr(state,==,"Сохранён"); g_free(state); g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(list,"insight-window-filter"))),==,"~hlpr"); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(en),TRUE); g_assert_true(gtk_tree_model_get_iter_first(model,&first_row)); gtk_tree_model_get(model,&first_row,3,&state,-1); g_assert_cmpstr(state,==,"Saved"); g_free(state);
  document_set_text_changed(document_find_by_id(untitled_id),FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(list,"insight-window-list-dock-toggle")),TRUE); phase++;
 } else if(phase==5) {
  GtkWidget *dock=named(window,"insight-window-list-dock"),*list=toplevel("Window List",TRUE);g_assert_nonnull(dock);g_assert_true(gtk_widget_get_mapped(dock));g_assert_false(gtk_widget_get_visible(list));
  GtkWidget *view=named(dock,"insight-window-list"),*search=named(dock,"insight-window-filter");g_assert_nonnull(view);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(search)),==,"~hlpr");
  gint sort=-1;GtkSortType direction;gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(view))),&sort,&direction);g_assert_cmpint(sort,==,1);g_assert_cmpint(direction,==,GTK_SORT_DESCENDING);
  g_assert_cmpuint(document_get_current()->id,==,primary_id);
  GtkNotebook *sidebar=GTK_NOTEBOOK(gtk_widget_get_ancestor(dock,GTK_TYPE_NOTEBOOK));g_assert_nonnull(sidebar);gint dock_index=gtk_notebook_page_num(sidebar,dock);gtk_notebook_set_current_page(sidebar,0);document_set_text_changed(document_find_by_id(primary_id),TRUE);gtk_notebook_set_current_page(sidebar,dock_index);
  GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter_first(model,&iter));gchar *state=NULL;gtk_tree_model_get(model,&iter,3,&state,-1);g_assert_cmpstr(state,==,"Modified");g_free(state);document_set_text_changed(document_find_by_id(primary_id),FALSE);
  GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(search));event->key.keyval=GDK_KEY_Escape;gboolean handled=FALSE;g_signal_emit_by_name(search,"key-press-event",event,&handled);gdk_event_free(event);
  g_assert_true(handled);g_assert_false(gtk_widget_get_visible(dock));gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-window-list-command")));g_assert_true(gtk_widget_get_visible(dock));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dock,"insight-window-list-dock-toggle")),FALSE);g_assert_true(gtk_widget_get_visible(list));g_assert_false(gtk_widget_get_visible(dock));g_assert_true(named(list,"insight-window-list")==view);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(list,"insight-window-filter"))),==,"~hlpr");phase++;
 } else if(phase==6) {
  GtkWidget *list=toplevel("Window List",TRUE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(list,"insight-window-list-dock-toggle")),TRUE);gtk_widget_destroy(list);
  gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-window-list-command")));phase++;
 } else if(phase==7) {
  GtkWidget *dock=named(window,"insight-window-list-dock"),*list=toplevel("Window List",TRUE);g_assert_nonnull(dock);g_assert_true(gtk_widget_get_mapped(dock));g_assert_false(gtk_widget_get_visible(list));
  g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(dock,"insight-window-filter"))),==,"~hlpr");g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(dock,"insight-window-list-dock-toggle"))));
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(named(dock,"insight-window-list-dock-toggle"))),==,"Закрепить в боковой панели");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);
  gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-layout-save-0")));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dock,"insight-window-list-dock-toggle")),FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-layout-save-1")));
  GdkEvent *event=gdk_event_new(GDK_DELETE);gboolean handled=FALSE;g_signal_emit_by_name(list,"delete-event",event,&handled);gdk_event_free(event);g_assert_true(handled);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-layout-save-2")));
  gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-layout-load-0")));g_assert_true(gtk_widget_get_visible(dock));g_assert_false(gtk_widget_get_visible(list));
  gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-layout-load-2")));g_assert_false(gtk_widget_get_visible(dock));g_assert_false(gtk_widget_get_visible(list));
  gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-layout-load-1")));g_assert_false(gtk_widget_get_visible(dock));g_assert_true(gtk_widget_get_visible(list));g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(list,"insight-window-filter"))),==,"~hlpr");
  gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-layout-load-0")));phase++;
 } else if(phase==8) {
  GtkWidget *dock=named(window,"insight-window-list-dock");g_assert_true(gtk_widget_get_mapped(dock));g_assert_cmpuint(document_get_current()->id,==,primary_id);
  gchar *path=g_build_filename(profile,"insight","workbench.conf",NULL);GKeyFile *settings=g_key_file_new();g_assert_true(g_key_file_load_from_file(settings,path,G_KEY_FILE_NONE,NULL));g_free(path);
  g_assert_cmpint(g_key_file_get_integer(settings,"layout-A","version",NULL),==,IL_FORMAT_VERSION);g_assert_true(g_key_file_get_boolean(settings,"layout-A","window-list-docked",NULL));g_assert_false(g_key_file_get_boolean(settings,"layout-B","window-list-docked",NULL));g_assert_false(g_key_file_get_boolean(settings,"layout-C","window-list-visible",NULL));g_key_file_unref(settings);phase++;
 } else {
  g_print("Real editor GUI: Symbol Info source/metadata, background open, snapshot guard, untitled Jump; Window List preview/activate, filtering and cancelled close/save, saved geometry/filter/sort/column widths/order and soft close/reopen passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY"))) return 77;
 gchar *cwd=g_get_current_dir(),*dirname=g_strdup_printf("symbol-info-gui-%u",g_random_int());profile=g_build_filename(cwd,dirname,"profile",NULL);g_free(cwd);g_free(dirname);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);
 gchar *directory=g_path_get_dirname(profile),*config=g_build_filename(profile,"geany.conf",NULL);primary=g_build_filename(directory,"основной тест.c",NULL);external=g_build_filename(directory,"external.c",NULL);project=g_build_filename(directory,"smoke.geany",NULL);
 g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nuse_native_dialogs=false\n",-1,NULL));g_assert_true(g_file_set_contents(project,"[project]\nname=Symbol Info smoke\nbase_path=.\n",-1,NULL));g_assert_true(g_file_set_contents(primary,"extern int external_symbol(int);\nint helper(int n) { return n + 1; }\nint main(void) { return helper(0) + external_symbol(1); }\nstruct InfoCell { int value; };\ntypedef struct InfoCell InfoAlias;\nInfoAlias *alias_object;\n",-1,NULL));g_assert_true(g_file_set_contents(external,"// closed definition\nint external_symbol(int n) { return n + 2; }\n",-1,NULL));g_free(config);g_free(directory);
 gchar *settings_folder=g_build_filename(profile,"insight",NULL); g_mkdir_with_parents(settings_folder,0700); gchar *settings_file=g_build_filename(settings_folder,"window-list.conf",NULL); g_assert_true(g_file_set_contents(settings_file,"[window-list]\nversion=1\nwidth=740\nheight=390\nx=30000\ny=-30000\npositioned=true\nsort-column=2\nsort-direction=0\ncolumn-widths=55;180;90;60;170;250;\ncolumn-order=0;1;2;3;4;5;\nfilter=\n",-1,NULL)); g_free(settings_file); g_free(settings_folder);
 gchar *argv[]={"symbol-info-gui","--new-instance","--no-session","--config",profile,project,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(40,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,argv);
 gchar *settings=g_build_filename(profile,"insight","window-list.conf",NULL); GKeyFile *key=g_key_file_new(); g_assert_true(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL)); g_assert_cmpint(g_key_file_get_integer(key,"window-list","version",NULL),==,1); g_assert_cmpint(g_key_file_get_integer(key,"window-list","width",NULL),==,760); g_assert_cmpint(g_key_file_get_integer(key,"window-list","height",NULL),==,450); g_assert_cmpint(g_key_file_get_integer(key,"window-list","sort-column",NULL),==,1); g_assert_cmpint(g_key_file_get_integer(key,"window-list","sort-direction",NULL),==,GTK_SORT_DESCENDING); g_key_file_unref(key); g_free(settings);
 g_free(profile);g_free(project);g_free(primary);g_free(external);return result;
}
