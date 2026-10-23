/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "vte.h"
#include <gio/gio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window,*assistant,*diff;
static GeanyDocument *source;
static gchar *path,*profile,*endpoint;
static guint phase;
static gint64 after;
static GSubprocess *server;
static const gchar *base="int value = 1\n",*proposal="int value = 2;\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *v=named(m,name);if(v) return v; } }
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *v=NULL;for(GList *p=list;p && !v;p=p->next) v=named(p->data,name);g_list_free(list);return v; }return NULL;
}
static GtkWidget *top(const gchar *name) { GList *list=gtk_window_list_toplevels();GtkWidget *v=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data) && !g_strcmp0(gtk_widget_get_name(p->data),name)) v=p->data;g_list_free(list);return v; }
static void click(GtkWidget *w,const gchar *name) { GtkWidget *b=named(w,name);g_assert_nonnull(b);g_assert_true(gtk_widget_get_sensitive(b));gtk_button_clicked(GTK_BUTTON(b)); }
static void entry(GtkWidget *w,const gchar *name,const gchar *value) { GtkWidget *v=named(w,name);g_assert_nonnull(v);gtk_entry_set_text(GTK_ENTRY(v),value); }
static gchar *text(GtkWidget *w,const gchar *name) { GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(w,name)));GtkTextIter a,z;gtk_text_buffer_get_bounds(b,&a,&z);return gtk_text_buffer_get_text(b,&a,&z,TRUE); }
static void source_is(const gchar *expected) { gchar *s=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(s,==,expected);g_free(s); }
static void capture(GtkWidget *w,const gchar *env) { if(!g_getenv(env)) return;GdkPixbuf *im=gdk_pixbuf_get_from_window(gtk_widget_get_window(w),0,0,gtk_widget_get_allocated_width(w),gtk_widget_get_allocated_height(w));g_assert_nonnull(im);g_assert_true(gdk_pixbuf_save(im,g_getenv(env),"jpeg",NULL,"quality","88",NULL));g_object_unref(im); }
static gboolean step(gpointer data) {
 (void)data;
 if(phase && assistant && g_object_get_data(G_OBJECT(assistant),"insight-ai-busy")) return G_SOURCE_CONTINUE;
 switch(phase) {
 case 0: {
  source=document_get_current();if(!source || g_strcmp0(source->file_name,path)) return G_SOURCE_CONTINUE;
  window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));
  GtkWidget *out=named(window,"insight-ssh-output");g_assert_nonnull(out);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(out)),"source.c:1: error: expected ';' before end of input\n",-1);
  click(window,"insight-ssh-ai");assistant=named(window,"insight-ai-window");g_assert_nonnull(assistant);
  gchar *context=text(assistant,"insight-ai-context");g_assert_nonnull(strstr(context,"source.c:1"));g_free(context);
  gtk_combo_box_set_active(GTK_COMBO_BOX(named(assistant,"insight-ai-provider")),4);entry(assistant,"insight-ai-endpoint",endpoint);entry(assistant,"insight-ai-model","mock-model");entry(assistant,"insight-ai-key","fixture-secret-key");click(assistant,"insight-ai-attach");entry(assistant,"insight-ai-question","Explain this compiler error and propose a correction.");click(assistant,"insight-ai-send");phase++;break;
 }
 case 1: {
  gchar *answer=text(assistant,"insight-ai-answer");g_assert_nonnull(strstr(answer,"Mock analysis"));g_free(answer);g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(named(assistant,"insight-ai-answer"))));
  click(assistant,"insight-ai-insert");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(window,"insight-ssh-input"))),==,"printf 'review only'");source_is(base);
  gchar *config=g_build_filename(profile,"insight-ai.conf",NULL),*saved=NULL;g_assert_true(g_file_get_contents(config,&saved,NULL,NULL));g_assert_null(strstr(saved,"fixture-secret-key"));g_assert_null(strstr(saved,"compiler error"));g_assert_null(strstr(saved,"int value"));g_free(config);g_free(saved);
  after=g_get_monotonic_time()+200*G_TIME_SPAN_MILLISECOND;phase++;break;
 }
 case 2:
  if(g_get_monotonic_time()<after) { break; }
  capture(GTK_IS_WINDOW(assistant) ? assistant:window,"GEANY_CONSOLE_AI_CAPTURE");click(assistant,"insight-ai-review");diff=top("insight-diff-window");g_assert_nonnull(diff);
  GtkWidget *tabs=named(diff,"insight-diff-toolbar-tabs");g_assert_true(gtk_widget_get_ancestor(named(diff,"insight-diff-ai-question"),GTK_TYPE_NOTEBOOK)==tabs);g_assert_true(gtk_widget_get_ancestor(named(diff,"insight-diff-ai-apply"),GTK_TYPE_NOTEBOOK)==tabs);phase++;break;
 case 3: {
  if(!gtk_widget_get_sensitive(named(diff,"insight-diff-ai-apply"))) break;
  gchar *left=text(diff,"insight-diff-left"),*right=text(diff,"insight-diff-right"),*explanation=text(diff,"insight-diff-ai-explanation");g_assert_cmpstr(left,==,base);g_assert_cmpstr(right,==,proposal);g_assert_nonnull(strstr(explanation,"Mock analysis"));g_free(left);g_free(right);g_free(explanation);
  g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(diff,"insight-diff-ai-tools"))));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(diff,"insight-diff-ai-tools")),TRUE);g_assert_true(gtk_widget_get_visible(gtk_widget_get_parent(named(diff,"insight-diff-replacement"))));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(diff,"insight-diff-ai-tools")),FALSE);click(diff,"insight-diff-all-right");entry(diff,"insight-diff-ai-question","Why is this terminator required?");click(diff,"insight-diff-ai-ask");after=g_get_monotonic_time()+200*G_TIME_SPAN_MILLISECOND;phase++;break;
 }
 case 4: {
  if(g_get_monotonic_time()<after) { break; }
  gtk_window_present(GTK_WINDOW(diff));after=g_get_monotonic_time()+200*G_TIME_SPAN_MILLISECOND;phase++;break;
 }
 case 5: {
  if(g_get_monotonic_time()<after) { break; }
  capture(diff,"GEANY_AI_REVIEW_CAPTURE");source_is(base);
  sci_set_text(source->editor->sci,"later source edit\n");click(diff,"insight-diff-ai-apply");source_is("later source edit\n");SEND(source->editor->sci,SCI_UNDO,0,0);source_is(base);
  SEND(source->editor->sci,SCI_SETREADONLY,TRUE,0);click(diff,"insight-diff-ai-apply");source_is(base);SEND(source->editor->sci,SCI_SETREADONLY,FALSE,0);
  click(diff,"insight-diff-ai-apply");source_is(proposal);SEND(source->editor->sci,SCI_UNDO,0,0);source_is(base);SEND(source->editor->sci,SCI_REDO,0,0);source_is(proposal);
  gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,base);g_free(disk);gtk_widget_destroy(diff);diff=NULL;
  click(assistant,"insight-ai-review");diff=top("insight-diff-window");g_assert_nonnull(diff);click(diff,"insight-diff-cancel");phase++;break;
 }
 case 6:
  if(gtk_widget_get_sensitive(named(diff,"insight-diff-cancel"))) break;
  g_assert_false(gtk_widget_get_sensitive(named(diff,"insight-diff-ai-apply")));gtk_widget_destroy(diff);diff=NULL;entry(assistant,"insight-ai-model","slow");click(assistant,"insight-ai-send");click(assistant,"insight-ai-stop");phase++;break;
 case 7:
  g_assert_false(gtk_widget_get_sensitive(named(assistant,"insight-ai-review")));gtk_widget_destroy(assistant);assistant=NULL;
