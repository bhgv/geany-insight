/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include <gdk/gdkkeysyms.h>
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static GeanyDocument *source,*locked,*special,*fresh,*cloned;static gchar *path,*locked_path,*saved_path,*config;static guint phase;static gint call;
static const gchar *text="int helper(int x) { return x+1; }\nint caller(void) { return helper(2); }\n// helper comment\nconst char *s = \"helper\";\n";
static const gchar *dirty="int helper(int x) { return x+1; }\nint caller(void) { return helper(2); }\n// helper comment\nconst char *s = \"helper\";\n// unsaved\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *c=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=c;p&&!f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
static void activate(GeanyDocument *doc) { GtkWidget *child=GTK_WIDGET(doc->editor->sci);while(gtk_widget_get_parent(child)) { GtkWidget *parent=gtk_widget_get_parent(child);if(GTK_IS_NOTEBOOK(parent)) gtk_notebook_set_current_page(GTK_NOTEBOOK(parent),gtk_notebook_page_num(GTK_NOTEBOOK(parent),child));child=parent; }gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci)); }
static void check(GeanyDocument *doc,const gchar *expected) { gchar *actual=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(actual,==,expected);g_free(actual); }
static void caret(gint pos) { SEND(source->editor->sci,SCI_CLEARSELECTIONS,0,0);SEND(source->editor->sci,SCI_SETSEL,pos,pos);gtk_widget_grab_focus(GTK_WIDGET(source->editor->sci)); }
static void key(guint val) { GdkEventKey event={0};event.type=GDK_KEY_PRESS;event.keyval=val;gboolean handled=FALSE;g_signal_emit_by_name(val==GDK_KEY_BackSpace ? window:GTK_WIDGET(source->editor->sci),"key-press-event",&event,&handled);g_assert_true(handled); }
static void click_at(gint pos,guint state) {
 caret(pos);GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);event->button.button=1;event->button.state=state;event->button.time=GDK_CURRENT_TIME;event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(source->editor->sci)));event->button.x=SEND(source->editor->sci,SCI_POINTXFROMPOSITION,0,pos)+1;event->button.y=SEND(source->editor->sci,SCI_POINTYFROMPOSITION,0,pos)+2;
 gdk_event_set_device(event,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));gboolean handled=FALSE;g_signal_emit_by_name(source->editor->sci,"button-press-event",event,&handled);event->type=GDK_BUTTON_RELEASE;g_signal_emit_by_name(source->editor->sci,"button-release-event",event,&handled);gdk_event_free(event);
}
static void mode(gboolean on) { gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-browser-mode")),on);g_assert_cmpint(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(window,"insight-browser-mode-status"))),==,on);g_assert_cmpint(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(named(g_object_get_data(G_OBJECT(window),"insight-editor-menu"),"insight-browser-mode-popup"))),==,on); }
static gboolean tick(gpointer data) {
 (void)data;
 if(!phase) {
  source=document_get_current();if(!source || g_strcmp0(source->file_name,path)) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));
  g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(named(window,"insight-browser-mode"))));
  locked=document_open_file(locked_path,TRUE,NULL,NULL);g_assert_nonnull(locked);special=document_new_file(NULL,NULL,"special\n");SEND(special->editor->sci,SCI_SETREADONLY,TRUE,0);activate(source);
  SEND(source->editor->sci,SCI_APPENDTEXT,strlen("// unsaved\n"),(sptr_t)"// unsaved\n");check(source,dirty);g_assert_true(source->changed);g_assert_true(SEND(source->editor->sci,SCI_CANUNDO,0,0));
  call=(gint)(strstr(dirty,"helper(2)")-dirty)+1;caret(call);mode(TRUE);g_assert_true(source->readonly);g_assert_true(locked->readonly);g_assert_true(SEND(source->editor->sci,SCI_GETREADONLY,0,0));
  SEND(source->editor->sci,SCI_REPLACESEL,0,(sptr_t)"BAD");check(source,dirty);g_assert_false(SEND(source->editor->sci,SCI_CANUNDO,0,0));phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==1) {
  caret(call);key(GDK_KEY_space);if(sci_get_current_line(source->editor->sci)!=0) return G_SOURCE_CONTINUE;
  key(GDK_KEY_BackSpace);g_assert_cmpint(sci_get_current_position(source->editor->sci),==,call);check(source,dirty);
  GtkWidget *context=named(window,"insight-context-code");g_assert_nonnull(context);SEND(SCINTILLA(context),SCI_SETSEL,4,10);
  click_at(call,0);g_assert_cmpint(sci_get_current_line(source->editor->sci),==,0);g_assert_cmpint(sci_get_current_position(source->editor->sci),==,10);key(GDK_KEY_BackSpace);g_assert_cmpint(sci_get_current_position(source->editor->sci),==,call);
  click_at(call,GEANY_PRIMARY_MOD_MASK);g_assert_cmpint(sci_get_current_line(source->editor->sci),==,1);caret(call);
  gint comment=(gint)(strstr(dirty,"helper comment")-dirty)+1;click_at(comment,0);g_assert_cmpint(sci_get_current_line(source->editor->sci),==,2);key(GDK_KEY_space);g_assert_cmpint(sci_get_current_line(source->editor->sci),==,2);
  gint quoted=(gint)(strstr(dirty,"\"helper\"")-dirty)+2;click_at(quoted,0);g_assert_cmpint(sci_get_current_line(source->editor->sci),==,3);check(source,dirty);
  GtkWidget *readonly=ui_lookup_widget(window,"set_file_readonly1");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(readonly),TRUE);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(readonly),FALSE);g_assert_true(source->readonly);g_assert_true(SEND(source->editor->sci,SCI_GETREADONLY,0,0));
  fresh=document_new_file(NULL,filetypes_lookup_by_name("C"),"int fresh(void) { return 1; }\n");g_assert_true(fresh->readonly);g_assert_true(SEND(fresh->editor->sci,SCI_GETREADONLY,0,0));check(fresh,"int fresh(void) { return 1; }\n");g_assert_true(document_save_file_as(fresh,saved_path));g_assert_true(fresh->readonly);g_assert_true(SEND(fresh->editor->sci,SCI_GETREADONLY,0,0));
  g_assert_true(document_reload_force(fresh,NULL));g_assert_true(fresh->readonly);check(fresh,"int fresh(void) { return 1; }\n");
  gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"clone1")));cloned=document_get_current();g_assert_true(cloned!=fresh);g_assert_true(cloned->readonly);check(cloned,"int fresh(void) { return 1; }\n");
  mode(FALSE);g_assert_false(source->readonly);g_assert_false(SEND(source->editor->sci,SCI_GETREADONLY,0,0));g_assert_true(locked->readonly);g_assert_true(SEND(locked->editor->sci,SCI_GETREADONLY,0,0));g_assert_false(special->readonly);g_assert_true(SEND(special->editor->sci,SCI_GETREADONLY,0,0));g_assert_false(fresh->readonly);g_assert_false(cloned->readonly);g_assert_false(SEND(fresh->editor->sci,SCI_GETREADONLY,0,0));
  activate(source);check(source,dirty);g_assert_true(source->changed);SEND(source->editor->sci,SCI_UNDO,0,0);check(source,text);SEND(source->editor->sci,SCI_REDO,0,0);check(source,dirty);g_assert_true(source->changed);
  gtk_button_clicked(GTK_BUTTON(named(window,"insight-browser-mode-status")));g_assert_true(source->readonly);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_BROWSERMODE);g_assert_false(source->readonly);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_BROWSERMODE);g_assert_true(source->readonly);
  document_set_text_changed(source,FALSE);document_set_text_changed(cloned,FALSE);document_set_text_changed(special,FALSE);
  g_print("Browser Mode: menus/status/keybinding, all/new/read-only buffers, single-click/Space/Backspace, Ctrl selection, comment/string guards, writable-state/session isolation, Save As/reload/cloned and original Undo/Redo passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Browser Mode timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("insight-browser-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL);g_assert_cmpint(g_mkdir(profile,0700),==,0);path=g_build_filename(dir,"source.c",NULL);locked_path=g_build_filename(dir,"locked.c",NULL);saved_path=g_build_filename(dir,"saved.c",NULL);config=g_build_filename(profile,"geany.conf",NULL);
 g_assert_true(g_file_set_contents(path,text,-1,NULL));g_assert_true(g_file_set_contents(locked_path,"int locked;\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nshow_keep_edit_history_on_reload_msg=false\n",-1,NULL));
 gchar *args[]={"browser-mode","--new-instance","--config",profile,path,NULL};g_timeout_add(200,tick,NULL);g_timeout_add_seconds(40,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,args);
 gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,text);g_free(disk);
 GKeyFile *saved=g_key_file_new();g_assert_true(g_key_file_load_from_file(saved,g_build_filename(profile,"session.conf",NULL),G_KEY_FILE_NONE,NULL));gchar **keys=g_key_file_get_keys(saved,"files",NULL,NULL);gboolean source_seen=FALSE,locked_seen=FALSE;
 for(guint n=0;keys && keys[n];n++) if(g_str_has_prefix(keys[n],"FILE_NAME_")) { gchar *entry=g_key_file_get_string(saved,"files",keys[n],NULL);gchar **fields=g_strsplit(entry,";",-1);if(g_strv_length(fields)>=8) { gchar *filename=g_uri_unescape_string(fields[7],NULL);if(!g_strcmp0(filename,path)) { source_seen=TRUE;g_assert_cmpstr(fields[2],==,"0"); }if(!g_strcmp0(filename,locked_path)) { locked_seen=TRUE;g_assert_cmpstr(fields[2],==,"1"); }g_free(filename); }g_strfreev(fields);g_free(entry); }
 g_assert_true(source_seen);g_assert_true(locked_seen);g_strfreev(keys);g_key_file_unref(saved);return result;
}
