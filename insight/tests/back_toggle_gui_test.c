/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "navqueue.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
static gchar *first,*second;
static GtkWidget *window;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static void toggle(void) { gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-go-back-toggle"))); }
static void at(GeanyDocument *doc,gint line) { g_assert_true(document_get_current()==doc);g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,line); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 GeanyDocument *a=document_open_file(first,FALSE,NULL,NULL),*b=document_open_file(second,FALSE,NULL,NULL);window=gtk_widget_get_toplevel(GTK_WIDGET(a->editor->sci));
 navqueue_goto_line(b,a,2);navqueue_goto_line(a,b,3);at(b,2);
 for(guint i=0;i<6;i++) { toggle();at(i%2 ? b:a,i%2 ? 2:1); }
 keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_INSIGHT_BACKTOGGLE);at(a,1);
 keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_GOTO_FORWARD);at(b,2);toggle();at(a,1);
 sci_set_current_position(a->editor->sci,sci_get_position_from_line(a->editor->sci,3),FALSE);toggle();at(a,1);toggle();at(a,3);
 navqueue_goto_line(a,b,2);toggle();at(a,3);scintilla_send_message(a->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"prefix\n");toggle();at(a,2);toggle();at(a,4);
 g_assert_true(document_close(b));toggle();g_assert_true(document_get_current()==a);toggle();g_assert_true(document_get_current()==a);
 scintilla_send_message(a->editor->sci,SCI_UNDO,0,0);g_assert_false(scintilla_send_message(a->editor->sci,SCI_CANUNDO,0,0));
 GeanyDocument *untitled=document_new_file(NULL,NULL,NULL);toggle();g_assert_true(document_get_current()==untitled);g_assert_true(document_close(untitled));
 gchar *text=sci_get_contents(a->editor->sci,-1);g_assert_cmpstr(text,==,"α\nbeta\ngamma\ndelta\n");g_free(text);g_assert_false(scintilla_send_message(a->editor->sci,SCI_CANUNDO,0,0));
 g_print("Back Toggle GUI: repeated pairs, native key dispatch, manual Forward/caret, edit anchors, closed files, untitled and unchanged text/Undo passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Back toggle timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("back-toggle-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_mkdir_with_parents(profile,0700);first=g_build_filename(dir,"first.c",NULL);second=g_build_filename(dir,"second.c",NULL);g_assert_true(g_file_set_contents(first,"α\nbeta\ngamma\ndelta\n",-1,NULL));g_assert_true(g_file_set_contents(second,"one\ntwo\nthree\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *args[]={"back-toggle","--new-instance","--no-session","--config",profile,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(5,args);
}
