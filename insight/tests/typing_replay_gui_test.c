/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <gdk/gdkkeysyms.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GeanyDocument *source,*other;
static GtkWidget *window;
static guint mode,tick;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *dialog(const gchar *name) { GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),name)) { found=p->data;break; }g_list_free(windows);return found; }
static void action(const gchar *suffix) { gchar *name=g_strconcat("insight-typing-",suffix,NULL);GtkWidget *item=named(window,name);g_free(name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void text_is(const gchar *text) { gchar *actual=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(actual,==,text);g_free(actual); }
static void select_at(gint anchor,gint caret) { SEND(source->editor->sci,SCI_SETSEL,anchor,caret); }
static void reset(const gchar *text,gint anchor,gint caret) {
 ScintillaObject *sci=source->editor->sci;source->readonly=FALSE;SEND(sci,SCI_SETREADONLY,0,0);SEND(sci,SCI_CLEARSELECTIONS,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,0);SEND(sci,SCI_SETSELECTIONNANCHORVIRTUALSPACE,0,0);sci_set_text(sci,text);select_at(anchor,caret);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);gtk_widget_grab_focus(GTK_WIDGET(sci));
}
/* Exercise the real GTK IM/key path for ordinary direct input. */
static void key(gunichar code) {
 ScintillaObject *sci=source->editor->sci;GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(sci)));event->key.send_event=TRUE;event->key.keyval=gdk_unicode_to_keyval(code);event->key.time=GDK_CURRENT_TIME;
 gchar utf8[8]={0};event->key.length=g_unichar_to_utf8(code,utf8);event->key.string=g_strdup(utf8);
 GdkKeymapKey *keys=NULL;gint count=0;if(gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(gtk_widget_get_display(GTK_WIDGET(sci))),event->key.keyval,&keys,&count) && count) { event->key.hardware_keycode=keys[0].keycode;event->key.group=keys[0].group; }g_free(keys);
 GdkSeat *seat=gdk_display_get_default_seat(gtk_widget_get_display(GTK_WIDGET(sci)));if(seat) { GdkDevice *device=gdk_seat_get_keyboard(seat);if(device) gdk_event_set_device(event,device); }
 gboolean handled=FALSE;g_signal_emit_by_name(sci,"key-press-event",&event->key,&handled);gdk_event_free(event);
}
static void typed(const gchar *text) { for(const gchar *at=text;*at;at=g_utf8_next_char(at)) key(g_utf8_get_char(at)); }
/* Controlled native notification fixtures for committed/tentative IME chunks;
 * these do not claim to exercise an external OS input-method provider. */
