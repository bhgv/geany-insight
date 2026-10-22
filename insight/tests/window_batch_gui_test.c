/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window,*panel,*view;static GeanyDocument *a,*b,*ro;static gchar *folder,*path_a,*path_b;static guint phase,ticks,mutations;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static void click(const gchar *suffix) { gchar *name=g_strconcat("insight-window-",suffix,NULL);GtkWidget *button=named(panel,name);g_free(name);g_assert_nonnull(button);g_assert_true(gtk_widget_get_sensitive(button));gtk_button_clicked(GTK_BUTTON(button)); }
static void count(gint expected) { g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,expected); }
static void selected(gint expected) { g_assert_cmpint(gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(view))),==,expected); }
static void changed(GtkTreeModel *model,GtkTreePath *path,gpointer data) { (void)model;(void)path;(void)data;mutations++; }
static gboolean cancel_dialog(gpointer data) { (void)data;GList *tops=gtk_window_list_toplevels();gboolean done=FALSE;for(GList *p=tops;p && !done;p=p->next) if(GTK_IS_DIALOG(p->data) && (GTK_IS_FILE_CHOOSER(p->data) || GTK_IS_MESSAGE_DIALOG(p->data))) { gtk_dialog_response(GTK_DIALOG(p->data),GTK_RESPONSE_CANCEL);done=TRUE; }g_list_free(tops);return done ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE; }
static gboolean step(gpointer data) {
 (void)data;if(++ticks>160) g_error("Window batch timeout phase %u",phase);
 if(phase==0) {
  a=document_get_current();if(!a) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(a->editor->sci));
  b=document_open_file(path_b,FALSE,NULL,NULL);ro=document_new_file("readonly.c",NULL,"read only\n");ro->readonly=TRUE;SEND(ro->editor->sci,SCI_SETREADONLY,TRUE,0);
  SEND(a->editor->sci,SCI_APPENDTEXT,2,(sptr_t)"A\n");SEND(b->editor->sci,SCI_APPENDTEXT,2,(sptr_t)"B\n");
  gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-window-list-command")));GList *tops=gtk_window_list_toplevels();for(GList *p=tops;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-window-list-window")) panel=p->data;g_list_free(tops);g_assert_nonnull(panel);view=named(panel,"insight-window-list");count(3);click("all");selected(3);g_assert_false(gtk_widget_get_sensitive(named(panel,"insight-window-activate")));phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==1) {
  click("invert");selected(0);g_assert_false(gtk_widget_get_sensitive(named(panel,"insight-window-save")));click("all");click("paths");gchar *copy=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(copy);g_assert_nonnull(strstr(copy,path_a));g_assert_nonnull(strstr(copy,path_b));g_free(copy);
  gtk_combo_box_set_active(GTK_COMBO_BOX(named(panel,"insight-window-scope")),1);count(2);selected(2);click("save");count(0);
  gchar *text=NULL;g_assert_true(g_file_get_contents(path_a,&text,NULL,NULL));g_assert_cmpstr(text,==,"a\nA\n");g_free(text);g_assert_true(g_file_get_contents(path_b,&text,NULL,NULL));g_assert_cmpstr(text,==,"b\nB\n");g_free(text);
  gtk_combo_box_set_active(GTK_COMBO_BOX(named(panel,"insight-window-scope")),2);count(1);g_assert_false(gtk_widget_get_sensitive(named(panel,"insight-window-save")));gtk_combo_box_set_active(GTK_COMBO_BOX(named(panel,"insight-window-scope")),3);count(1);gtk_combo_box_set_active(GTK_COMBO_BOX(named(panel,"insight-window-scope")),0);count(3);click("all");selected(3);
  g_signal_connect(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),"row-deleted",G_CALLBACK(changed),NULL);SEND(b->editor->sci,SCI_GOTOPOS,0,0);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) { if(ticks<8) return G_SOURCE_CONTINUE;g_assert_cmpuint(mutations,==,0);selected(3);
  /* Save As cancellation must stop the batch before asking about another file. */
  g_timeout_add(50,cancel_dialog,NULL);click("save-as");count(3);selected(3);
  gtk_entry_set_text(GTK_ENTRY(named(panel,"insight-window-filter")),"a.c");count(1);SEND(a->editor->sci,SCI_APPENDTEXT,2,(sptr_t)"X\n");g_timeout_add(50,cancel_dialog,NULL);click("close");g_assert_true(a->is_valid);g_assert_true(a->changed);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==3) {
  document_set_text_changed(a,FALSE);gtk_entry_set_text(GTK_ENTRY(named(panel,"insight-window-filter")),"");click("all");
  /* The read-only untitled fixture is clean; closing the batch needs no prompts. */
  document_set_text_changed(ro,FALSE);click("close");count(0);
  g_print("Window batch GUI: multiple selection/inversion, filters, copy paths, save selected, unchanged-model stability, Save As/close cancellation and batch close passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;folder=g_dir_make_tmp("window-batch-XXXXXX",NULL);path_a=g_build_filename(folder,"a.c",NULL);path_b=g_build_filename(folder,"b.c",NULL);gchar *config=g_build_filename(folder,"geany.conf",NULL);g_assert_true(g_file_set_contents(path_a,"a\n",-1,NULL));g_assert_true(g_file_set_contents(path_b,"b\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nuse_native_windows_dialogs=false\n",-1,NULL));gchar *args[]={"window-batch","--new-instance","--no-session","--config",folder,path_a,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(250,step,NULL);return main_lib(6,args);
}
