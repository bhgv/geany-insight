/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window,*test_item,*help_menu;
static GeanyDocument *source;
static guint scenario,tick,executed;
static GtkWidget *named(GtkWidget *w,const gchar *name)
{
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *dialog(void)
{
 GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-command-browser")) { found=p->data;break; }g_list_free(windows);return found;
}
static void invoked(GtkMenuItem *item,gpointer data) { (void)item;(void)data;g_assert_true(document_get_current()==source);g_assert_true(gtk_window_get_focus(GTK_WINDOW(window))==GTK_WIDGET(source->editor->sci));executed++; }
static GtkWidget *add_item(const gchar *label)
{
 GtkWidget *item=gtk_menu_item_new_with_label(label);gtk_widget_set_tooltip_text(item,"deterministic browser fixture target");g_signal_connect(item,"activate",G_CALLBACK(invoked),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(help_menu),item);gtk_widget_show(item);return item;
}
static void query(GtkWidget *d,const gchar *text,gint count)
{
 GtkWidget *entry=named(d,"insight-command-search"),*view=named(d,"insight-command-view");g_assert_nonnull(entry);g_assert_nonnull(view);gtk_entry_set_text(GTK_ENTRY(entry),text);g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,count);
}
static gboolean drive(gpointer data)
{
 (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;
 if(scenario==1) { query(d,"FIXTURE α",1);GtkWidget *view=named(d,"insight-command-view");gtk_tree_view_column_clicked(gtk_tree_view_get_column(GTK_TREE_VIEW(view),0));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(scenario>=2 && scenario<=4) {
  if(!tick++) {
   query(d,"fixture α",1);
   if(scenario==2) gtk_widget_set_sensitive(test_item,FALSE);
   if(scenario==3) { gtk_widget_destroy(test_item);test_item=NULL; }
   if(scenario==4) gtk_widget_set_sensitive(gtk_menu_get_attach_widget(GTK_MENU(help_menu)),FALSE);
   gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE;
  }
  g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(d,"insight-command-count"))),"no longer available"));g_assert_cmpuint(executed,==,0);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(scenario==5) {
  if(!tick++) { query(d,"dynamic β",0);test_item=add_item("Browser dynamic β");gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY);return G_SOURCE_CONTINUE; }
  query(d,"DYNAMIC β",1);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(scenario==6) { query(d,"fixture α",1);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE; }
 if(scenario==7) { gtk_dialog_response(GTK_DIALOG(d),202);return G_SOURCE_REMOVE; }
 if(scenario==8) { g_assert_cmpstr(gtk_window_get_title(GTK_WINDOW(d)),==,"Поиск команд");query(d,"вырезать слово",2);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(scenario==9) {
  GtkWidget *entry=named(d,"insight-command-search");
  if(!tick++) { gtk_entry_set_text(GTK_ENTRY(entry),"Scroll");return G_SOURCE_CONTINUE; }
  GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_COMMAND_BROWSER_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 g_error("Unknown browser scenario");return G_SOURCE_REMOVE;
}
static void open_browser(guint which)
{
 scenario=which;tick=0;g_timeout_add(150,drive,NULL);if(which==1) keybindings_send_command(GEANY_KEY_GROUP_HELP,GEANY_KEYS_INSIGHT_COMMANDBROWSER);else gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-command-browser-command")));
}
static gboolean step(gpointer data)
{
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));help_menu=ui_lookup_widget(window,"menu_help1_menu");test_item=add_item("Browser fixture α");SEND(source->editor->sci,SCI_SETSEL,4,1);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"preserved",-1);
 open_browser(1);g_assert_cmpuint(executed,==,0);
 open_browser(2);gtk_widget_set_sensitive(test_item,TRUE);
 open_browser(3);test_item=add_item("Browser fixture α");
 open_browser(4);gtk_widget_set_sensitive(gtk_menu_get_attach_widget(GTK_MENU(help_menu)),TRUE);
 GtkWidget *original=test_item;open_browser(5);gtk_widget_destroy(test_item);test_item=original;
 open_browser(6);g_assert_cmpuint(executed,==,1);
 open_browser(7);GeanyDocument *reference=document_get_current();g_assert_true(reference!=source);g_assert_cmpstr(reference->file_name,==,"Keylist.txt");document_set_text_changed(reference,FALSE);g_assert_true(document_close(reference));
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);open_browser(8);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);
 gchar *large=g_strnfill(1024*1024+1,'x');gtk_widget_set_tooltip_text(test_item,large);g_free(large);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-create-command-list")));g_assert_true(document_get_current()==source);gtk_widget_set_tooltip_text(test_item,"restored");
 if(g_getenv("GEANY_COMMAND_BROWSER_CAPTURE")) open_browser(9);
 gchar *text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,"source");g_free(text);g_assert_false(SEND(source->editor->sci,SCI_CANUNDO,0,0));g_assert_false(source->changed);g_assert_cmpint(SEND(source->editor->sci,SCI_GETANCHOR,0,0),==,4);g_assert_cmpint(SEND(source->editor->sci,SCI_GETCURRENTPOS,0,0),==,1);
 gchar *clipboard=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(clipboard,==,"preserved");g_free(clipboard);gtk_widget_destroy(test_item);
 g_print("Command browser GUI: Unicode token filter/sort, Run/focus, disabled/hidden-ancestor/destroyed guards, refresh dynamic menu, keylist export, RU/EN, bounded snapshots and untouched source/clipboard passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Command browser timeout");return G_SOURCE_REMOVE; }
int main(void)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("command-browser-XXXXXX",NULL),*config=g_build_filename(directory,"geany.conf",NULL),*path=g_build_filename(directory,"source.txt",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(path,"source",-1,NULL));gchar *args[]={"command-browser-test","--new-instance","--no-session","--config",directory,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(35,timeout,NULL);return main_lib(6,args);
}
