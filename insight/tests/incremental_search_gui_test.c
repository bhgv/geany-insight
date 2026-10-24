/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "search.h"
#include "ui_utils.h"
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GeanyDocument *doc;
static GtkWidget *window,*strip,*query;
static gchar *path,*text,*dirty;
static guint phase;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *f=named(menu,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p&&!found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static void activate(GeanyDocument *source) {
 GtkWidget *child=GTK_WIDGET(source->editor->sci);while(gtk_widget_get_parent(child)) { GtkWidget *parent=gtk_widget_get_parent(child);if(GTK_IS_NOTEBOOK(parent)) gtk_notebook_set_current_page(GTK_NOTEBOOK(parent),gtk_notebook_page_num(GTK_NOTEBOOK(parent),child));child=parent; }gtk_widget_grab_focus(GTK_WIDGET(source->editor->sci));
}
static void selection(gint anchor,gint caret) { SEND(doc->editor->sci,SCI_SETSEL,anchor,caret); }
static void range(gint begin,gint end) { g_assert_cmpint(SEND(doc->editor->sci,SCI_GETSELECTIONSTART,0,0),==,begin);g_assert_cmpint(SEND(doc->editor->sci,SCI_GETSELECTIONEND,0,0),==,end); }
static void start(gboolean back) { keybindings_send_command(GEANY_KEY_GROUP_SEARCH,back ? GEANY_KEYS_INSIGHT_INCREMENTALBACK:GEANY_KEYS_INSIGHT_INCREMENTAL);g_assert_true(gtk_widget_get_visible(strip));g_assert_true(gtk_widget_is_focus(query)); }
static void pattern(const gchar *value) { gtk_entry_set_text(GTK_ENTRY(query),value);gtk_editable_set_position(GTK_EDITABLE(query),-1); }
static void button(const gchar *suffix) { gchar *name=g_strconcat("insight-incremental-",suffix,NULL);GtkWidget *widget=named(window,name);g_free(name);g_assert_nonnull(widget);gtk_button_clicked(GTK_BUTTON(widget)); }
static gboolean key(guint val,guint state) {
 GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(window));event->key.keyval=val;event->key.state=state;event->key.time=GDK_CURRENT_TIME;gdk_event_set_device(event,gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default())));gboolean handled=FALSE;g_signal_emit_by_name(window,"key-press-event",event,&handled);gdk_event_free(event);return handled;
}
static void same_text(const gchar *expected) { gchar *actual=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(actual,==,expected);g_free(actual); }
static gboolean tick(gpointer data) {
 (void)data;
 if(phase++==0) { doc=document_get_current();if(!DOC_VALID(doc) || g_strcmp0(doc->file_name,path)) { phase=0;return G_SOURCE_CONTINUE; }window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));return G_SOURCE_CONTINUE; }
 strip=named(window,"insight-incremental-strip");query=named(window,"insight-incremental-query");g_assert_nonnull(strip);g_assert_nonnull(query);g_assert_false(gtk_widget_get_visible(strip));
 SEND(doc->editor->sci,SCI_APPENDTEXT,strlen("dirty\n"),(sptr_t)"dirty\n");g_assert_true(doc->changed);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"incremental-clipboard",-1);
 selection(18,12);SEND(doc->editor->sci,SCI_SETFIRSTVISIBLELINE,80,0);gint top=SEND(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0);gchar *old_pattern=g_strdup(search_data.text);
 gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-incremental-command")));g_assert_true(gtk_widget_is_focus(query));pattern("alpha");range(12,17);pattern("alphaZZ");range(12,17);g_assert_true(key(GDK_KEY_Escape,0));g_assert_false(gtk_widget_get_visible(strip));g_assert_cmpint(SEND(doc->editor->sci,SCI_GETANCHOR,0,0),==,18);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,12);g_assert_cmpint(SEND(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0),==,top);g_assert_cmpstr(search_data.text,==,old_pattern);g_free(old_pattern);
 selection(0,0);start(FALSE);pattern("alpha");range(0,5);button("next");range(6,11);button("previous");range(0,5);pattern("ALPHA");range(6,11);pattern("ALPHAX");range(6,11);g_signal_emit_by_name(query,"backspace");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(query)),==,"ALPHA");range(6,11);g_assert_true(key(GDK_KEY_Return,0));g_assert_false(gtk_widget_get_visible(strip));g_assert_cmpstr(search_data.text,==,"ALPHA");g_assert_true(search_data.flags&GEANY_FIND_MATCHCASE);
 selection(0,0);g_assert_true(key(GDK_KEY_F12,GDK_CONTROL_MASK));g_assert_true(gtk_widget_get_visible(strip));g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(query)),==,"");g_assert_true(key(GDK_KEY_F12,GDK_CONTROL_MASK));range(6,11);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(query)),==,"ALPHA");button("cancel");range(0,0);
 gint lower=strstr(dirty,"привет")-dirty,upper=strstr(dirty,"Привет")-dirty;
 selection(0,0);start(FALSE);pattern("привет");range(lower,lower+strlen("привет"));pattern("Привет");range(upper,upper+strlen("Привет"));button("accept");
 selection(strlen(dirty),strlen(dirty));start(TRUE);pattern("alpha");range(12,17);g_assert_true(key(GDK_KEY_F12,GDK_CONTROL_MASK|GDK_SHIFT_MASK));range(6,11);button("accept");
 selection(strlen(dirty),strlen(dirty));start(FALSE);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-incremental-wrap")),FALSE);pattern("ALPHA");range(strlen(dirty),strlen(dirty));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-incremental-wrap")),TRUE);range(6,11);button("cancel");range(strlen(dirty),strlen(dirty));
 selection(0,0);start(FALSE);pattern("~needle");gint literal=strstr(dirty,"~needle")-dirty;range(literal,literal+7);button("accept");keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_SEARCH_FINDNEXT);range(literal+8,literal+15);
 selection(0,0);start(FALSE);pattern("alpha");g_assert_true(key(GDK_KEY_f,GDK_CONTROL_MASK));g_assert_false(gtk_widget_get_visible(strip));GtkWidget *find=gtk_window_get_focus(GTK_WINDOW(window));g_assert_true(find!=query);/* Close native Find if this platform opened it. */
 GList *windows=gtk_window_list_toplevels();for(GList *p=windows;p;p=p->next) if(GTK_IS_DIALOG(p->data)) gtk_widget_hide(p->data);g_list_free(windows);activate(doc);
 start(FALSE);pattern("alpha");gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));g_assert_false(gtk_widget_get_visible(strip));
 keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_BROWSERMODE);g_assert_true(doc->readonly);start(FALSE);pattern("ALPHA");range(6,11);button("cancel");g_assert_true(doc->readonly);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_BROWSERMODE);
 SEND(doc->editor->sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_INSIGHT_INCREMENTAL);g_assert_false(gtk_widget_get_visible(strip));SEND(doc->editor->sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);
 start(FALSE);pattern("alpha");GeanyDocument *other=document_new_file(NULL,filetypes_lookup_by_name("None"),"alpha beta");g_assert_false(gtk_widget_get_visible(strip));start(FALSE);g_assert_true(gtk_widget_get_visible(strip));SEND(other->editor->sci,SCI_APPENDTEXT,1,(sptr_t)"!");g_assert_false(gtk_widget_get_visible(strip));document_set_text_changed(other,FALSE);g_assert_true(document_close(other));activate(doc);
 other=document_new_file(NULL,filetypes_lookup_by_name("None"),"alpha beta");start(FALSE);pattern("alpha");document_set_text_changed(other,FALSE);g_assert_true(document_close(other));g_assert_false(gtk_widget_get_visible(strip));activate(doc);
 other=document_new_file(NULL,filetypes_lookup_by_name("None"),"x");SEND(other->editor->sci,SCI_APPENDTEXT,3,(sptr_t)"x\0y");keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_INSIGHT_INCREMENTAL);g_assert_false(gtk_widget_get_visible(strip));document_set_text_changed(other,FALSE);g_assert_true(document_close(other));activate(doc);
 same_text(dirty);g_assert_true(doc->changed);SEND(doc->editor->sci,SCI_UNDO,0,0);same_text(text);SEND(doc->editor->sci,SCI_REDO,0,0);same_text(dirty);gchar *clipboard=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clipboard,==,"incremental-clipboard");g_free(clipboard);
 document_set_text_changed(doc,FALSE);g_print("Incremental search: live entry, smart/Unicode case, failure/backspace, bidirectional repeat/previous pattern, wrap, Escape reverse selection/viewport, Enter/F3 literal sharing, configured keys/native command focus, Browser Mode, rectangle/NUL/edit/switch/close guards, text/Undo/clipboard preserved\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Incremental search timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("incremental-search-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL);g_assert_cmpint(g_mkdir(profile,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL),*keys=g_build_filename(profile,"keybindings.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(keys,"[Bindings]\ninsight_incremental_search=<Control>F12\ninsight_incremental_search_backward=<Control><Shift>F12\n",-1,NULL));
 GString *body=g_string_new("alpha ALPHA alphabet\nпривет Привет\n~needle ~needle\n");for(guint n=0;n<500;n++) g_string_append_printf(body,"filler %03u\n",n);text=g_string_free(body,FALSE);dirty=g_strconcat(text,"dirty\n",NULL);path=g_build_filename(dir,"source.txt",NULL);g_assert_true(g_file_set_contents(path,text,-1,NULL));
 gchar *args[]={"incremental-search","--new-instance","--no-session","--config",profile,path,NULL};g_timeout_add(250,tick,NULL);g_timeout_add_seconds(40,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,args);gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,text);g_free(disk);return result;
}
