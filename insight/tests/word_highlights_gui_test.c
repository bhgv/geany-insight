/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GeanyDocument *source,*other;static GtkWidget *window,*manager;static guint phase,ticks;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static void action(const gchar *suffix) { gchar *name=g_strconcat("insight-word-highlights-",suffix,NULL);GtkWidget *item=named(window,name);g_free(name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static gboolean marked(GeanyDocument *doc,gint pos) { return SEND(doc->editor->sci,SCI_INDICATORVALUEAT,26,pos)!=0; }
static void rows(gint count) { GtkWidget *tree=named(manager,"insight-word-highlights-list");g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)),NULL),==,count); }
static gboolean step(gpointer unused) {
 (void)unused;if(++ticks>160) g_error("Word highlights timeout phase %u",phase);
 if(!source) { source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci)); }
 ScintillaObject *sci=source->editor->sci;
 switch(phase) {
 case 0:
  SEND(sci,SCI_SETSEL,0,3);action("toggle");g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,0);g_assert_cmpint(sci_get_current_position(sci),==,3);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));phase++;break;
 case 1:
  if(!marked(source,0) || !marked(source,4)) break;
  g_assert_false(marked(source,8));g_assert_false(marked(source,15));SEND(sci,SCI_SETSEL,0,0);action("manage");
  { GList *tops=gtk_window_list_toplevels();for(GList *p=tops;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-word-highlights-window")) manager=p->data;g_list_free(tops); }
  g_assert_nonnull(manager);rows(1);other=document_new_file("other.c",NULL,"foo fooBar FOO\n");SEND(other->editor->sci,SCI_SETSAVEPOINT,0,0);other->readonly=TRUE;SEND(other->editor->sci,SCI_SETREADONLY,1,0);phase++;break;
 case 2:
  if(!marked(other,0)) break;g_assert_false(marked(other,4));g_assert_false(marked(other,11));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(manager,"insight-word-highlights-case")),FALSE);phase++;break;
 case 3:
  if(!marked(other,11) || !marked(source,15)) break;
  { GtkWidget *page=GTK_WIDGET(sci);while(gtk_widget_get_parent(page) && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) page=gtk_widget_get_parent(page);GtkWidget *book=gtk_widget_get_parent(page);g_assert_true(GTK_IS_NOTEBOOK(book));gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),page)); }SEND(sci,SCI_SETSEL,0,3);action("next");g_assert_cmpint(SEND(sci,SCI_GETSELECTIONSTART,0,0),==,4);action("previous");g_assert_cmpint(SEND(sci,SCI_GETSELECTIONSTART,0,0),==,0);action("previous");g_assert_cmpint(SEND(sci,SCI_GETSELECTIONSTART,0,0),==,15);
  SEND(sci,SCI_INSERTTEXT,3,(sptr_t)"x");phase++;break;
 case 4:
  if(marked(source,0) || !marked(source,5)) break;
  SEND(sci,SCI_UNDO,0,0);phase++;break;
 case 5:
  if(!marked(source,0) || !marked(source,4)) break;
  { GdkRGBA rgba={0.25,0.5,0.75,1};GtkWidget *pick=named(manager,"insight-word-highlights-color");gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(pick),&rgba);g_signal_emit_by_name(pick,"color-set");g_assert_cmpint(SEND(sci,SCI_INDICGETFORE,26,0),==,0xbf8040); }
  SEND(sci,SCI_SETSEL,19,21);action("toggle");rows(2);phase++;break;
 case 6:
  if(!marked(source,19)) break;g_assert_false(marked(source,22));
  { GtkWidget *tree=named(manager,"insight-word-highlights-list");GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path);gtk_button_clicked(GTK_BUTTON(named(manager,"insight-word-highlights-remove")));rows(1); }phase++;break;
 case 7:
  if(marked(source,0) || marked(other,0)) break;g_assert_true(marked(source,19));
  action("clear");rows(0);phase++;break;
 case 8:
  if(marked(source,19)) break;
  g_assert_false(SEND(sci,SCI_GETMODIFY,0,0));g_assert_false(SEND(other->editor->sci,SCI_GETMODIFY,0,0));
  gtk_widget_destroy(manager);g_print("Word highlights: all buffers/new/read-only, Unicode boundaries, case/color, editing/undo, navigation/wrap, manager remove/clear, unchanged text passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("word-highlights-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL),*path=g_build_filename(dir,"source.c",NULL);g_assert_true(g_file_set_contents(path,"foo foo fooBar FOO\nα αβ\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *args[]={"word-highlights","--new-instance","--no-session","--config",dir,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(250,step,NULL);return main_lib(6,args);
}