#ifdef HAVE_VTE
  GtkWidget *terminal=named(window,"insight-local-terminal");g_assert_nonnull(terminal);GList *menus=gtk_menu_get_for_attach_widget(terminal);g_assert_nonnull(menus);GtkWidget *item=named(menus->data,"insight-terminal-ai");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));assistant=top("insight-ai-window");g_assert_nonnull(assistant);g_assert_nonnull(named(assistant,"insight-ai-context"));gtk_widget_destroy(assistant);assistant=NULL;
#endif
  gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-ai-menu")));assistant=top("insight-ai-window");g_assert_nonnull(assistant);entry(assistant,"insight-ai-model","slow");entry(assistant,"insight-ai-question","Cancel safely on close");click(assistant,"insight-ai-send");gtk_widget_destroy(assistant);assistant=NULL;after=g_get_monotonic_time()+300*G_TIME_SPAN_MILLISECOND;phase++;break;
 case 8:
  if(g_get_monotonic_time()<after) { break; }
  document_set_text_changed(source,FALSE);g_subprocess_force_exit(server);g_subprocess_wait(server,NULL,NULL);g_object_unref(server);g_print("Console AI native GUI: SSH output, compatible API, key privacy, explicit source attachment, command insertion without execution, Diff/Merge explanation and follow-up, stale/read-only guards, one Undo, unchanged disk, cancellation, local VTE and close while pending passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer p) { (void)p;g_error("Console AI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) { return 77; }
 g_assert_cmpint(argc,==,2);GError *error=NULL;server=g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE,&error,"python3","-I",argv[1],NULL);g_assert_no_error(error);GDataInputStream *stream=g_data_input_stream_new(g_subprocess_get_stdout_pipe(server));gchar *port=g_data_input_stream_read_line(stream,NULL,NULL,&error);g_assert_no_error(error);g_assert_nonnull(port);endpoint=g_strdup_printf("http://127.0.0.1:%s/v1/chat/completions",port);g_free(port);g_object_unref(stream);
 gchar *dir=g_dir_make_tmp("console-ai-native-XXXXXX",NULL);profile=g_build_filename(dir,"profile",NULL);path=g_build_filename(dir,"source.c",NULL);g_assert_true(g_file_set_contents(path,base,-1,NULL));gchar *args[]={"console-ai-native","--new-instance","--no-session","--config",profile,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(100,step,NULL);g_timeout_add_seconds(50,timeout,NULL);return main_lib(6,args);
}
