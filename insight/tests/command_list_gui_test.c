/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;
static GeanyDocument *source;
static gchar *directory;
static GtkWidget *named(GtkWidget *w,const gchar *name)
{
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static gchar *reference(const gchar *name,const gchar *filename)
{
 GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));GeanyDocument *doc=document_get_current();g_assert_true(doc!=source);g_assert_true(doc->changed);g_assert_null(doc->real_path);g_assert_cmpstr(doc->file_name,==,filename);g_assert_false(SEND(doc->editor->sci,SCI_CANUNDO,0,0));gchar *text=sci_get_contents(doc->editor->sci,-1);document_set_text_changed(doc,FALSE);g_assert_true(document_close(doc));g_assert_true(document_get_current()==source);return text;
}
static gboolean step(gpointer data)
{
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));
 SEND(source->editor->sci,SCI_SETSEL,4,1);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"preserved",-1);
 gchar *text=reference("insight-create-command-list","Command List.txt");g_assert_nonnull(strstr(text,"Cut Word"));g_assert_nonnull(strstr(text,"Create Key List"));g_assert_nonnull(strstr(text,"\t"));g_free(text);
 GeanyKeyBinding *key=keybindings_get_item(keybindings_get_core_group(GEANY_KEY_GROUP_FILE),GEANY_KEYS_FILE_SAVE);g_assert_nonnull(key);guint old_key=key->key;GdkModifierType old_mods=key->mods;key->key=GDK_KEY_F12;key->mods=GDK_CONTROL_MASK|GDK_SHIFT_MASK;
 gchar *shortcut=gtk_accelerator_get_label(key->key,key->mods);text=reference("insight-create-key-list","Keylist.txt");g_assert_nonnull(strstr(text,shortcut));g_assert_nonnull(strstr(text,"Unassigned"));g_free(text);key->key=old_key;key->mods=old_mods;g_free(shortcut);
 gchar *contents=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(contents,==,"source");g_free(contents);g_assert_false(SEND(source->editor->sci,SCI_CANUNDO,0,0));g_assert_false(source->changed);g_assert_cmpint(SEND(source->editor->sci,SCI_GETANCHOR,0,0),==,4);g_assert_cmpint(SEND(source->editor->sci,SCI_GETCURRENTPOS,0,0),==,1);
 gchar *clipboard=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clipboard,==,"preserved");g_free(clipboard);
 for(guint i=0;i<2;i++) { gchar *path=g_build_filename(directory,i ? "Keylist.txt" : "Command List.txt",NULL),*disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,"existing file unchanged");g_free(disk);g_free(path); }
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);text=reference("insight-create-key-list","Keylist.txt");g_assert_nonnull(strstr(text,"Список клавиш"));g_free(text);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);
 g_print("Command references GUI: menu paths/current shortcuts/unassigned registry, RU/EN, unsaved buffers, source selection/Undo/clipboard and existing disk files preserved\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Command list timeout");return G_SOURCE_REMOVE; }
int main(void)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 directory=g_dir_make_tmp("command-list-XXXXXX",NULL);gchar *config=g_build_filename(directory,"geany.conf",NULL),*path=g_build_filename(directory,"source.txt",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(path,"source",-1,NULL));
 for(guint i=0;i<2;i++) { gchar *name=g_build_filename(directory,i ? "Keylist.txt" : "Command List.txt",NULL);g_assert_true(g_file_set_contents(name,"existing file unchanged",-1,NULL));g_free(name); }g_assert_cmpint(g_chdir(directory),==,0);
 gchar *args[]={"command-list-test","--new-instance","--no-session","--config",directory,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(6,args);
}
