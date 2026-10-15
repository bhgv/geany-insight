/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#undef SSM
#define SSM(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window,*insert;static ScintillaObject *sci;static guint phase;
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static gboolean parameters(gpointer data) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=windows;p;p=p->next) if(GTK_IS_DIALOG(p->data) && !g_strcmp0(gtk_window_get_title(GTK_WINDOW(p->data)),"Template parameters")) dialog=p->data;g_list_free(windows);g_assert_nonnull(dialog);if(data) { gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }gtk_entry_set_text(GTK_ENTRY(named(dialog,"insight-template-variable-name")),"имя");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-template-editable-fields")),TRUE);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static void selection(const gchar *expected) { gchar *text=sci_get_selection_contents(sci);g_assert_cmpstr(text,==,expected);g_free(text); }
static void text_is(const gchar *expected) { gchar *text=sci_get_contents(sci,sci_get_length(sci)+1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void key(guint value,guint modifiers) { GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(window));event->key.keyval=value;event->key.state=modifiers;gtk_widget_grab_focus(GTK_WIDGET(sci));g_assert_true(gtk_widget_event(window,event));gdk_event_free(event); }
static void insert_template(void) { g_idle_add(parameters,NULL);gtk_button_clicked(GTK_BUTTON(insert)); }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 if(!phase) { sci=doc->editor->sci;SSM(sci,SCI_SETEOLMODE,SC_EOL_CRLF,0);window=gtk_widget_get_toplevel(GTK_WIDGET(sci));GtkWidget *command=named(ui_lookup_widget(window,"tools1_menu"),"insight-templates-command");g_assert_nonnull(command);gtk_menu_item_activate(GTK_MENU_ITEM(command));GtkWidget *clips=named(window,"insight-clips");GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(clips));GtkTreeIter first;g_assert_true(gtk_tree_model_get_iter_first(model,&first));gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(clips)),&first);insert=named(window,"insight-insert-template");g_assert_nonnull(insert);insert_template();phase++;return G_SOURCE_CONTINUE; }
 if(phase==1) {
  text_is("имя//имя\r\n");selection("имя");g_assert_cmpint(SSM(sci,SCI_INDICATORVALUEAT,27,0),>,0);
  sci_replace_sel(sci,"тест");text_is("тест//имя\r\n");key(GDK_KEY_Tab,0);selection("");g_assert_cmpint(sci_get_current_position(sci),==,strlen("тест/"));
  sci_replace_sel(sci,"x");key(GDK_KEY_ISO_Left_Tab,GDK_SHIFT_MASK);selection("тест");key(GDK_KEY_Tab,0);selection("x");key(GDK_KEY_Tab,0);selection("имя");sci_replace_sel(sci,"end");key(GDK_KEY_Tab,0);text_is("тест/x/end\r\n");g_assert_cmpint(sci_get_current_position(sci),==,strlen("тест/x/end"));g_assert_cmpint(SSM(sci,SCI_INDICATORVALUEAT,27,0),==,0);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) { SSM(sci,SCI_SETSEL,0,sci_get_length(sci));insert_template();selection("имя");key(GDK_KEY_Escape,0);text_is("имя//имя\r\n");g_assert_cmpint(SSM(sci,SCI_INDICATORVALUEAT,27,0),==,0);phase++;return G_SOURCE_CONTINUE; }
 if(phase==3) { SSM(sci,SCI_SETSEL,0,sci_get_length(sci));insert_template();selection("имя");SSM(sci,SCI_UNDO,0,0);g_assert_cmpint(SSM(sci,SCI_INDICATORVALUEAT,27,0),==,0);phase++;return G_SOURCE_CONTINUE; }
 if(phase==4) { SSM(sci,SCI_SETSEL,0,sci_get_length(sci));insert_template();selection("имя");SSM(sci,SCI_INSERTTEXT,sci_get_length(sci),(sptr_t)"outside");g_assert_cmpint(SSM(sci,SCI_INDICATORVALUEAT,27,0),==,0);phase++;return G_SOURCE_CONTINUE; }
 if(phase==5) { sci_set_text(sci,"Fields");sci_set_current_position(sci,6,FALSE);g_idle_add(parameters,NULL);key(GDK_KEY_Tab,0);text_is("имя//имя\r\n");selection("имя");key(GDK_KEY_Escape,0);phase++;return G_SOURCE_CONTINUE; }
 if(phase==6) { sci_set_text(sci,"Fields");sci_set_current_position(sci,6,FALSE);g_idle_add(parameters,GINT_TO_POINTER(1));key(GDK_KEY_Tab,0);text_is("Fields");selection("");g_assert_cmpint(sci_get_current_position(sci),==,6);phase++;return G_SOURCE_CONTINUE; }
 if(phase==7) { sci_set_text(sci,"Fie");sci_set_current_position(sci,3,FALSE);key(GDK_KEY_Tab,0);g_assert_true(SSM(sci,SCI_AUTOCACTIVE,0,0));SSM(sci,SCI_INSERTTEXT,0,(sptr_t)"changed:");SCNotification notification={0};notification.nmhdr.code=SCN_USERLISTSELECTION;notification.listType=74;notification.text="Fields";g_signal_emit_by_name(sci,"sci-notify",0,&notification);text_is("changed:Fie");SSM(sci,SCI_AUTOCCANCEL,0,0);phase++;return G_SOURCE_CONTINUE; }
 g_print("Editable template GUI: real parameter dialog, independent repeated/empty UTF-8 fields, edits, Tab/Shift+Tab, final cursor, Escape, undo and outside-edit cancellation, exact completion with parameters/fields, dialog cancellation and stale-prefix callback rejection passed\n");document_set_text_changed(doc,FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Editable template GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("template-fields-%u",g_random_int()),*profile=g_build_filename(cwd,name,NULL),*folder=g_build_filename(profile,"insight",NULL);g_assert_cmpint(g_mkdir_with_parents(folder,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));GKeyFile *notes=g_key_file_new();g_key_file_set_integer(notes,"notes","version",1);g_key_file_set_integer(notes,"notes","count",1);g_key_file_set_integer(notes,"note-0","kind",2);g_key_file_set_string(notes,"note-0","name","Fields");g_key_file_set_string(notes,"note-0","path","");g_key_file_set_uint64(notes,"note-0","line",0);g_key_file_set_string(notes,"note-0","text","$name$/$empty$/$name$$cursor$\n");gchar *text=g_key_file_to_data(notes,NULL,NULL),*note_path=g_build_filename(folder,"notes.conf",NULL);g_assert_true(g_file_set_contents(note_path,text,-1,NULL));g_key_file_unref(notes);g_free(text);gchar *argv[]={"template-fields-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(25,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(cwd);g_free(name);g_free(profile);g_free(folder);g_free(config);g_free(note_path);return result;
}