static void committed(const gchar *text,gint kind) {
 ScintillaObject *sci=source->editor->sci;SEND(sci,SCI_BEGINUNDOACTION,0,0);SEND(sci,SCI_REPLACESEL,0,(sptr_t)text);SCNotification nt={0};nt.nmhdr.code=SCN_CHARADDED;nt.ch=g_utf8_get_char(text);nt.characterSource=kind;g_signal_emit_by_name(sci,"sci-notify",0,&nt);SEND(sci,SCI_ENDUNDOACTION,0,0);
}
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog("insight-repeat-typing-dialog");
 if(d) {
  if(mode==26) return G_SOURCE_CONTINUE;
  GtkWidget *count=named(d,"insight-repeat-count");g_assert_nonnull(count);
  if(mode==0) {
   if(!tick++) { gtk_spin_button_set_value(GTK_SPIN_BUTTON(count),3);return G_SOURCE_CONTINUE; }
   if(g_getenv("GEANY_REPEAT_TYPING_CAPTURE")) { GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_REPEAT_TYPING_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  }
  if(mode==1) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
  if(mode==2) {
   if(!tick++) { gtk_spin_button_set_value(GTK_SPIN_BUTTON(count),2);gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
   gchar *copy=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(copy,==,"abcabc");g_free(copy);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
  }
  if(mode==3) SEND(source->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"prefix");
  if(mode==4) select_at(0,0);
  if(mode==5) { g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT))); }
  if(mode==6) other=document_new_file("other.txt",NULL,"other");
  if(mode==7) { document_set_text_changed(source,FALSE);g_assert_true(document_close(source)); }
  if(mode==8) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE; }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
 }
 d=dialog("insight-typing-history-dialog");if(!d) return G_SOURCE_CONTINUE;
 GtkWidget *tree=named(d,"insight-typing-history-list");GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
 if(mode==20) {
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),>=,2);if(!tick++) return G_SOURCE_CONTINUE;
  if(g_getenv("GEANY_TYPING_HISTORY_CAPTURE")) { GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_TYPING_HISTORY_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==21) {
  if(!tick++) { gtk_dialog_response(GTK_DIALOG(d),202);return G_SOURCE_CONTINUE; }
  gchar *copy=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(copy,==,"xyz");g_free(copy);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==22) {
  if(!tick++) { committed("new",SC_CHARACTERSOURCE_IME_RESULT);gtk_dialog_response(GTK_DIALOG(d),202);return G_SOURCE_CONTINUE; }
  if(tick==2) { tick++;gchar *copy=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(copy,==,"sentinel");g_free(copy);gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==23) {
  if(!tick++) { gtk_dialog_response(GTK_DIALOG(d),203);return G_SOURCE_CONTINUE; }
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT)));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==24) { g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,20);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(mode==25) { g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(mode==26) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
 return G_SOURCE_CONTINUE;
}
static void modal(guint which,const gchar *command) { mode=which;tick=0;guint timer=g_timeout_add(120,respond,NULL);action(command);if(which==26) g_source_remove(timer); }
static gboolean preview_cancel(gpointer data) { (void)data;GtkWidget *d=dialog("insight-repeat-typing-dialog");if(!d) return G_SOURCE_CONTINUE;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));ScintillaObject *sci=source->editor->sci;
 action("clear");reset("",0,0);typed("abc");text_is("abc");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);action("repeat");text_is("abcabc");SEND(sci,SCI_UNDO,0,0);text_is("abc");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);text_is("abcabc");
 reset("left right",10,5);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);action("repeat");text_is("left abc");g_assert_cmpint(sci_get_current_position(sci),==,8);SEND(sci,SCI_UNDO,0,0);text_is("left right");
 reset("abc",0,3);action("repeat");text_is("abc");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,0);g_assert_cmpint(sci_get_current_position(sci),==,3);
 reset("",0,0);SEND(sci,SCI_REPLACESEL,0,(sptr_t)"paste");action("repeat");text_is("pasteabc");
 reset("",0,0);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_REPEATTYPING);text_is("abc");
 reset("target",1,4);modal(0,"preview");text_is("tabcabcabcet");SEND(sci,SCI_UNDO,0,0);text_is("target");SEND(sci,SCI_REDO,0,0);text_is("tabcabcabcet");
 reset("target",1,4);modal(1,"preview");text_is("target");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));modal(2,"preview");text_is("target");
 modal(3,"preview");text_is("prefixtarget");reset("target",1,4);modal(4,"preview");text_is("target");
 reset("target",1,4);SEND(sci,SCI_SETREADONLY,1,0);modal(5,"preview");text_is("target");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("target",1,4);modal(6,"preview");text_is("target");g_assert_true(document_get_current()==other);g_assert_true(document_close(other));
 reset("tabs",1,3);SEND(sci,SCI_SETUSETABS,FALSE,0);action("simple-tab");text_is("t\ts");SEND(sci,SCI_UNDO,0,0);text_is("tabs");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("tabs",3,1);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_SIMPLETAB);text_is("t\ts");
 action("clear");reset("",0,0);committed("pre",SC_CHARACTERSOURCE_TENTATIVE_INPUT);action("repeat");text_is("pre");select_at(0,3);committed("完了",SC_CHARACTERSOURCE_IME_RESULT);action("repeat");text_is("完了完了");
 action("clear");reset("",0,0);typed("abc");SEND(sci,SCI_INSERTTEXT,3,(sptr_t)" ");select_at(4,4);typed("xyz");text_is("abc xyz");modal(20,"history");modal(21,"history");
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);modal(22,"history");text_is("abc xyznew");
 g_timeout_add(150,preview_cancel,NULL);modal(26,"history");modal(23,"history");action("repeat");text_is("abc xyznew");modal(25,"history");
 for(guint i=0;i<25;i++) { SEND(sci,SCI_INSERTTEXT,sci_get_length(sci),(sptr_t)" ");select_at(sci_get_length(sci),sci_get_length(sci));key('a'+i%26); }modal(24,"history");
 action("clear");reset("",0,0);gchar *large=g_strnfill(4090,'z');committed(large,SC_CHARACTERSOURCE_IME_RESULT);g_free(large);typed("1234567");modal(25,"history");
 action("clear");reset("",0,0);typed("abc");reset("abc\r\n",0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);action("repeat");action("simple-tab");text_is("abc\r\n");
 reset("abc\r\n",0,0);SEND(sci,SCI_ADDSELECTION,2,2);SEND(sci,SCI_SETMAINSELECTION,0,0);action("repeat");text_is("abc\r\n");
 reset("abc\r\n",0,0);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,1);action("repeat");text_is("abc\r\n");
 reset("(α)",2,2);action("repeat");text_is("(α)");reset("a\r\nb",2,2);action("simple-tab");text_is("a\r\nb");
 reset("",0,0);SEND(sci,SCI_ADDTEXT,3,(sptr_t)"a\0b");select_at(0,0);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);action("repeat");g_assert_cmpint(sci_get_length(sci),==,3);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("target",1,4);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);modal(1,"preview");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);
 GtkWidget *entry=gtk_entry_new();gtk_box_pack_start(GTK_BOX(ui_lookup_widget(window,"vbox1")),entry,FALSE,FALSE,0);gtk_widget_show(entry);gtk_widget_grab_focus(entry);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_REPEATTYPING);text_is("target");gtk_widget_destroy(entry);
 reset("target",1,4);modal(7,"preview");
 g_print("Typing replay GUI: real GTK direct typing, controlled committed/tentative IME chunks, literal replay/Undo/Redo/no-op, paste exclusion, preview/count/copy/stale/readonly/switched/closed guards, Simple Tab, history copy/clear/stale/cap/overflow, unsupported/invalid selections, focus and RU/EN passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Typing replay GUI timeout mode %u tick %u",mode,tick);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("typing-replay-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL),*path=g_build_filename(dir,"source.txt",NULL);g_assert_true(g_file_set_contents(path,"",0,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nautoclose_chars=0\n",-1,NULL));gchar *args[]={"typing-replay","--new-instance","--no-session","--config",dir,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(45,timeout,NULL);return main_lib(6,args);
}
