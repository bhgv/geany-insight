/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "insight-help.h"
#include <glib/gstdio.h>
static guint phase,hits;static GtkWidget *menu,*toggle,*palette;
static void clicked(GtkMenuItem *item,gpointer data) { (void)item;(void)data;hits++; }
static GtkWidget *find(GtkWidget *root,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) {GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *out=NULL;for(GList *p=children;p && !out;p=p->next) out=find(p->data,name);g_list_free(children);return out;}return NULL;
}
static GtkWidget *first_button(GtkWidget *root) {
 if(GTK_IS_BUTTON(root)) return root;
 if(GTK_IS_CONTAINER(root)) {GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *out=NULL;for(GList *p=children;p && !out;p=p->next) out=first_button(p->data);g_list_free(children);return out;}return NULL;
}
static void destroyed(GtkWidget *widget,gpointer data) { (void)widget;*(gboolean *)data=TRUE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 if(!phase) {
  menu=gtk_menu_new();g_object_ref_sink(menu);toggle=gtk_check_menu_item_new_with_label("Fixture toggle");gtk_menu_shell_append(GTK_MENU_SHELL(menu),toggle);g_signal_connect(toggle,"activate",G_CALLBACK(clicked),NULL);
  g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);
  insight_menu_icons(menu);gtk_widget_show_all(menu);
  GtkWidget *detach=find(menu,"insight-menu-detach");g_assert_nonnull(detach);gtk_menu_shell_select_item(GTK_MENU_SHELL(menu),detach);
  GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.keyval=GDK_KEY_Return;gboolean handled=FALSE;g_signal_emit_by_name(menu,"key-press-event",event,&handled);gdk_event_free(event);g_assert_true(handled);
  palette=g_object_get_data(G_OBJECT(menu),"insight-palette-window");g_assert_nonnull(palette);g_assert_true(gtk_widget_get_visible(palette));
 } else if(phase==1) {
  g_assert_false(gtk_widget_get_mapped(menu));GtkWidget *button=first_button(palette);g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button));g_assert_cmpuint(hits,==,1);g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(toggle)));g_assert_true(gtk_widget_get_visible(palette));
  gtk_widget_set_sensitive(toggle,FALSE);g_assert_false(gtk_widget_get_sensitive(button));gtk_widget_set_sensitive(toggle,TRUE);gtk_menu_item_set_label(GTK_MENU_ITEM(toggle),"Updated caption");g_assert_nonnull(g_strstr_len(gtk_button_get_label(GTK_BUTTON(button)),-1,"Updated caption"));
  g_signal_handlers_disconnect_by_func(menu,G_CALLBACK(gtk_widget_destroy),menu);gtk_widget_destroy(palette);g_assert_null(g_object_get_data(G_OBJECT(menu),"insight-palette-window"));
  gtk_menu_item_activate(GTK_MENU_ITEM(find(menu,"insight-menu-detach")));palette=g_object_get_data(G_OBJECT(menu),"insight-palette-window");g_assert_nonnull(palette);
  gboolean gone=FALSE;g_signal_connect(palette,"destroy",G_CALLBACK(destroyed),&gone);gtk_widget_destroy(menu);g_assert_true(gone);g_object_unref(menu);
  menu=gtk_menu_new();g_object_ref_sink(menu);gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_menu_item_new_with_label("Transient command"));g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);insight_menu_icons(menu);gtk_widget_show_all(menu);gtk_menu_item_activate(GTK_MENU_ITEM(find(menu,"insight-menu-detach")));palette=g_object_get_data(G_OBJECT(menu),"insight-palette-window");g_assert_nonnull(palette);gboolean cleaned=FALSE;g_signal_connect(menu,"destroy",G_CALLBACK(destroyed),&cleaned);gtk_widget_destroy(palette);g_assert_true(cleaned);g_object_unref(menu);
  g_print("Menu palette GUI: keyboard tear-off, retained transient popup, live state, repeated action and safe owner destruction passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 phase++;return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Menu palette timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("menu-palette-%u",g_random_int()),*profile=g_build_filename(cwd,name,NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *argv[]={"menu-palette-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(300,step,NULL);g_timeout_add_seconds(20,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(cwd);g_free(name);g_free(profile);g_free(config);return result;
}
