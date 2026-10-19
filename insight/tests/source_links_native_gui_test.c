/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static const gchar *source="// header\r\n// α needle one\r\n// needle two\r\n// tail\r\n";
static const gchar *prefix="// inserted\r\n";
static GtkWidget *window,*results;static gchar *file,*profile;static guint phase;static GeanyDocument *doc;
static GtkWidget *named(GtkWidget *root,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_MENU_ITEM(root)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root)),*found=sub ? named(sub,name) : NULL;if(found) return found; }
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static void command(guint n) { gchar *name=g_strdup_printf("insight-source-link-command-%u",n);GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));g_free(name); }
static void selected(const gchar *text) { gchar *value=sci_get_selection_contents(doc->editor->sci);g_assert_cmpstr(value,==,text);g_free(value); }
static void activate(gint row) { GtkTreePath *path=gtk_tree_path_new_from_indices(row,-1);gtk_tree_view_row_activated(GTK_TREE_VIEW(results),path,gtk_tree_view_get_column(GTK_TREE_VIEW(results),0));gtk_tree_path_free(path);doc=document_get_current(); }
static void back_key(void) {
 GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(doc->editor->sci)));event->key.keyval=GDK_KEY_l;event->key.state=GDK_CONTROL_MASK;gboolean handled=FALSE;
 static guint calls;g_signal_emit_by_name((++calls % 2) ? G_OBJECT(window) : G_OBJECT(doc->editor->sci),"key-press-event",&event->key,&handled);g_assert_true(handled);gdk_event_free(event);g_assert_true(gtk_widget_has_focus(results));
}
static void search(void) { gtk_combo_box_set_active(GTK_COMBO_BOX(named(window,"insight-search-scope")),2);GtkWidget *query=named(window,"insight-project-query");gtk_entry_set_text(GTK_ENTRY(query),"needle");g_signal_emit_by_name(query,"activate"); }
static gboolean timeout(gpointer data) { (void)data;g_error("Source links native GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;if(!window) { doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci)); }
 if(!phase) { doc=document_open_file(file,FALSE,NULL,NULL);g_assert_nonnull(doc);results=named(window,"insight-search-results");g_assert_nonnull(results);search();phase++;return G_SOURCE_CONTINUE; }
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(results));
 if(phase==1) {
  if(gtk_tree_model_iter_n_children(model,NULL)!=2) return G_SOURCE_CONTINUE;
  activate(0);selected("needle");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,1);back_key();
  SEND(doc->editor->sci,SCI_INSERTTEXT,0,(sptr_t)prefix);activate(0);selected("needle");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,2);
  SEND(doc->editor->sci,SCI_UNDO,0,0);activate(0);selected("needle");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,1);
  SEND(doc->editor->sci,SCI_REDO,0,0);gint line_start=sci_get_position_from_line(doc->editor->sci,2);SEND(doc->editor->sci,SCI_INSERTTEXT,line_start,(sptr_t)"// λ ");activate(0);selected("needle");
  SEND(doc->editor->sci,SCI_UNDO,0,0);activate(0);selected("needle");back_key();
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model),2,GTK_SORT_DESCENDING);activate(1);selected("needle");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,2);back_key();
  GtkTreeIter removed;GtkTreePath *path=gtk_tree_path_new_from_indices(1,-1);g_assert_true(gtk_tree_model_get_iter(model,&removed,path));gtk_list_store_remove(GTK_LIST_STORE(model),&removed);gtk_tree_path_free(path);
  command(1);doc=document_get_current();selected("needle");g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,3);back_key();
  gint begin=sci_get_position_from_line(doc->editor->sci,3),end=sci_get_position_from_line(doc->editor->sci,4);SEND(doc->editor->sci,SCI_DELETERANGE,begin,end-begin);SEND(doc->editor->sci,SCI_SETSEL,0,0);activate(0);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,0);
  SEND(doc->editor->sci,SCI_UNDO,0,0);activate(0);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,0);
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model),GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,GTK_SORT_ASCENDING);search();phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {
  if(gtk_tree_model_iter_n_children(model,NULL)!=2) return G_SOURCE_CONTINUE;
  /* Only fresh rows carry the current snapshot; opening revalidates any
   * retained result and captures its new editor anchor. */
  activate(0);selected("needle");g_assert_true(document_save_file(doc,FALSE));g_assert_true(document_close(doc));doc=NULL;
  command(1);doc=document_get_current();g_assert_cmpstr(doc->file_name,==,file);selected("needle");back_key();
  g_assert_true(document_close(doc));doc=NULL;gchar *saved=NULL;g_assert_true(g_file_get_contents(file,&saved,NULL,NULL));gchar *changed=g_strconcat("// external change\r\n",saved,NULL);g_assert_true(g_file_set_contents(file,changed,-1,NULL));g_free(saved);g_free(changed);
  command(1);doc=document_get_current();g_assert_cmpstr(doc->file_name,==,file);g_assert_cmpint(sci_get_selection_start(doc->editor->sci),==,sci_get_selection_end(doc->editor->sci));g_assert_false(doc->changed);
  g_print("Source Links: bidirectional Ctrl+L, UTF-8/CRLF edits, undo/redo, sorting, row removal, deleted-line refusal, fresh search, saved target reopening and changed-disk refusal passed.\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("source-links-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL);g_free(cwd);g_free(name);profile=g_build_filename(directory,"profile",NULL);file=g_build_filename(directory,"sample.c",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);g_assert_true(g_file_set_contents(file,source,-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"source-links","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);g_free(file);return result;
}
