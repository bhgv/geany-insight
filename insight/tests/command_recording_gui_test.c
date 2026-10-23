/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "project.h"
#include <string.h>
#include <gdk/gdkkeysyms.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;
static GeanyDocument *doc;
static guint expected_rows;
static void rows(guint count);
static gchar *project_path,*profile;
static gboolean choose_project(gpointer data)
{
 (void)data;GList *list=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=list;p;p=p->next) if(GTK_IS_FILE_CHOOSER(p->data) && gtk_widget_get_visible(p->data)) dialog=p->data;g_list_free(list);if(!dialog) return G_SOURCE_CONTINUE;
 gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));gboolean ready=!g_strcmp0(path,project_path);g_free(path);if(!ready) { gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog),project_path);return G_SOURCE_CONTINUE; }gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static void save_settings(GKeyFile *key)
{
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"preferences1")));GList *list=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=list;p;p=p->next) if(!strcmp(gtk_widget_get_name(p->data),"GeanyPrefsDialog")) dialog=p->data;g_list_free(list);g_assert_nonnull(dialog);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_OK);
 gchar *path=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_key_file_load_from_file(key,path,0,NULL));g_free(path);
}
#define GROUP "insight-command-recording"
static GtkWidget *named(GtkWidget *w,const gchar *name)
{
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=list;p && !found;p=p->next) found=named(p->data,name);g_list_free(list);return found; }return NULL;
}
static void action(const gchar *suffix) { gchar *name=g_strconcat("insight-recording-",suffix,NULL);GtkWidget *item=named(window,name);g_free(name);g_assert_nonnull(item);g_signal_emit_by_name(gtk_widget_get_parent(item),"show");gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
/* Exercise ordinary input through the real GTK/Scintilla key handler. */
static void typed(gunichar code)
{
 ScintillaObject *sci=doc->editor->sci;GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(sci)));event->key.send_event=TRUE;event->key.keyval=gdk_unicode_to_keyval(code);event->key.time=GDK_CURRENT_TIME;
 gchar utf8[8]={0};event->key.length=g_unichar_to_utf8(code,utf8);event->key.string=g_strdup(utf8);
 GdkKeymapKey *keys=NULL;gint count=0;if(gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(gtk_widget_get_display(GTK_WIDGET(sci))),event->key.keyval,&keys,&count) && count) { event->key.hardware_keycode=keys[0].keycode;event->key.group=keys[0].group; }g_free(keys);
 GdkSeat *seat=gdk_display_get_default_seat(gtk_widget_get_display(GTK_WIDGET(sci)));if(seat) { GdkDevice *device=gdk_seat_get_keyboard(seat);if(device) gdk_event_set_device(event,device); }
 gboolean handled=FALSE;g_signal_emit_by_name(sci,"key-press-event",&event->key,&handled);gdk_event_free(event);
}
static void text_is(const gchar *expected) { gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void reset(const gchar *text,gint caret) { ScintillaObject *sci=doc->editor->sci;SEND(sci,SCI_SETREADONLY,0,0);sci_set_text(sci,text);SEND(sci,SCI_SETSEL,caret,caret);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);gtk_widget_grab_focus(GTK_WIDGET(sci)); }
static void remember(gboolean enabled)
{
 GtkWidget *item=named(window,"insight-recording-remember");g_assert_nonnull(item);g_signal_emit_by_name(gtk_widget_get_parent(item),"show");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),enabled);
}
static void saved_text(GKeyFile *key,const gchar *text)
{
 gsize n=0;gchar **values=g_key_file_get_string_list(key,GROUP,"texts",&n,NULL);g_assert_cmpuint(n,==,1);g_assert_cmpstr(values[0],==,text);g_strfreev(values);
}
static GKeyFile *record_key(const gchar *text)
{
 GKeyFile *key=g_key_file_new();const gchar *commands[]={"SCI_REPLACESEL"},*texts[]={text};g_key_file_set_integer(key,GROUP,"version",1);g_key_file_set_boolean(key,GROUP,"remember",TRUE);g_key_file_set_string_list(key,GROUP,"commands",commands,1);g_key_file_set_string_list(key,GROUP,"texts",texts,1);return key;
}
static void open_project(GKeyFile *key)
{
 g_key_file_set_string(key,"project","name","Recording test");g_key_file_set_string(key,"project","base_path",profile);gsize n=0;gchar *data=g_key_file_to_data(key,&n,NULL);g_assert_true(g_file_set_contents(project_path,data,n,NULL));g_free(data);
 document_set_text_changed(doc,FALSE);g_timeout_add(100,choose_project,NULL);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));doc=document_new_file("record-project.txt",NULL,"");gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
}
static void close_project(void)
{
 GtkWidget *book=ui_lookup_widget(window,"notebook1");for(gint i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(book));i++) { GeanyDocument *d=document_get_from_page(i);if(DOC_VALID(d)) document_set_text_changed(d,FALSE); }
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_close1")));doc=document_new_file("record-global.txt",NULL,"");gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
}
static void persistence(void)
{
 const gchar *global="global;\\n\r\nя",*local="project;\\t\r\nΩ";
 reset("",0);action("start");SEND(doc->editor->sci,SCI_REPLACESEL,0,(sptr_t)global);action("stop");remember(TRUE);
 GKeyFile *settings=g_key_file_new();save_settings(settings);saved_text(settings,global);
 GKeyFile *key=record_key(local);open_project(key);rows(1);text_is("");action("play");text_is(local);
 save_settings(settings);saved_text(settings,global);
 remember(FALSE);project_write_config();GKeyFile *disk=g_key_file_new();g_assert_true(g_key_file_load_from_file(disk,project_path,0,NULL));g_assert_false(g_key_file_has_key(disk,GROUP,"texts",NULL));
 remember(TRUE);project_write_config();g_assert_true(g_key_file_load_from_file(disk,project_path,0,NULL));saved_text(disk,local);close_project();reset("",0);action("play");text_is(global);
 open_project(disk);rows(1);action("play");text_is(local);action("clear");project_write_config();g_assert_true(g_key_file_load_from_file(disk,project_path,0,NULL));g_assert_false(g_key_file_has_key(disk,GROUP,"commands",NULL));close_project();
 /* Disabling persistence must not lose the in-memory global recording when
  * temporarily entering a project, or put its text in global preferences. */
 remember(FALSE);open_project(key);close_project();reset("",0);action("play");text_is(global);save_settings(settings);g_assert_false(g_key_file_has_key(settings,GROUP,"texts",NULL));
 const gchar *bad_commands[]={"SCI_COPY","SCI_CHARLEFT","12345","SCI_REPLACESEL"};
 for(guint i=0;i<4;i++) {
  GKeyFile *bad=record_key("opaque");g_key_file_set_string_list(bad,GROUP,"commands",bad_commands+i,1);if(i==3) g_key_file_remove_key(bad,GROUP,"texts",NULL);
  open_project(bad);rows(0);action("play");text_is("");project_write_config();g_assert_true(g_key_file_load_from_file(disk,project_path,0,NULL));g_assert_false(g_key_file_get_boolean(disk,GROUP,"remember",NULL));close_project();g_key_file_unref(bad);
 }
 gchar *huge=g_strnfill(65537,'x');GKeyFile *bad=record_key(huge);g_free(huge);open_project(bad);rows(0);close_project();g_key_file_unref(bad);
 bad=record_key("future");g_key_file_set_integer(bad,GROUP,"version",2);open_project(bad);rows(0);close_project();g_key_file_unref(bad);
 g_key_file_unref(disk);g_key_file_unref(key);g_key_file_unref(settings);remember(TRUE);action("clear");
}
static gboolean close_preview(gpointer data)
{
 (void)data;GList *list=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=list;p;p=p->next) if(!strcmp(gtk_widget_get_name(p->data),"insight-recording-dialog")) dialog=p->data;g_list_free(list);if(!dialog) return G_SOURCE_CONTINUE;
 GtkWidget *tree=named(dialog,"insight-recording-list");g_assert_nonnull(tree);g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),NULL),==,expected_rows);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
}
static void rows(guint count) {
 g_printerr("Inspect recording: %u steps\n",count);
 if(!count) { GtkWidget *item=named(window,"insight-recording-preview");g_assert_nonnull(item);g_signal_emit_by_name(gtk_widget_get_parent(item),"show");g_assert_false(gtk_widget_get_sensitive(item));return; }
 expected_rows=count;g_timeout_add(80,close_preview,NULL);action("preview");
}
static gboolean run(gpointer data)
{
 (void)data;doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;ScintillaObject *sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));
 if(g_getenv("GEANY_TEST_RECORDING_PROJECT_STARTUP")) {
  g_assert_true(gtk_widget_get_sensitive(ui_lookup_widget(window,"project_close1")) );text_is("");rows(1);reset("",0);action("play");text_is("cold project;\r\nΩ");close_project();rows(1);action("play");text_is("startup;\r\nя");document_set_text_changed(doc,FALSE);g_print("Cold project recording and independent global restore passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 text_is("");rows(1);reset("",0);action("play");text_is("startup;\r\nя");SEND(sci,SCI_UNDO,0,0);text_is("");
 reset("",0);action("start");typed('a');typed('b');SEND(sci,SCI_CHARLEFT,0,0);SEND(sci,SCI_REPLACESEL,0,(sptr_t)"x");action("stop");text_is("axb");rows(4);
 reset("12",1);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);action("play");text_is("1axb2");SEND(sci,SCI_UNDO,0,0);text_is("12");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);text_is("1axb2");
 gchar *clip=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clip,==,"sentinel");g_free(clip);
 reset("",0);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_RECORDPLAY);text_is("axb");
 reset("",0);action("start");SEND(sci,SCI_REPLACESEL,0,(sptr_t)"я\r\n");action("play");text_is("я\r\nя\r\n");rows(1);
 reset("a",1);SEND(sci,SCI_SETREADONLY,1,0);action("play");text_is("a");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_SETREADONLY,0,0);
 reset("",0);action("start");SEND(sci,SCI_REPLACESEL,0,(sptr_t)"prefix");SEND(sci,SCI_COPY,0,0);rows(0);
 reset("",0);action("start");for(guint i=0;i<257;i++) SEND(sci,SCI_REPLACESEL,0,(sptr_t)"x");rows(0);
 reset("",0);action("start");SEND(sci,SCI_REPLACESEL,0,(sptr_t)"x");SEND(sci,SCI_UNDO,0,0);rows(0);
 reset("",0);action("start");SEND(sci,SCI_REPLACESEL,0,(sptr_t)"ok");GeanyDocument *other=document_new_file("record-other.txt",NULL,"");ScintillaObject *other_sci=other->editor->sci;SEND(other_sci,SCI_REPLACESEL,0,(sptr_t)"outside");rows(1);document_set_text_changed(other,FALSE);g_assert_true(document_close(other));gtk_widget_grab_focus(GTK_WIDGET(sci));
 action("clear");rows(0);reset("",0);action("play");text_is("");
 persistence();document_set_text_changed(doc,FALSE);g_print("Command recording: cold restore, opt-in, global/project isolation, escaped UTF-8/CRLF roundtrip, disable/clear, malformed/forbidden/oversized/future records, native replay, one Undo/Redo and existing guards passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timed_out(gpointer data) { (void)data;g_error("Command recording timeout");return G_SOURCE_REMOVE; }
int main(void)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 profile=g_dir_make_tmp("recording-XXXXXX",NULL);project_path=g_build_filename(profile,"record.geany",NULL);gchar *config=g_build_filename(profile,"geany.conf",NULL),*path=g_build_filename(profile,"source.txt",NULL);GKeyFile *key=record_key("startup;\r\nя");g_key_file_set_boolean(key,"geany","confirm_exit",FALSE);g_key_file_set_integer(key,"geany","autoclose_chars",0);gchar *bytes=g_key_file_to_data(key,NULL,NULL);g_assert_true(g_file_set_contents(config,bytes,-1,NULL));g_free(bytes);g_key_file_unref(key);g_assert_true(g_file_set_contents(path,"",0,NULL));gboolean cold_project=g_getenv("GEANY_TEST_RECORDING_PROJECT_STARTUP")!=NULL;
 if(cold_project) { key=record_key("cold project;\r\nΩ");g_key_file_set_string(key,"project","name","Cold recording");g_key_file_set_string(key,"project","base_path",profile);bytes=g_key_file_to_data(key,NULL,NULL);g_assert_true(g_file_set_contents(project_path,bytes,-1,NULL));g_free(bytes);g_key_file_unref(key); }
 gchar *args[]={"recording","--new-instance","--no-session","--config",profile,cold_project ? project_path:path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,run,NULL);g_timeout_add_seconds(60,timed_out,NULL);return main_lib(6,args);
}
