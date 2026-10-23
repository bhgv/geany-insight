/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*flat,*tree;static GeanyDocument *source_doc;static gchar *file,*closed_file;static guint phase,action,stage;static gint caret,anchor;static gint64 wait_until;
static const gchar *source="class Root {};\nclass Middle : public Root {};\nclass End : public Middle {};\nclass Other : public Root {};\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=list;p && !found;p=p->next) found=named(p->data,name);g_list_free(list);return found; }return NULL; }
static GtkWidget *report(void) { GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data) && !g_strcmp0(gtk_widget_get_name(p->data),"insight-relation-report")) found=p->data;g_list_free(list);return found; }
static guint bookmarks(void) { return gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(named(window,"insight-bookmarks"))),NULL); }
static gchar *preview(GtkWidget *d) { GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(d,"insight-relation-report-text")));GtkTextIter a,z;gtk_text_buffer_get_bounds(b,&a,&z);return gtk_text_buffer_get_text(b,&a,&z,FALSE); }
static void filter(const gchar *text) { gtk_entry_set_text(GTK_ENTRY(named(window,"insight-relation-filter")),text); }
static void toggle(const gchar *name,gboolean value) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,name)),value); }
static void open_report(guint next_action) { action=next_action;stage=0;GtkWidget *button=named(window,"insight-relation-outline");g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button));g_assert_cmpuint(action,==,0); }
static gboolean interrupt(gpointer data) { GtkWidget *d=report();g_assert_nonnull(d);g_assert_nonnull(g_object_get_data(G_OBJECT(d),"insight-relation-busy"));if(GPOINTER_TO_INT(data)==1) gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);else filter("Other");return G_SOURCE_REMOVE; }
static gboolean drive(gpointer data) {
 (void)data;if(!action || action==7) return G_SOURCE_CONTINUE;GtkWidget *d=report();if(!d) return G_SOURCE_CONTINUE;
 if(g_object_get_data(G_OBJECT(d),"insight-relation-busy")) return G_SOURCE_CONTINUE;
 GtkWidget *create=named(d,"insight-relation-report-create");const gchar *status=gtk_label_get_text(GTK_LABEL(named(d,"insight-relation-report-status")));
 if(action==6) { if(!stage) { if(!gtk_widget_get_sensitive(create)) { gtk_dialog_response(GTK_DIALOG(d),1);return G_SOURCE_CONTINUE; } gtk_dialog_response(GTK_DIALOG(d),4);stage=1; }else { g_assert_nonnull(strstr(status,"remaining note capacity"));g_assert_cmpuint(bookmarks(),==,4);action=0;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE); }return G_SOURCE_CONTINUE; }
 if(!gtk_widget_get_sensitive(create)) { gtk_dialog_response(GTK_DIALOG(d),1);return G_SOURCE_CONTINUE; }
 if(!stage) {
  gchar *text=preview(d);g_assert_nonnull(strstr(text,"Relations: Root"));g_assert_nonnull(named(d,"insight-relation-report-print"));g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(named(d,"insight-relation-report-text"))));
  if(action==1) { g_assert_nonnull(strstr(text,"Other"));g_assert_null(strstr(text,"Middle"));g_assert_cmpuint(bookmarks(),==,0);g_free(text);action=0;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_CONTINUE; }
  if(action==5) { g_assert_nonnull(strstr(text,"    End\t"));g_assert_nonnull(strstr(text,"Closed")); }
  g_free(text);gtk_dialog_response(GTK_DIALOG(d),2);stage=1;return G_SOURCE_CONTINUE;
 }
 if(stage==1) {
  gchar *copy=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(copy);g_assert_nonnull(strstr(copy,"Other"));g_free(copy);
  gtk_dialog_response(GTK_DIALOG(d),4);if(action==2 || action==3) g_idle_add_full(G_PRIORITY_HIGH,interrupt,GINT_TO_POINTER(action==2 ? 1:2),NULL);stage=2;if(action==2) action=0;return G_SOURCE_CONTINUE;
 }
 if(action==3) { g_assert_nonnull(strstr(status,"changed"));g_assert_cmpuint(bookmarks(),==,0);action=0;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_CONTINUE; }
 if(stage==2) {
  g_assert_nonnull(strstr(status,"bookmarks created"));g_assert_cmpuint(bookmarks(),==,action==5 ? 4:3);gtk_dialog_response(GTK_DIALOG(d),4);stage=3;return G_SOURCE_CONTINUE;
 }
 g_assert_nonnull(strstr(status,"No new source locations"));g_assert_cmpuint(bookmarks(),==,action==5 ? 4:3);
 if(action==5) { gtk_dialog_response(GTK_DIALOG(d),3);stage=4;action=7;return G_SOURCE_CONTINUE; }
 action=0;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_CONTINUE;
}
static gboolean drive_open(gpointer data) {
 (void)data;if(action!=7) return G_SOURCE_CONTINUE;GtkWidget *d=report();g_assert_nonnull(d);GeanyDocument *doc=document_get_current();g_assert_nonnull(doc);if(doc==source_doc) return G_SOURCE_CONTINUE;g_assert_true(doc!=source_doc);g_assert_null(doc->file_name);g_assert_true(doc->changed);gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_nonnull(strstr(text,"    End\t"));g_free(text);document_set_text_changed(doc,FALSE);g_assert_true(document_close(doc));action=0;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_CONTINUE;
}
static GtkTreePath *middle;
static gboolean find_middle(GtkTreeModel *m,GtkTreePath *p,GtkTreeIter *i,gpointer data) { (void)data;gchar *name=NULL;gtk_tree_model_get(m,i,7,&name,-1);gboolean found=!g_strcmp0(name,"Middle");g_free(name);if(found) middle=gtk_tree_path_copy(p);return found; }
static gboolean has_end(GtkTreeModel *m,GtkTreePath *p,GtkTreeIter *i,gpointer data) { (void)p;gchar *name=NULL;gtk_tree_model_get(m,i,7,&name,-1);gboolean found=!g_strcmp0(name,"End");g_free(name);if(found) *(gboolean *)data=TRUE;return found; }
static void cycle_views(gboolean tree_mode) {
 GtkWidget *button=named(window,"insight-relation-next-view");
 GtkWidget *direction=named(window,tree_mode ? "insight-relation-tree-graph-direction":"insight-relation-graph-direction");
 GtkWidget *zoom=named(window,tree_mode ? "insight-relation-tree-graph-zoom":"insight-relation-graph-zoom");
 GtkWidget *graph=named(window,"insight-relation-graph-toggle");
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree_mode ? tree:flat));
 gint count=gtk_tree_model_iter_n_children(model,NULL);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(zoom),125);
 for(guint i=0;i<6;i++) {
  gtk_button_clicked(GTK_BUTTON(button));
  g_assert_cmpint(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(graph)),==,i%3!=2);
  if(i%3!=2) g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(direction)),==,i%3);
  g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(zoom)),==,125);
  g_assert_true(model==gtk_tree_view_get_model(GTK_TREE_VIEW(tree_mode ? tree:flat)));
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,count);
 }
}
static GtkWidget *command(const gchar *name) {
 GtkWidget *navigate=g_object_get_data(G_OBJECT(window),"insight-navigate");
 GtkWidget *item=named(gtk_menu_item_get_submenu(GTK_MENU_ITEM(navigate)),name);g_assert_nonnull(item);return item;
}
static void row_deleted(GtkTreeModel *model,GtkTreePath *path,gpointer data) { (void)model;(void)path;(*(guint *)data)++; }
static guint print_cancelled;
static gboolean cancel_print(gpointer data) {
 guint *attempts=data;g_assert_cmpuint(++*attempts,<,100);GList *list=gtk_window_list_toplevels();
 for(GList *p=list;p;p=p->next) if(GTK_IS_DIALOG(p->data) && gtk_widget_get_visible(p->data) && g_str_has_prefix(G_OBJECT_TYPE_NAME(p->data),"GtkPrint")) { print_cancelled++;gtk_dialog_response(GTK_DIALOG(p->data),GTK_RESPONSE_CANCEL);g_list_free(list);return G_SOURCE_REMOVE; }
 g_list_free(list);return G_SOURCE_CONTINUE;
}
static void check_print_commands(void) {
 for(guint graph=0;graph<2;graph++) {
  toggle("insight-relation-graph-toggle",graph);guint attempts=0;g_timeout_add(50,cancel_print,&attempts);gtk_menu_item_activate(GTK_MENU_ITEM(command("insight-relation-print-command")));g_assert_cmpuint(print_cancelled,==,graph+1);
 }
 toggle("insight-relation-graph-toggle",FALSE);
}
static void check_locks(void) {
 GtkWidget *follow=named(window,"insight-relations-follow");GtkWidget *lock=command("insight-relation-lock-command");
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(flat));guint deleted=0;
 gulong handler=g_signal_connect(model,"row-deleted",G_CALLBACK(row_deleted),&deleted);
 g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lock)));
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(lock),FALSE);
 g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(follow)));
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(lock),TRUE);
 g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(follow)));
 g_assert_cmpuint(deleted,==,0);g_signal_handler_disconnect(model,handler);
 GtkWidget *context=named(window,"insight-context-follow");GtkWidget *context_lock=command("insight-context-lock-command");
 g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(context_lock)));
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(context_lock),FALSE);
 g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(context)));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(context),FALSE);
 g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(context_lock)));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(follow),TRUE);
 g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lock)));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(follow),FALSE);
 g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lock)));
}
static void direct_views(gboolean tree_mode) {
 const gchar *names[]={"insight-relation-vertical-command","insight-relation-horizontal-command","insight-relation-list-command"};
 GtkWidget *direction=named(window,tree_mode ? "insight-relation-tree-graph-direction":"insight-relation-graph-direction");
 for(guint i=0;i<3;i++) {
  gtk_menu_item_activate(GTK_MENU_ITEM(command(names[i])));
  g_assert_cmpint(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(window,"insight-relation-graph-toggle"))),==,i!=2);
  if(i!=2) g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(direction)),==,i==0);
 }
}
static gboolean step(gpointer data) {
 (void)data;if(!window) { source_doc=document_get_current();if(!source_doc) return G_SOURCE_CONTINUE;source_doc=document_open_file(file,FALSE,NULL,NULL);window=gtk_widget_get_toplevel(GTK_WIDGET(source_doc->editor->sci));flat=named(window,"insight-relations");tree=named(window,"insight-relations-tree");g_assert_nonnull(flat);g_assert_nonnull(tree);toggle("insight-relations-follow",FALSE);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-relation-query")),"Root");gtk_combo_box_set_active(GTK_COMBO_BOX(named(window,"insight-relation-mode")),5);return G_SOURCE_CONTINUE; }
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(flat));
 if(phase==0) { if(gtk_tree_model_iter_n_children(model,NULL)!=3) return G_SOURCE_CONTINUE;caret=sci_get_current_position(source_doc->editor->sci);anchor=scintilla_send_message(source_doc->editor->sci,SCI_GETANCHOR,0,0);check_print_commands();check_locks();cycle_views(FALSE);direct_views(FALSE);filter("Other");open_report(1);filter("");
 GtkTreePath *first=gtk_tree_path_new_first();gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(flat)),first);gtk_tree_path_free(first);gtk_button_clicked(GTK_BUTTON(named(window,"insight-relation-node-remove")));g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,2);
 gtk_menu_item_activate(GTK_MENU_ITEM(command("insight-relation-refresh-command")));g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(command("insight-relation-lock-command"))));phase++; }
 else if(phase==1) { if(gtk_tree_model_iter_n_children(model,NULL)!=3) return G_SOURCE_CONTINUE;open_report(2);wait_until=g_get_monotonic_time()+500000;phase++; }
 else if(phase==2) { if(g_get_monotonic_time()<wait_until) return G_SOURCE_CONTINUE;g_assert_cmpuint(bookmarks(),==,0);open_report(3);filter("");phase++; }
 else if(phase==3) { if(gtk_tree_model_iter_n_children(model,NULL)!=3) return G_SOURCE_CONTINUE;open_report(4);g_assert_null(document_find_by_filename(closed_file));toggle("insight-relation-tree-toggle",TRUE);phase++; }
 else if(phase==4) { gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),find_middle,NULL);if(!middle) return G_SOURCE_CONTINUE;gtk_tree_view_expand_row(GTK_TREE_VIEW(tree),middle,FALSE);gtk_tree_path_free(middle);middle=NULL;phase++; }
 else if(phase==5) { gboolean found=FALSE;gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),has_end,&found);if(!found) return G_SOURCE_CONTINUE;cycle_views(TRUE);direct_views(TRUE);open_report(5);toggle("insight-relation-tree-toggle",FALSE);wait_until=g_get_monotonic_time()+G_TIME_SPAN_SECOND;phase++; }
 else if(phase==6) {
  if(g_get_monotonic_time()<wait_until) return G_SOURCE_CONTINUE;
  GtkTreeModel *base=model;while(GTK_IS_TREE_MODEL_SORT(base) || GTK_IS_TREE_MODEL_FILTER(base)) base=GTK_IS_TREE_MODEL_SORT(base) ? gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(base)):gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(base));g_assert_true(GTK_IS_LIST_STORE(base));for(guint i=0;i<65;i++) { GtkTreeIter row;gtk_list_store_append(GTK_LIST_STORE(base),&row);gtk_list_store_set(GTK_LIST_STORE(base),&row,0,file,1,file,2,(gulong)(100+i),7,"Overflow",-1); }open_report(6);
  gchar *text=sci_get_contents(source_doc->editor->sci,-1);g_assert_cmpstr(text,==,source);g_free(text);g_assert_false(source_doc->changed);g_assert_cmpint(sci_get_current_position(source_doc->editor->sci),==,caret);g_assert_cmpint(scintilla_send_message(source_doc->editor->sci,SCI_GETANCHOR,0,0),==,anchor);g_assert_false(scintilla_send_message(source_doc->editor->sci,SCI_CANUNDO,0,0));
  g_print("Relation results: filtered preview/copy, pending cancellation, stale-model refusal, open/closed source bookmarks, deduplication, loaded-tree indentation, unsaved outline and atomic capacity refusal passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Relation results timeout phase %u action %u stage %u",phase,action,stage);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;gchar *dir=g_dir_make_tmp("insight-relation-results-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL),*sd=g_build_filename(profile,"insight",NULL),*settings=g_build_filename(sd,"workbench.conf",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);file=g_build_filename(dir,"types.cpp",NULL);closed_file=g_build_filename(dir,"closed.cpp",NULL);g_assert_true(g_file_set_contents(file,source,-1,NULL));g_assert_true(g_file_set_contents(closed_file,"class Closed : public Root {};\n",-1,NULL));gchar *encoded=g_base64_encode((const guchar *)dir,strlen(dir)),*seed=g_strdup_printf("[workbench]\nversion=1\nfollow=false\nrelation-follow=false\nproject-visible=true\nroot-base64=%s\n",encoded);g_assert_true(g_file_set_contents(settings,seed,-1,NULL));g_free(encoded);g_free(seed);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *args[]={"relation-results","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(100,drive,NULL);g_timeout_add(100,drive_open,NULL);g_timeout_add(200,step,NULL);g_timeout_add_seconds(40,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,args);gchar *notes=g_build_filename(sd,"notes.conf",NULL);GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,notes,G_KEY_FILE_NONE,NULL));g_assert_cmpint(g_key_file_get_integer(key,"notes","count",NULL),==,4);gchar *saved=NULL;g_assert_true(g_file_get_contents(file,&saved,NULL,NULL));g_assert_cmpstr(saved,==,source);g_free(saved);g_assert_true(g_file_get_contents(closed_file,&saved,NULL,NULL));g_assert_cmpstr(saved,==,"class Closed : public Root {};\n");g_free(saved);g_key_file_unref(key);g_free(notes);return result;
}
