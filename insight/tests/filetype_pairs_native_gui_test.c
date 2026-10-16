/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "keybindings.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static gchar *profile,*source;static GeanyDocument *doc;
static void text_is(const gchar *expected) { gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+2);g_assert_cmpstr(text,==,expected);g_free(text); }
static void reset(void) { ScintillaObject *sci=doc->editor->sci;SEND(sci,SCI_SETREADONLY,0,0);SEND(sci,SCI_SETTEXT,0,(sptr_t)"");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSEL,0,0); }
static void typed(gchar ch) {
 ScintillaObject *sci=doc->editor->sci;gchar text[]={ch,0};SEND(sci,SCI_BEGINUNDOACTION,0,0);SEND(sci,SCI_REPLACESEL,0,(sptr_t)text);SCNotification nt={0};nt.nmhdr.code=SCN_CHARADDED;nt.ch=ch;g_signal_emit_by_name(sci,"sci-notify",0,&nt);SEND(sci,SCI_ENDUNDOACTION,0,0);
}
static gboolean key(guint value,GdkModifierType state) {
 ScintillaObject *sci=doc->editor->sci;GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(sci)));event->key.send_event=TRUE;event->key.keyval=value;event->key.state=state;gboolean handled=FALSE;g_signal_emit_by_name(sci,"key-press-event",&event->key,&handled);gdk_event_free(event);return handled;
}
static void undo(void) { keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_UNDO); }
static void redo(void) { keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_REDO); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;doc=document_open_file(source,FALSE,NULL,NULL);g_assert_nonnull(doc);ScintillaObject *sci=doc->editor->sci;
 const gchar *opens="([{\"'",*closes=")]}\"'";
 for(guint i=0;i<5;i++) {
  reset();typed(opens[i]);gchar pair[]={opens[i],closes[i],0};text_is(pair);g_assert_cmpint(sci_get_current_position(sci),==,1);
  typed('x');gchar filled[]={opens[i],'x',closes[i],0};text_is(filled);g_assert_true(key(gdk_unicode_to_keyval(closes[i]),0));text_is(filled);g_assert_cmpint(sci_get_current_position(sci),==,3);undo();text_is(pair);undo();text_is("");
  reset();typed(opens[i]);g_assert_true(key(GDK_KEY_BackSpace,0));text_is("");undo();text_is(pair);redo();text_is("");
 }
 reset();typed('(');typed('[');text_is("([])");g_assert_true(key(GDK_KEY_bracketright,0));g_assert_true(key(GDK_KEY_parenright,GDK_SHIFT_MASK));text_is("([])");g_assert_cmpint(sci_get_current_position(sci),==,4);
 /* Opening a matching block adds one outer pair as one undo action. */
 reset();SEND(sci,SCI_SETTEXT,0,(sptr_t)"(thing)");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSEL,0,0);g_assert_true(key(GDK_KEY_parenleft,GDK_SHIFT_MASK));text_is("((thing))");g_assert_cmpint(sci_get_current_position(sci),==,1);undo();text_is("(thing)");redo();text_is("((thing))");
 /* Byte offsets follow UTF-8 inserted before and inside an existing tracked pair. */
 reset();typed('(');SEND(sci,SCI_INSERTTEXT,0,(sptr_t)"Ж");SEND(sci,SCI_SETSEL,3,3);SEND(sci,SCI_REPLACESEL,0,(sptr_t)"λ");text_is("Ж(λ)");g_assert_true(key(GDK_KEY_parenright,0));g_assert_cmpint(sci_get_current_position(sci),==,6);text_is("Ж(λ)");
 /* A manually inserted empty pair is never marked and Backspace removes one character only. */
 reset();SEND(sci,SCI_SETTEXT,0,(sptr_t)"()");SEND(sci,SCI_SETSEL,1,1);key(GDK_KEY_BackSpace,0);text_is(")");
 /* Undo invalidates transient markers, so restored text is ordinary editable text. */
 reset();typed('(');typed('x');undo();text_is("()");key(GDK_KEY_BackSpace,0);text_is(")");
 /* Read-only text cannot be removed by the shortcut. */
 reset();typed('(');SEND(sci,SCI_SETREADONLY,1,0);key(GDK_KEY_BackSpace,0);text_is("()");SEND(sci,SCI_SETREADONLY,0,0);
 reset();SEND(sci,SCI_SETSAVEPOINT,0,0);g_print("Native typing pairs: brackets/quotes, skip, empty Backspace, Undo/Redo, nested pairs, UTF-8 offsets, manual/undo/read-only guards passed\n");GtkWidget *window=gtk_widget_get_toplevel(GTK_WIDGET(sci));gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Typing pairs GUI timeout");return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 (void)argc;(void)argv;if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("insight-typing-pairs-XXXXXX",NULL);profile=g_build_filename(directory,"profile",NULL);source=g_build_filename(directory,"test.pairs",NULL);gchar *folder=g_build_filename(profile,"languages","Pairs",NULL),*config=g_build_filename(folder,"language.conf",NULL),*css=g_build_filename(folder,"style.css",NULL),*prefs=g_build_filename(profile,"geany.conf",NULL);
 g_assert_cmpint(g_mkdir_with_parents(folder,0700),==,0);g_assert_true(g_file_set_contents(config,"[language]\nname=Pairs\nparser=C++\nfile-filters=*.pairs\nextensions=pairs\n[editing]\nversion=1\nenabled=true\ntyping-shortcuts=true\n",-1,NULL));g_assert_true(g_file_set_contents(css,"",0,NULL));g_assert_true(g_file_set_contents(prefs,"[geany]\nconfirm_exit=false\nautoclose_chars=31\n",-1,NULL));g_assert_true(g_file_set_contents(source,"",0,NULL));
 gchar *args[]={"typing-pairs-native","--new-instance","--no-session","--config",profile,source,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(25,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,args);g_free(directory);g_free(profile);g_free(source);g_free(folder);g_free(config);g_free(css);g_free(prefs);return result;
}
