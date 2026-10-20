/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "navqueue.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window;static GeanyDocument *one,*two;static gchar *first,*second;static guint phase,dialog_phase;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *history(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *result=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-selection-history")) result=p->data;g_list_free(windows);return result; }
static gboolean choose(GtkWidget *view,const gchar *file,gint target) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter iter;gboolean valid=gtk_tree_model_get_iter_first(model,&iter);
 for(;valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *path=NULL;gint line;gtk_tree_model_get(model,&iter,5,&path,2,&line,-1);gboolean match=!g_strcmp0(path,file) && line==target;g_free(path);if(match) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&iter);return TRUE; } }return FALSE;
}
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *dialog=history();if(!dialog) return G_SOURCE_CONTINUE;GtkWidget *view=named(dialog,"insight-selection-history-list");g_assert_nonnull(view);
 if(phase==2 && !dialog_phase++) { scintilla_send_message(one->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"// another\n");gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
 if(phase==3) { g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),<=,513);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(phase==1) { if(g_getenv("GEANY_HISTORY_CAPTURE")) { GdkWindow *w=gtk_widget_get_window(dialog);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_HISTORY_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); } }
 g_assert_true(choose(view,first,phase==0 ? 3 : phase==1 ? 4 : 5));gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static void show(void) { g_timeout_add(100,respond,NULL);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-selection-history-command"))); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 one=document_open_file(first,FALSE,NULL,NULL);two=document_open_file(second,FALSE,NULL,NULL);g_assert_nonnull(one);g_assert_nonnull(two);window=gtk_widget_get_toplevel(GTK_WIDGET(one->editor->sci));
 navqueue_goto_line(two,one,3);navqueue_goto_line(one,two,2);phase=0;show();g_assert_true(document_get_current()==one);g_assert_cmpint(sci_get_current_line(one->editor->sci),==,2);
 keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_GOTO_FORWARD);g_assert_true(document_get_current()==two);g_assert_cmpint(sci_get_current_line(two->editor->sci),==,1);
 keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_GOTO_BACK);g_assert_true(document_get_current()==one);
 scintilla_send_message(one->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"// prefix\n");phase=1;show();g_assert_cmpint(sci_get_current_line(one->editor->sci),==,3);
 phase=2;show();g_assert_cmpint(sci_get_current_line(one->editor->sci),==,4);
 g_assert_true(document_close(two));two=NULL;
 for(guint i=0;i<700;i++) navqueue_goto_line(one,one,1+i%2);
 phase=3;show();
 scintilla_send_message(one->editor->sci,SCI_SETSAVEPOINT,0,0);
 gchar *disk=NULL;g_assert_true(g_file_get_contents(first,&disk,NULL,NULL));g_assert_cmpstr(disk,==,"alpha\nβeta\ngamma\ndelta\n");g_free(disk);
 g_print("Selection History: cross-file Go To, Back/Forward, edit anchors, stale refresh, closed-file removal, bounded queue and unchanged disk passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("History GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("selection-history-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_mkdir_with_parents(profile,0700);first=g_build_filename(dir,"first.c",NULL);second=g_build_filename(dir,"second.c",NULL);
 g_assert_true(g_file_set_contents(first,"alpha\nβeta\ngamma\ndelta\n",-1,NULL));g_assert_true(g_file_set_contents(second,"one\ntwo\nthree\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 gchar *args[]={"selection-history","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(600,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(5,args);
}
