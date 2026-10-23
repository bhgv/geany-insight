/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <time.h>
#include <utime.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;
static GeanyDocument *source,*second,*missing;
static gchar *path,*second_path,*missing_path;
static guint scenario,phase,clock_tick;
static GtkWidget *named(GtkWidget *w,const gchar *name)
{
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *top(const gchar *name)
{
 GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;
 for(GList *p=windows;p && !found;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),name)) found=p->data;
 g_list_free(windows);return found;
}
static void contents(GeanyDocument *doc,const gchar *expected)
{ gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void disk_change(const gchar *name,const gchar *text)
{
 g_assert_true(g_file_set_contents(name,text,-1,NULL));struct utimbuf times={0};times.actime=times.modtime=time(NULL)+10+(++clock_tick)*2;g_assert_cmpint(g_utime(name,&times),==,0);
}
static GtkTreePath *find(GtkWidget *tree,const gchar *name,gboolean expected)
{
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));GtkTreeIter iter;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) {
  gchar *value;gboolean pick;gtk_tree_model_get(model,&iter,0,&pick,2,&value,-1);gboolean match=!g_strcmp0(value,name);g_free(value);
  if(match) { g_assert_cmpint(pick,==,expected);return gtk_tree_model_get_path(model,&iter); }
 }
 g_error("Missing file row %s",name);return NULL;
}
static void check(GtkWidget *tree,GtkTreePath *path)
{
 GtkTreeViewColumn *column=gtk_tree_view_get_column(GTK_TREE_VIEW(tree),0);GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));gchar *text=gtk_tree_path_to_string(path);
 g_signal_emit_by_name(cells->data,"toggled",text);g_free(text);g_list_free(cells);
}
static gboolean drive(gpointer data)
{
 (void)data;GtkWidget *dialog=top("insight-reload-modified"),*question=top("insight-reload-confirm");
 if(question) {
  if(scenario==2) SEND(source->editor->sci,SCI_APPENDTEXT,8,(sptr_t)"stale\nα");
  if(scenario==3) disk_change(path,"disk changed again\n");
  if(scenario==5) { document_set_text_changed(source,FALSE);g_assert_true(document_close(source));source=NULL; }
  gtk_dialog_response(GTK_DIALOG(question),scenario==0 ? GTK_RESPONSE_CANCEL:GTK_RESPONSE_ACCEPT);phase=10;return G_SOURCE_CONTINUE;
 }
 if(!dialog) return G_SOURCE_CONTINUE;
 GtkWidget *tree=named(dialog,"insight-reload-files");g_assert_nonnull(tree);
 if(phase==10) { gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(phase==0) {
  if(scenario==0) {
   GtkTreePath *p=find(tree,path,TRUE);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),p,NULL,FALSE);gtk_tree_path_free(p);
   p=find(tree,second_path,FALSE);gtk_tree_path_free(p);p=find(tree,missing_path,FALSE);check(tree,p);gtk_tree_path_free(p);p=find(tree,missing_path,FALSE);gtk_tree_path_free(p);
   gtk_dialog_response(GTK_DIALOG(dialog),5);phase=1;return G_SOURCE_CONTINUE;
  }
  if(scenario==4) {
   GtkTreePath *p=find(tree,path,TRUE);check(tree,p);gtk_tree_path_free(p);
   p=find(tree,second_path,FALSE);check(tree,p);gtk_tree_path_free(p);
  }
  gtk_dialog_response(GTK_DIALOG(dialog),7);phase=5;return G_SOURCE_CONTINUE;
 }
 if(phase==1) {
  GtkWidget *compare=top("insight-snapshot-compare-window");g_assert_nonnull(compare);g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(named(compare,"insight-snapshot-left"))));gtk_widget_destroy(compare);
  gtk_dialog_response(GTK_DIALOG(dialog),6);phase=2;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {
  gchar *clipboard=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)),*expected=g_strconcat(path,"\n",NULL);g_assert_cmpstr(clipboard,==,expected);g_free(clipboard);g_free(expected);
  gtk_dialog_response(GTK_DIALOG(dialog),7);phase=5;return G_SOURCE_CONTINUE;
 }
 return G_SOURCE_CONTINUE;
}
static void run(guint which,gboolean key)
{
 scenario=which;phase=0;g_timeout_add(80,drive,NULL);
 if(key) keybindings_send_command(GEANY_KEY_GROUP_DOCUMENT,GEANY_KEYS_INSIGHT_RELOADMODIFIED);
 else { GtkWidget *item=named(window,"insight-reload-modified-command");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
 g_assert_null(top("insight-reload-modified"));g_assert_cmpuint(phase,==,10);
}
static gboolean step(gpointer data)
{
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;
 window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));if(!named(window,"insight-reload-modified-command")) return G_SOURCE_CONTINUE;
 second=document_open_file(second_path,FALSE,NULL,NULL);missing=document_open_file(missing_path,FALSE,NULL,NULL);g_assert_nonnull(second);g_assert_nonnull(missing);
 SEND(second->editor->sci,SCI_SETTEXT,0,(sptr_t)"pending second\n");g_assert_true(second->changed);
 g_assert_cmpint(g_unlink(missing_path),==,0);disk_change(path,"external α\n");disk_change(second_path,"external second\n");
 run(0,FALSE);contents(source,"initial\n");contents(second,"pending second\n");
 source->readonly=TRUE;SEND(source->editor->sci,SCI_SETREADONLY,TRUE,0);
 run(1,TRUE);contents(source,"external α\n");g_assert_true(source->readonly);contents(second,"pending second\n");
 source->readonly=FALSE;SEND(source->editor->sci,SCI_SETREADONLY,FALSE,0);
 disk_change(path,"external next\n");run(2,FALSE);contents(source,"external α\nstale\nα");
 /* Save prepares a new clean baseline without altering the other dirty buffer. */
 g_assert_true(document_save_file(source,TRUE));disk_change(path,"external final\n");run(3,FALSE);contents(source,"external α\nstale\nα");
 run(4,FALSE);contents(second,"external second\n");g_assert_false(second->changed);
 /* Closed document IDs cannot be reused as batch targets. */
 run(5,FALSE);g_assert_null(source);contents(second,"external second\n");
 document_set_text_changed(missing,FALSE);
 g_print("Reload Modified Files: automatic clean selection, dirty/missing exclusion, copy, immutable comparison, Cancel, key dispatch, read-only reload, stale buffer/disk, dirty confirmation and closed target passed\n");
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data)
{ (void)data;g_error("Reload timeout scenario %u phase %u",scenario,phase);return G_SOURCE_REMOVE; }
int main(void)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("reload-modified-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL);
 path=g_build_filename(dir,"исходник.c",NULL);second_path=g_build_filename(dir,"second.c",NULL);missing_path=g_build_filename(dir,"missing.c",NULL);
 g_assert_true(g_file_set_contents(path,"initial\n",-1,NULL));g_assert_true(g_file_set_contents(second_path,"second\n",-1,NULL));g_assert_true(g_file_set_contents(missing_path,"missing\n",-1,NULL));
 g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\ndisk_check_timeout=0\nkeep_edit_history_on_reload=true\nshow_keep_edit_history_on_reload_msg=false\n",-1,NULL));
 gchar *args[]={"reload-modified","--new-instance","--no-session","--config",dir,path,NULL};
 g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(40,timeout,NULL);return main_lib(6,args);
}
