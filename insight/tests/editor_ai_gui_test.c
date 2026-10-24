/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window,*review;static GeanyDocument *source;static gchar *path,*profile,*endpoint,*root;static GSubprocess *server;static guint phase;static gint64 after;static gchar *v1,*manual,*v2;
static const gchar *base="int value = 1;\n",*external="int external_edit = 3;\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *v=named(m,name);if(v) return v; } }if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *v=NULL;for(GList *p=list;p && !v;p=p->next) v=named(p->data,name);g_list_free(list);return v; }return NULL; }
static GtkWidget *top(const gchar *name) { GList *list=gtk_window_list_toplevels();GtkWidget *v=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data) && !g_strcmp0(gtk_widget_get_name(p->data),name)) v=p->data;g_list_free(list);return v; }
static void click(const gchar *name) { GtkWidget *b=named(review,name);g_assert_nonnull(b);g_assert_true(gtk_widget_get_sensitive(b));gtk_button_clicked(GTK_BUTTON(b)); }
static void entry(const gchar *name,const gchar *value) { gtk_entry_set_text(GTK_ENTRY(named(review,name)),value); }
static gchar *text(const gchar *name) { GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(review,name)));GtkTextIter a,z;gtk_text_buffer_get_bounds(b,&a,&z);return gtk_text_buffer_get_text(b,&a,&z,TRUE); }
static void set_text(const gchar *name,const gchar *value) { gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(review,name))),value,-1); }
static void assert_text(const gchar *name,const gchar *expected) { gchar *s=text(name);g_assert_cmpstr(s,==,expected);g_free(s); }
static void assert_source(const gchar *expected) { gchar *s=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(s,==,expected);g_free(s); }
static gboolean select_project(gpointer data) {
 (void)data;GtkWidget *d=top("insight-editor-ai-project-files");if(!d) return G_SOURCE_CONTINUE;GtkWidget *tree=named(d,"insight-editor-ai-project-list");gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static gboolean step(gpointer data) {
 (void)data;if(review && g_object_get_data(G_OBJECT(review),"insight-editor-ai-busy")) return G_SOURCE_CONTINUE;
 if(review && gtk_widget_get_sensitive(named(review,"insight-diff-cancel"))) return G_SOURCE_CONTINUE;
 switch(phase) {
 case 0:
  source=document_get_current();if(!source || g_strcmp0(source->file_name,path)) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-editor-ai-menu")));review=top("insight-editor-ai-window");g_assert_nonnull(review);
  GtkWidget *tabs=named(review,"insight-diff-toolbar-tabs");g_assert_true(GTK_IS_NOTEBOOK(tabs));g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(tabs)),>=,10);g_assert_true(gtk_widget_get_ancestor(named(review,"insight-editor-ai-prompt"),GTK_TYPE_NOTEBOOK)==tabs);g_assert_true(gtk_widget_get_ancestor(named(review,"insight-editor-ai-provider"),GTK_TYPE_NOTEBOOK)==tabs);phase++;break;
 case 1: {
  gtk_combo_box_set_active(GTK_COMBO_BOX(named(review,"insight-editor-ai-provider")),4);entry("insight-editor-ai-endpoint",endpoint);entry("insight-editor-ai-model","editor");entry("insight-editor-ai-key","private-editor-key");
  /* Project capture can become ready a little later than the editor. */
  g_timeout_add(50,select_project,NULL);click("insight-editor-ai-project");GtkTreeModel *model=gtk_combo_box_get_model(GTK_COMBO_BOX(named(review,"insight-editor-ai-file")));if(gtk_tree_model_iter_n_children(model,NULL)<2) break;
  click("insight-editor-ai-context-build");gchar *context=text("insight-editor-ai-context");g_assert_nonnull(strstr(context,"helper_function"));g_free(context);
  gtk_combo_box_set_active(GTK_COMBO_BOX(named(review,"insight-editor-ai-task")),4);click("insight-editor-ai-add-task");gchar *prompt=text("insight-editor-ai-prompt");g_assert_nonnull(strstr(prompt,"names"));g_free(prompt);set_text("insight-editor-ai-prompt","FIRST: improve readability");click("insight-editor-ai-send");phase++;break;
 }
 case 2:
  { GdkEventKey key={0};key.type=GDK_KEY_PRESS;key.state=GDK_CONTROL_MASK;key.keyval=GDK_KEY_f;gboolean handled=FALSE;g_signal_emit_by_name(review,"key-press-event",&key,&handled);g_assert_true(handled);g_assert_true(gtk_widget_get_visible(gtk_widget_get_parent(named(review,"insight-diff-search"))));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(review,"insight-diff-ai-tools")),FALSE); }
  v1=g_strconcat(base,"// generated variant\n",NULL);assert_text("insight-diff-result",v1);assert_source(base);manual=g_strconcat(v1,"// manual correction\n",NULL);set_text("insight-diff-result",manual);set_text("insight-editor-ai-prompt","SECOND: add comments");click("insight-editor-ai-send");phase++;break;
 case 3: {
  v2=g_strconcat(manual,"// generated variant\n",NULL);assert_text("insight-diff-result",v2);gchar *requirements=text("insight-editor-ai-requirements");g_assert_nonnull(strstr(requirements,"FIRST"));g_assert_nonnull(strstr(requirements,"SECOND"));g_free(requirements);gchar *answer=text("insight-diff-ai-explanation");g_assert_nonnull(strstr(answer,"manual=True"));g_assert_nonnull(strstr(answer,"project=True"));g_free(answer);click("insight-editor-ai-undo");phase++;break;
 }
 case 4:assert_text("insight-diff-result",manual);assert_text("insight-editor-ai-requirements","FIRST: improve readability");click("insight-editor-ai-redo");phase++;break;
 case 5:
  assert_text("insight-diff-result",v2);click("insight-editor-ai-open");g_assert_true(document_get_current()==source);g_assert_true(gtk_widget_get_visible(review));sci_set_text(source->editor->sci,external);click("insight-diff-ai-apply");assert_source(external);gtk_window_present(GTK_WINDOW(review));click("insight-editor-ai-refresh");phase++;break;
 case 6:
  assert_text("insight-diff-left",external);assert_text("insight-diff-result",v2);SEND(source->editor->sci,SCI_SETREADONLY,TRUE,0);click("insight-diff-ai-apply");assert_source(external);SEND(source->editor->sci,SCI_SETREADONLY,FALSE,0);click("insight-diff-ai-apply");assert_source(v2);SEND(source->editor->sci,SCI_UNDO,0,0);assert_source(external);after=g_get_monotonic_time()+200*G_TIME_SPAN_MILLISECOND;phase++;break;
 case 7: {
  if(g_get_monotonic_time()<after) { break; }
  if(g_getenv("GEANY_EDITOR_AI_CAPTURE")) { GdkPixbuf *im=gdk_pixbuf_get_from_window(gtk_widget_get_window(review),0,0,gtk_widget_get_allocated_width(review),gtk_widget_get_allocated_height(review));g_assert_nonnull(im);g_assert_true(gdk_pixbuf_save(im,g_getenv("GEANY_EDITOR_AI_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(im); }
  gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,base);g_free(disk);gchar *config=g_build_filename(profile,"insight-ai.conf",NULL);g_assert_true(g_file_get_contents(config,&disk,NULL,NULL));g_assert_null(strstr(disk,"private-editor-key"));g_free(disk);g_free(config);config=g_build_filename(profile,"insight","ai-history","editor-project.conf",NULL);g_assert_true(g_file_get_contents(config,&disk,NULL,NULL));g_assert_nonnull(strstr(disk,"FIRST"));g_assert_nonnull(strstr(disk,"SECOND"));g_assert_null(strstr(disk,"private-editor-key"));g_assert_null(strstr(disk,"generated variant"));g_free(disk);g_free(config);
  GtkTreeModel *recent=gtk_combo_box_get_model(GTK_COMBO_BOX(named(review,"insight-editor-ai-recent")));g_assert_cmpint(gtk_tree_model_iter_n_children(recent,NULL),==,2);gtk_combo_box_set_active(GTK_COMBO_BOX(named(review,"insight-editor-ai-recent")),0);click("insight-editor-ai-history-delete");g_assert_cmpint(gtk_tree_model_iter_n_children(recent,NULL),==,1);click("insight-editor-ai-history-clear");g_assert_cmpint(gtk_tree_model_iter_n_children(recent,NULL),==,0);
  click("insight-editor-ai-recompare");click("insight-diff-cancel");phase++;break;
 }
 case 8:
  g_assert_false(gtk_widget_get_sensitive(named(review,"insight-diff-ai-apply")));click("insight-editor-ai-recompare");phase++;break;
 case 9:
  assert_text("insight-diff-result",v2);entry("insight-editor-ai-model","slow");set_text("insight-editor-ai-prompt","cancel this request");click("insight-editor-ai-send");click("insight-editor-ai-cancel");phase++;break;
 case 10:
  assert_text("insight-diff-result",v2);gtk_widget_destroy(review);review=NULL;document_set_text_changed(source,FALSE);g_subprocess_force_exit(server);g_subprocess_wait(server,NULL,NULL);g_object_unref(server);g_print("Editor AI native GUI: project membership/context, standard tasks, cumulative requests and manual Result, variant Undo/Redo, open source without closing Diff, explicit refresh, stale/read-only guards, one Undo, unchanged disk, prompt lists/privacy and request cancellation passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer p) { (void)p;g_error("Editor AI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 g_assert_cmpint(argc,==,2);GError *error=NULL;server=g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE,&error,"python3","-I",argv[1],NULL);g_assert_no_error(error);GDataInputStream *in=g_data_input_stream_new(g_subprocess_get_stdout_pipe(server));gchar *port=g_data_input_stream_read_line(in,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(port);endpoint=g_strdup_printf("http://127.0.0.1:%s/v1/chat/completions",port);g_free(port);g_object_unref(in);
 gchar *dir=g_dir_make_tmp("editor-ai-native-XXXXXX",NULL);root=g_build_filename(dir,"source",NULL);g_assert_cmpint(g_mkdir(root,0700),==,0);profile=g_build_filename(dir,"profile",NULL);path=g_build_filename(root,"source.c",NULL);gchar *helper=g_build_filename(root,"helper.c",NULL),*project=g_build_filename(dir,"fixture.geany",NULL);g_assert_true(g_file_set_contents(path,base,-1,NULL));g_assert_true(g_file_set_contents(helper,"int helper_function(void) { return 0; }\n",-1,NULL));gchar *settings=g_strdup_printf("[project]\nname=Editor AI fixture\nbase_path=%s\n",root);g_assert_true(g_file_set_contents(project,settings,-1,NULL));g_free(settings);gchar *args[]={"editor-ai-native","--new-instance","--no-session","--config",profile,project,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(100,step,NULL);g_timeout_add_seconds(50,timeout,NULL);return main_lib(7,args);
}
