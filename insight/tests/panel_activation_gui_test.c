/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "keybindings.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window;static GeanyDocument *source;static guint phase,ticks;
static const gchar *targets[]={"insight-file-query","insight-folder-query","insight-symbol-query","insight-relation-query","insight-bookmarks-query","insight-clips-query","insight-type-query","insight-category-query","insight-local-query","insight-context-code","insight-window-filter","insight-file-search-query","insight-project-query","insight-search-results"};
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *v=named(m,name);if(v) return v; } }if(GTK_IS_CONTAINER(w)) { GList *c=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *v=NULL;for(GList *p=c;p && !v;p=p->next) v=named(p->data,name);g_list_free(c);return v; }return NULL; }
static GtkWidget *find(const gchar *name) { GList *windows=gtk_window_list_toplevels();GtkWidget *w=NULL;for(GList *p=windows;p && !w;p=p->next) w=named(p->data,name);g_list_free(windows);return w; }
static void at(guint action) { GtkWidget *target=find(targets[action]);g_assert_nonnull(target);GtkWidget *owner=gtk_widget_get_toplevel(target);g_assert_true(gtk_widget_get_visible(owner));g_assert_true(gtk_window_get_focus(GTK_WINDOW(owner))==target);g_assert_true(gtk_widget_is_visible(target)); }
static void menu(guint action) { gchar *id=g_strdup_printf("insight-activate-panel-%u",action);GtkWidget *w=named(window,id);g_free(id);g_assert_nonnull(w);gtk_menu_item_activate(GTK_MENU_ITEM(w)); }
static GtkWidget *panel(GtkWidget *w) { while(w && !g_object_get_data(G_OBJECT(w),"insight-panel-id")) w=gtk_widget_get_parent(w);g_assert_nonnull(w);return w; }
static gboolean step(gpointer unused) {
 (void)unused;if(++ticks>100) g_error("Panel activation timeout phase %u",phase);source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));if(!named(window,"insight-activate-panel-14")) return G_SOURCE_CONTINUE;
 if(phase<14) { menu(phase);at(phase);if(GTK_IS_ENTRY(find(targets[phase]))) { gtk_entry_set_text(GTK_ENTRY(find(targets[phase])),"keep");keybindings_send_command(GEANY_KEY_GROUP_FOCUS,GEANY_KEYS_INSIGHT_ACTIVATEPROJECTFILES+phase);at(phase);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(find(targets[phase]))),==,"keep"); }phase++;return G_SOURCE_CONTINUE; }
 if(phase==14) {
  menu(14);g_assert_true(gtk_window_get_focus(GTK_WINDOW(window))==GTK_WIDGET(source->editor->sci));GtkWidget *book=named(window,"insight-project-notebook");gtk_widget_hide(book);menu(0);at(0);g_assert_true(gtk_widget_get_visible(book));
  GtkWidget *page=panel(find(targets[7]));gtk_button_clicked(GTK_BUTTON(named(page,"insight-panel-toggle-dock")));GtkWidget *owner=gtk_widget_get_toplevel(page);g_assert_true(owner!=window);gtk_button_clicked(GTK_BUTTON(named(page,"insight-panel-minimize")));g_assert_false(gtk_widget_get_visible(owner));menu(7);at(7);gtk_button_clicked(GTK_BUTTON(named(page,"insight-panel-toggle-dock")));menu(7);at(7);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==15) { keybindings_send_command(GEANY_KEY_GROUP_FOCUS,GEANY_KEYS_INSIGHT_ACTIVATEEDITOR);g_assert_true(gtk_window_get_focus(GTK_WINDOW(window))==GTK_WIDGET(source->editor->sci));gchar *text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,"int value = 1;\n");g_free(text);g_assert_false(source->changed);g_assert_false(scintilla_send_message(source->editor->sci,SCI_CANUNDO,0,0));g_print("Panel activation: 15 menu/key commands, preserved queries/source, hidden project and detached minimized panel restoration passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
int main(void) { if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;gchar *dir=g_dir_make_tmp("panel-activation-XXXXXX",NULL),*file=g_build_filename(dir,"source.c",NULL),*config=g_build_filename(dir,"geany.conf",NULL);g_file_set_contents(file,"int value = 1;\n",-1,NULL);g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL);gchar *args[]={"panel-activation","--new-instance","--no-session","--config",dir,file,NULL};g_timeout_add(200,step,NULL);return main_lib(6,args); }
