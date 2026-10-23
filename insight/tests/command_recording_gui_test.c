/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;
static GeanyDocument *doc;
static guint expected_rows;
static GtkWidget *named(GtkWidget *w,const gchar *name)
{
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=list;p && !found;p=p->next) found=named(p->data,name);g_list_free(list);return found; }return NULL;
}
static void action(const gchar *suffix) { gchar *name=g_strconcat("insight-recording-",suffix,NULL);GtkWidget *item=named(window,name);g_free(name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void text_is(const gchar *expected) { gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void reset(const gchar *text,gint caret) { ScintillaObject *sci=doc->editor->sci;SEND(sci,SCI_SETREADONLY,0,0);sci_set_text(sci,text);SEND(sci,SCI_SETSEL,caret,caret);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);gtk_widget_grab_focus(GTK_WIDGET(sci)); }
static gboolean close_preview(gpointer data)
{
 (void)data;GList *list=gtk_window_list_toplevels();GtkWidget *dialog=NULL;for(GList *p=list;p;p=p->next) if(!strcmp(gtk_widget_get_name(p->data),"insight-recording-dialog")) dialog=p->data;g_list_free(list);if(!dialog) return G_SOURCE_CONTINUE;
 GtkWidget *tree=named(dialog,"insight-recording-list");g_assert_nonnull(tree);g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),NULL),==,expected_rows);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
}
static void rows(guint count) { expected_rows=count;g_timeout_add(80,close_preview,NULL);action("preview"); }
static gboolean run(gpointer data)
{
 (void)data;doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;ScintillaObject *sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));
 reset("",0);action("start");SEND(sci,SCI_REPLACESEL,0,(sptr_t)"ab");SEND(sci,SCI_CHARLEFT,0,0);SEND(sci,SCI_REPLACESEL,0,(sptr_t)"x");action("stop");text_is("axb");rows(3);
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
 g_print("Command recording: literal UTF-8/CRLF, relative movement, preview, one Undo/Redo, clipboard preservation, keys, readonly, unsupported actions, bounds, Undo invalidation and document switch passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timed_out(gpointer data) { (void)data;g_error("Command recording timeout");return G_SOURCE_REMOVE; }
int main(void)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("recording-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL),*path=g_build_filename(dir,"source.txt",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nautoclose_chars=0\n",-1,NULL));g_assert_true(g_file_set_contents(path,"",0,NULL));gchar *args[]={"recording","--new-instance","--no-session","--config",dir,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,run,NULL);g_timeout_add_seconds(30,timed_out,NULL);return main_lib(6,args);
}
