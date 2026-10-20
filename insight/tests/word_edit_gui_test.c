/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include <gdk/gdkkeysyms.h>
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;
static ScintillaObject *sci;
static GtkWidget *named(GtkWidget *w,const gchar *name)
{
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static void action(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);g_print("Word edit: %s\n",name);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void reset(const gchar *text,gint anchor,gint caret)
{
 SEND(sci,SCI_SETREADONLY,0,0);SEND(sci,SCI_CLEARSELECTIONS,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);sci_set_text(sci,text);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);SEND(sci,SCI_SETSEL,anchor,caret);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);
}
static void text_is(const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void clipboard_is(const gchar *expected) { gchar *text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,expected);g_free(text); }
static void roundtrip(const gchar *original,const gchar *result)
{
 text_is(result);g_assert_true(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_UNDO,0,0);text_is(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);text_is(result);
}
static gboolean capture(gpointer data)
{
 GtkWidget *menu=data;GdkWindow *surface=gtk_widget_get_window(menu);g_assert_nonnull(surface);GdkPixbuf *image=gdk_pixbuf_get_from_window(surface,0,0,gtk_widget_get_allocated_width(menu),gtk_widget_get_allocated_height(menu));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_WORD_EDIT_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image);gtk_menu_popdown(GTK_MENU(menu));gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean step(gpointer data)
{
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));
 reset("αβ one\r\nlast",2,2);gtk_widget_grab_focus(GTK_WIDGET(sci));GdkEventKey event={0};event.type=GDK_KEY_PRESS;event.keyval=GDK_KEY_F12;event.state=GDK_CONTROL_MASK|GDK_SHIFT_MASK;gboolean handled=FALSE;g_signal_emit_by_name(window,"key-press-event",&event,&handled);g_assert_true(handled);clipboard_is("β");roundtrip("αβ one\r\nlast","α one\r\nlast");
 reset("αβ one\r\nlast",4,4);gtk_widget_grab_focus(GTK_WIDGET(sci));keybindings_send_command(GEANY_KEY_GROUP_CLIPBOARD,GEANY_KEYS_INSIGHT_CUTWORDLEFT);clipboard_is("αβ");roundtrip("αβ one\r\nlast"," one\r\nlast");
 reset("one  two",5,5);action("insight-cut-word-left");clipboard_is("  ");roundtrip("one  two","onetwo");
 reset("one two",0,5);action("insight-cut-word");clipboard_is("wo");roundtrip("one two","one t");
 reset("first\r\nsecond\r\nlast",12,1);action("insight-delete-line");clipboard_is("sentinel");roundtrip("first\r\nsecond\r\nlast","second\r\nlast");
 reset("one\r\nlast",6,6);gtk_widget_grab_focus(GTK_WIDGET(sci));keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_DELETELINE);clipboard_is("sentinel");roundtrip("one\r\nlast","one\r\n");
 reset("αβ\r\nlast",0,0);gtk_widget_grab_focus(GTK_WIDGET(sci));keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_DELETECHARACTER);clipboard_is("sentinel");roundtrip("αβ\r\nlast","β\r\nlast");
 reset("αβ\r\nlast",4,4);action("insight-delete-character");roundtrip("αβ\r\nlast","αβlast");clipboard_is("sentinel");
 reset("one two",7,4);action("insight-delete-character");roundtrip("one two","one ");clipboard_is("sentinel");
 reset("last",4,4);action("insight-delete-character");action("insight-cut-word");text_is("last");clipboard_is("sentinel");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("last",0,0);action("insight-cut-word-left");text_is("last");clipboard_is("sentinel");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("αβ",1,1);action("insight-cut-word");action("insight-delete-character");text_is("αβ");clipboard_is("sentinel");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("a\r\nb",2,2);action("insight-delete-character");text_is("a\r\nb");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("word",0,0);SEND(sci,SCI_SETREADONLY,1,0);action("insight-cut-word");action("insight-delete-line");text_is("word");clipboard_is("sentinel");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("word",0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);action("insight-cut-word");text_is("word");clipboard_is("sentinel");
 reset("word",0,0);SEND(sci,SCI_ADDSELECTION,2,2);action("insight-delete-character");text_is("word");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("word",0,0);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,1);action("insight-cut-word");text_is("word");SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,0);
 reset("",0,0);SEND(sci,SCI_ADDTEXT,3,(sptr_t)"a\0b");SEND(sci,SCI_SETSEL,0,3);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);action("insight-delete-character");g_assert_cmpint(SEND(sci,SCI_GETLENGTH,0,0),==,3);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));clipboard_is("sentinel");
 gchar *large=g_strnfill(1024*1024+1,'a');reset(large,0,0);action("insight-cut-word");text_is(large);clipboard_is("sentinel");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));g_free(large);
 reset("",0,0);action("insight-cut-word");action("insight-cut-word-left");action("insight-delete-line");action("insight-delete-character");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));clipboard_is("sentinel");
 g_print("Word editing GUI: native UTF-8/run ranges, CRLF, active/reverse selections, clipboard preservation, one Undo/Redo, EOF/BOF, unsupported modes, read-only and invalid/oversize ranges passed\n");
 if(g_getenv("GEANY_WORD_EDIT_CAPTURE")) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(named(window,"insight-word-editing")));GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(sci)));event->button.button=3;event->button.time=GDK_CURRENT_TIME;gdk_event_set_device(event,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));gtk_menu_popup_at_widget(GTK_MENU(menu),GTK_WIDGET(sci),GDK_GRAVITY_CENTER,GDK_GRAVITY_CENTER,event);gdk_event_free(event);g_timeout_add(200,capture,menu);return G_SOURCE_REMOVE; }
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Word editing timeout");return G_SOURCE_REMOVE; }
int main(void)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("word-edit-XXXXXX",NULL),*config=g_build_filename(directory,"geany.conf",NULL),*source=g_build_filename(directory,"source.txt",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(source,"word",-1,NULL));gchar *keys=g_build_filename(directory,"keybindings.conf",NULL);g_assert_true(g_file_set_contents(keys,"[Bindings]\ninsight_cut_word=<Control><Shift>F12\n",-1,NULL));g_free(keys);gchar *args[]={"word-edit-test","--new-instance","--no-session","--config",directory,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(6,args);
}
