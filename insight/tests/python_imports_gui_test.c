/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;
static gchar *client_path,*module_path,*other_path;
static GeanyDocument *client;
static guint phase;
static const gchar *code="from models import work as task, Thing as Alias\nimport models as mod\nfrom .models import work as relative_task\nfrom absent import work as missing_task\ntask()\nmod.work()\nAlias.field\nrelative_task()\nmissing_task()\ndef shadow(task):\n    return task()\n";
static const gchar *module_code="def work():\n    return 1\nclass Thing:\n    field = 7\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }
 return NULL;
}
static void follow(const gchar *needle,guint offset) {
 sci_set_current_position(client->editor->sci,strstr(code,needle)-code+offset,FALSE);
 gtk_widget_grab_focus(GTK_WIDGET(client->editor->sci));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-context-follow")),TRUE);
}
static gboolean target_is(ScintillaObject *preview,gint line) {
 return sci_get_length(preview)==(gint)strlen(module_code) && sci_get_current_line(preview)==line && strstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-context-title"))),module_path)!=NULL;
}
static void selected_is(ScintillaObject *preview,const gchar *expected) {
 gchar text[64]={0};g_assert_cmpint(SEND(preview,SCI_GETSELTEXT,0,0),<,sizeof text);SEND(preview,SCI_GETSELTEXT,0,(sptr_t)text);g_assert_cmpstr(text,==,expected);
}
static gboolean step(gpointer data) {
 (void)data;
 if(!phase) {
  document_open_file(module_path,FALSE,NULL,NULL);document_open_file(other_path,FALSE,NULL,NULL);client=document_open_file(client_path,FALSE,NULL,NULL);g_assert_nonnull(client);
  window=gtk_widget_get_toplevel(GTK_WIDGET(client->editor->sci));phase++;return G_SOURCE_CONTINUE;
 }
 ScintillaObject *preview=SCINTILLA(named(window,"insight-context-code"));
 if(phase==1) {
  gint task=strstr(code,"\ntask()")-code+1,field=strstr(code,"Alias.field")-code+6;
  if(sci_get_length(preview)!=(gint)strlen(code) || !SEND(preview,SCI_INDICATORVALUEAT,31,task) || !SEND(preview,SCI_INDICATORVALUEAT,31,field)) return G_SOURCE_CONTINUE;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-context-follow")),FALSE);sci_set_current_position(preview,task,FALSE);
  GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(preview)));event->key.keyval=GDK_KEY_F12;gtk_widget_event(GTK_WIDGET(preview),event);gdk_event_free(event);
  g_assert_true(target_is(preview,0));selected_is(preview,"work");g_assert_true(document_get_current()==client);follow("mod.work()",4);phase++;
 }else if(phase==2) { if(!target_is(preview,0)) return G_SOURCE_CONTINUE;follow("Alias.field",6);phase++; }
 else if(phase==3) { if(!target_is(preview,3)) return G_SOURCE_CONTINUE;selected_is(preview,"field");follow("relative_task()",0);phase++; }
 else if(phase==4) { if(!target_is(preview,0)) return G_SOURCE_CONTINUE;selected_is(preview,"work");follow("missing_task()",0);phase++; }
 else if(phase==5) {
  const gchar *title=gtk_label_get_text(GTK_LABEL(named(window,"insight-context-title")));
  if(sci_get_length(preview)!=(gint)strlen(code) || !strstr(title,"no indexed definition") || !strstr(title,client_path)) return G_SOURCE_CONTINUE;
  follow("return task()",7);phase++;
 }else if(phase==6) {
  if(sci_get_length(preview)!=(gint)strlen(code) || sci_get_current_line(preview)!=9) return G_SOURCE_CONTINUE;
  g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-context-title"))),client_path));
  follow("\ntask()",1);phase++;
 }else if(phase==7) {
  if(!target_is(preview,0)) return G_SOURCE_CONTINUE;
  GeanyDocument *module=document_find_by_filename(module_path);g_assert_nonnull(module);
  gchar *changed=g_strconcat("# shifted definition\n",module_code,NULL);sci_set_text(module->editor->sci,changed);g_free(changed);phase++;
 }else if(phase==8) {
  if(sci_get_length(preview)!=(gint)(strlen(module_code)+strlen("# shifted definition\n")) || sci_get_current_line(preview)!=1) { static guint ticks;if(++ticks%12==0) g_printerr("Updated import length=%d line=%d follow=%d title=%s\n",sci_get_length(preview),sci_get_current_line(preview),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(window,"insight-context-follow"))),gtk_label_get_text(GTK_LABEL(named(window,"insight-context-title"))));return G_SOURCE_CONTINUE; }
  selected_is(preview,"work");document_set_text_changed(document_find_by_filename(module_path),FALSE);
  g_print("Python imports GUI: explicit aliases, module members, class fields, relative imports, missing modules, local shadows, resolved-name selection, live index updates, F12 and Follow passed\n");
  gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Python imports timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("python-imports-gui-XXXXXX",NULL),*profile=g_build_filename(directory,"profile",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 client_path=g_build_filename(directory,"client.py",NULL);module_path=g_build_filename(directory,"models.py",NULL);other_path=g_build_filename(directory,"other.py",NULL);
 g_assert_true(g_file_set_contents(client_path,code,-1,NULL));g_assert_true(g_file_set_contents(module_path,module_code,-1,NULL));g_assert_true(g_file_set_contents(other_path,"def work():\n    return 2\nclass Thing:\n    field = 99\n",-1,NULL));
 gchar *argv[]={"python-imports-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);
 g_free(client_path);g_free(module_path);g_free(other_path);g_free(directory);g_free(profile);g_free(config);return result;
}
