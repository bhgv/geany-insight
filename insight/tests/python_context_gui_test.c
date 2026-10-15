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
static GtkWidget *window;static gchar *path;static guint phase;static GeanyDocument *source;
static const gchar *code="value = 99\nclass Alpha:\n    value = 1\n    def method(self, arg: int):\n        local = arg\n        def inner():\n            return local\n        return self.value + local\nclass Beta:\n    value = 2\n    def method(self):\n        return self.value\nvalue += 1\ndef helper():\n    return 1\nhelper()\nAlpha.value\nclass Closure:\n    captured = 3\n    def method(self):\n        def nested():\n            return self.captured\n        def shadow(self):\n            return self.captured\n        def rebound():\n            self = object()\n            return self.captured\n        def forward():\n            return self.captured\n            self = object()\n        return nested()\ndef outer():\n    def fake(self):\n        return self.captured\nshared = 40\ndef binding_outer():\n    shared = 41\n    def take_global():\n        global shared\n        shared = 42\n        return shared # module_binding\n    def take_nonlocal():\n        nonlocal shared\n        shared = 43\n        return shared # outer_binding\n    def forward_local():\n        return shared # forward_binding\n        shared = 44\n    def invalid_nonlocal():\n        nonlocal missing\n        return missing # missing_binding\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=list;p && !found;p=p->next) found=named(p->data,name);g_list_free(list);return found; }return NULL; }
static void jump_position(ScintillaObject *preview,gint pos) {sci_set_current_position(preview,pos,FALSE);GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(preview)));event->key.keyval=GDK_KEY_F12;gtk_widget_event(GTK_WIDGET(preview),event);gdk_event_free(event); }
static void jump(ScintillaObject *preview,const gchar *needle,guint skip) { jump_position(preview,strstr(code,needle)-code+skip); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!phase) { document_open_file(path,FALSE,NULL,NULL);source=document_get_current();window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));phase++;return G_SOURCE_CONTINUE; }
 ScintillaObject *preview=SCINTILLA(named(window,"insight-context-code"));
 if(phase==1) { if(sci_get_length(preview)!=(gint)strlen(code) || !SEND(preview,SCI_INDICATORVALUEAT,31,strstr(code,"self.value +")-code+5)) { static guint ticks;if(++ticks%8==0) g_printerr("Python preview length=%d expected=%u title=%s tags=%u\n",sci_get_length(preview),(guint)strlen(code),gtk_label_get_text(GTK_LABEL(named(window,"insight-context-title"))),source->tm_file ? source->tm_file->tags_array->len : 0);return G_SOURCE_CONTINUE; }
  g_assert_false(gtk_widget_get_visible(named(window,"insight-context-search-row")));g_assert_false(gtk_widget_get_visible(named(window,"insight-context-definitions-row")));
  GtkWidget *search=named(window,"insight-context-search-toggle"),*definitions=named(window,"insight-context-definitions-toggle");g_assert_nonnull(gtk_button_get_image(GTK_BUTTON(search)));gchar *tip=gtk_widget_get_tooltip_text(search);g_assert_nonnull(strstr(tip,"search"));g_free(tip);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(search),TRUE);g_assert_true(gtk_widget_get_visible(named(window,"insight-context-search-row")));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(definitions),TRUE);g_assert_true(gtk_widget_get_visible(named(window,"insight-context-definitions-row")));gint minimum=0,natural=0;gtk_widget_get_preferred_width(named(window,"insight-context-definitions"),&minimum,&natural);g_assert_cmpint(minimum,<,500);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(search),FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(definitions),FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-context-follow")),FALSE);jump(preview,"self.value +",5);g_assert_cmpint(sci_get_current_line(preview),==,2);phase++;
 } else if(phase==2) { jump(preview,"return self.value\n",12);g_assert_cmpint(sci_get_current_line(preview),==,9);phase++; }
 else if(phase==3) { jump(preview,"return local",7);g_assert_cmpint(sci_get_current_line(preview),==,4);g_assert_true(document_get_current()==source);g_assert_cmpint(sci_get_current_line(source->editor->sci),==,0);
  jump(preview,"local = arg",8);g_assert_cmpint(sci_get_current_line(preview),==,3);
  jump(preview,"\nhelper()",1);g_assert_cmpint(sci_get_current_line(preview),==,13);
  jump(preview,"Alpha.value",0);g_assert_cmpint(sci_get_current_line(preview),==,1);
  jump(preview,"Alpha.value",6);g_assert_cmpint(sci_get_current_line(preview),==,2);
  jump(preview,"return self.captured",12);g_assert_cmpint(sci_get_current_line(preview),==,18);
  const gchar *probe=strstr(code,"return self.captured");
  for(guint n=0;n<5;n++) {
   g_assert_nonnull(probe);gint pos=probe-code+12;
   if(n) { sci_set_current_position(preview,pos,FALSE);gint line=sci_get_current_line(preview);jump_position(preview,pos);g_assert_cmpint(sci_get_current_line(preview),==,line); }
   probe=strstr(probe+1,"return self.captured");
  }
  /* Editor Follow uses the same scoped resolver asynchronously. */
  sci_set_current_position(source->editor->sci,strstr(code,"self.value +")-code+5,FALSE);gtk_widget_grab_focus(GTK_WIDGET(source->editor->sci));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-context-follow")),TRUE);phase++;
 } else if(phase==4) { if(sci_get_current_line(preview)!=2) return G_SOURCE_CONTINUE;sci_set_current_position(source->editor->sci,strstr(code,"value +=")-code,FALSE);phase++; }
 else if(phase==5) { if(sci_get_current_line(preview)!=0) return G_SOURCE_CONTINUE;sci_set_current_position(source->editor->sci,strstr(code,"return self.captured")-code+12,FALSE);phase++; }
 else if(phase==6) { if(sci_get_current_line(preview)!=18) return G_SOURCE_CONTINUE;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-context-follow")),FALSE);
  jump(preview,"shared # module_binding",0);g_assert_cmpint(sci_get_current_line(preview),==,sci_get_line_from_position(preview,strstr(code,"shared = 40")-code));
  jump(preview,"shared # outer_binding",0);g_assert_cmpint(sci_get_current_line(preview),==,sci_get_line_from_position(preview,strstr(code,"shared = 41")-code));
  jump(preview,"shared # forward_binding",0);g_assert_cmpint(sci_get_current_line(preview),==,sci_get_line_from_position(preview,strstr(code,"shared = 44")-code));
  gint missing=strstr(code,"missing # missing_binding")-code;jump_position(preview,missing);g_assert_cmpint(sci_get_current_position(preview),==,missing);
  sci_set_current_position(source->editor->sci,strstr(code,"shared # module_binding")-code,FALSE);gtk_widget_grab_focus(GTK_WIDGET(source->editor->sci));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-context-follow")),TRUE);phase++;
 } else if(phase==7) { if(sci_get_current_line(preview)!=sci_get_line_from_position(preview,strstr(code,"shared = 40")-code)) return G_SOURCE_CONTINUE;
  sci_set_current_position(source->editor->sci,strstr(code,"shared # outer_binding")-code,FALSE);phase++;
 } else if(phase==8) { if(sci_get_current_line(preview)!=sci_get_line_from_position(preview,strstr(code,"shared = 41")-code)) return G_SOURCE_CONTINUE;g_print("Python Context GUI: self fields, distinct classes, closure locals and receivers, shadowed receivers, global/nonlocal, forward locals, module scope, preview F12, asynchronous Follow and hidden icon tools passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Python Context timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("python-context-XXXXXX",NULL),*profile=g_build_filename(directory,"profile",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));path=g_build_filename(directory,"source.py",NULL);g_assert_true(g_file_set_contents(path,code,-1,NULL));
 gchar *argv[]={"python-context-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);g_free(config);g_free(path);return result;
}
