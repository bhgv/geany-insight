/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <string.h>
#include <glib/gstdio.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window,*search,*manage;static GeanyDocument *doc;static guint phase,ticks;static gchar *capture,*settings_file;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *top(const gchar *name) { GList *tops=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=tops;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),name)) found=p->data;g_list_free(tops);return found; }
static void action(guint which) { gchar *name=g_strdup_printf("insight-web-command-%u",which);GtkWidget *item=named(window,name);g_free(name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void button(const gchar *suffix) { gchar *name=g_strconcat("insight-web-engine-",suffix,NULL);GtkWidget *b=named(manage,name);g_free(name);g_assert_nonnull(b);gtk_button_clicked(GTK_BUTTON(b)); }
static gint count(void) { return gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(named(manage,"insight-web-engines-list"))),NULL); }
static gboolean step(gpointer data) {
 (void)data;if(++ticks>120) g_error("Web search GUI timeout phase %u",phase);
 if(phase==0) {
  doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));if(!named(window,"insight-web-command-0")) return G_SOURCE_CONTINUE;
  SEND(doc->editor->sci,SCI_SETSEL,0,5);action(0);search=top("insight-web-search-window");g_assert_nonnull(search);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(search,"insight-web-query"))),==,"alpha");g_assert_nonnull(strstr(gtk_entry_get_text(GTK_ENTRY(named(search,"insight-web-preview"))),"alpha"));
  gtk_entry_set_text(GTK_ENTRY(named(search,"insight-web-query")),"α &+#");gtk_dialog_response(GTK_DIALOG(search),101);gchar *copy=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(strstr(copy,"%CE%B1%20%26%2B%23"));g_free(copy);action(1);manage=top("insight-web-engines-window");g_assert_nonnull(manage);g_assert_cmpint(count(),==,3);
  gtk_entry_set_text(GTK_ENTRY(named(manage,"insight-web-engine-name")),"Local test");gtk_entry_set_text(GTK_ENTRY(named(manage,"insight-web-engine-url")),"javascript:%S");button("add");g_assert_cmpint(count(),==,3);gtk_entry_set_text(GTK_ENTRY(named(manage,"insight-web-engine-url")),"https://example.test/search?q=%S");button("add");g_assert_cmpint(count(),==,4);button("up");button("down");
  gtk_combo_box_set_active(GTK_COMBO_BOX(named(search,"insight-web-engine")),3);g_assert_nonnull(strstr(gtk_entry_get_text(GTK_ENTRY(named(search,"insight-web-preview"))),"example.test/search"));gtk_dialog_response(GTK_DIALOG(search),GTK_RESPONSE_ACCEPT);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==1) {
  gchar *text=NULL;if(!g_file_get_contents(capture,&text,NULL,NULL)) return G_SOURCE_CONTINUE;if(!*text) { g_free(text);return G_SOURCE_CONTINUE; }g_assert_cmpstr(text,==,"https://example.test/search?q=%CE%B1%20%26%2B%23");g_free(text);
  GKeyFile *saved=g_key_file_new();g_assert_true(g_key_file_load_from_file(saved,settings_file,G_KEY_FILE_NONE,NULL));g_assert_cmpint(g_key_file_get_integer(saved,"search","selected",NULL),==,3);g_assert_cmpint(g_key_file_get_integer(saved,"search","count",NULL),==,4);g_assert_false(g_key_file_has_key(saved,"search","query",NULL));g_key_file_unref(saved);
  gtk_widget_destroy(search);gtk_widget_destroy(manage);action(0);search=top("insight-web-search-window");g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(named(search,"insight-web-engine"))),==,3);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(search,"insight-web-query"))),==,"alpha");
  gtk_entry_set_text(GTK_ENTRY(named(search,"insight-web-query")),"");g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(search),GTK_RESPONSE_ACCEPT)));
  keybindings_send_command(GEANY_KEY_GROUP_SEARCH,GEANY_KEYS_INSIGHT_SEARCHENGINES);manage=top("insight-web-engines-window");g_assert_cmpint(count(),==,4);button("remove");g_assert_cmpint(count(),==,3);g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(named(search,"insight-web-engine"))),==,2);
  gtk_widget_destroy(manage);gtk_widget_destroy(search);SEND(doc->editor->sci,SCI_SETSEL,0,10);action(0);search=top("insight-web-search-window");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(search,"insight-web-query"))),==,"");gtk_widget_destroy(search);
  g_assert_cmpint(SEND(doc->editor->sci,SCI_GETANCHOR,0,0),==,0);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,10);g_assert_false(SEND(doc->editor->sci,SCI_CANUNDO,0,0));g_assert_false(SEND(doc->editor->sci,SCI_GETMODIFY,0,0));
  g_print("Web search GUI: selection and multiline guard, escaped copy, engine validation/add/remove/reorder, singleton reopening, saved selection and captured browser argv, unchanged source passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
#ifndef G_OS_UNIX
 return 77;
#else
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("web-search-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL),*path=g_build_filename(dir,"source.txt",NULL),*browser=g_build_filename(dir,"browser",NULL);capture=g_build_filename(dir,"url.txt",NULL);settings_file=g_build_filename(dir,"insight","web-search.conf",NULL);g_setenv("INSIGHT_BROWSER_CAPTURE",capture,TRUE);g_assert_true(g_file_set_contents(browser,"#!/bin/sh\nprintf '%s' \"$1\" > \"$INSIGHT_BROWSER_CAPTURE\"\n",-1,NULL));g_assert_cmpint(g_chmod(browser,0700),==,0);gchar *settings=g_strdup_printf("[geany]\nconfirm_exit=false\n[tools]\nbrowser_cmd=%s\n",browser);g_assert_true(g_file_set_contents(config,settings,-1,NULL));g_assert_true(g_file_set_contents(path,"alpha\nbeta\n",-1,NULL));gchar *args[]={"web-search","--new-instance","--no-session","--config",dir,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(250,step,NULL);return main_lib(6,args);
#endif
}
