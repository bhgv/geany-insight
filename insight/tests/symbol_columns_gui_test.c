/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*local,*project;static gchar *profile,*path,*settings;static guint phase;static gint caret;
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!strcmp(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_ITEM(root)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
static void columns(GtkWidget *view,const gint *fields,const gint *widths,guint count) { GList *list=gtk_tree_view_get_columns(GTK_TREE_VIEW(view));g_assert_cmpuint(g_list_length(list),==,count);guint i=0;for(GList *p=list;p;p=p->next,i++) { GtkTreeViewColumn *column=p->data;g_assert_cmpint(gtk_tree_view_column_get_sort_column_id(column),==,fields[i]);g_assert_cmpint(gtk_tree_view_column_get_fixed_width(column),==,widths[i]);g_assert_true(gtk_tree_view_column_get_resizable(column));g_assert_true(gtk_tree_view_column_get_reorderable(column));g_assert_cmpint(gtk_tree_view_column_get_sizing(column),==,GTK_TREE_VIEW_COLUMN_FIXED); }g_list_free(list); }
static GtkWidget *popup(GtkWidget *target) { gtk_widget_grab_focus(target);GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(window));event->key.keyval=GDK_KEY_Menu;gdk_event_set_device(event,gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default())));gtk_main_do_event(event);gdk_event_free(event);GtkWidget *menu=g_object_get_data(G_OBJECT(target),"insight-symbol-popup");g_assert_nonnull(menu);gtk_menu_popdown(GTK_MENU(menu));return menu; }
static const gint initial[]={3,0,4},initial_widths[]={61,210,83},file_defaults[]={0,3,4},file_widths[]={220,70,70},project_defaults[]={0,1,2,3,4},project_widths[]={220,160,260,70,70},changed[]={4,3,0},changed_widths[]={145,61,210};
static gboolean timeout(gpointer data) { (void)data;g_error("Symbol columns GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(path,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!local) { local=named(window,"insight-local-symbols");project=named(window,"insight-project-symbols"); }
 if(!gtk_widget_get_sensitive(local) || !gtk_widget_get_sensitive(project)) return G_SOURCE_CONTINUE;
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(local));
 if(phase==1) {
  if(gtk_tree_model_iter_n_children(model,NULL)!=2) return G_SOURCE_CONTINUE;
  columns(local,initial,initial_widths,3);columns(project,project_defaults,project_widths,5);caret=sci_get_current_position(doc->editor->sci);
  GtkTreeViewColumn *type=gtk_tree_view_get_column(GTK_TREE_VIEW(local),2);gtk_tree_view_column_set_fixed_width(type,145);gtk_tree_view_move_column_after(GTK_TREE_VIEW(local),type,NULL);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-local-group-types")),TRUE);phase++;
 } else if(phase==2) {
  if(!GTK_IS_TREE_STORE(model)) return G_SOURCE_CONTINUE;
  columns(local,changed,changed_widths,3);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"alpha");phase++;
 } else if(phase==3) {
  if(gtk_tree_model_iter_n_children(model,NULL)!=1) return G_SOURCE_CONTINUE;
  columns(local,changed,changed_widths,3);GtkWidget *menu=popup(local),*reset=g_object_get_data(G_OBJECT(menu),"insight-symbol-command-14");g_assert_true(gtk_widget_get_sensitive(reset));gtk_menu_item_activate(GTK_MENU_ITEM(reset));columns(local,file_defaults,file_widths,3);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(window,"insight-local-query"))),==,"alpha");columns(project,project_defaults,project_widths,5);
  GtkTreeViewColumn *last=gtk_tree_view_get_column(GTK_TREE_VIEW(project),4);gtk_tree_view_column_set_fixed_width(last,123);gtk_tree_view_move_column_after(GTK_TREE_VIEW(project),last,NULL);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);phase++;
 } else {
  columns(local,file_defaults,file_widths,3);const gint fields[]={4,0,1,2,3},widths[]={123,220,160,260,70};columns(project,fields,widths,5);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);GtkWidget *menu=popup(local),*reset=g_object_get_data(G_OBJECT(menu),"insight-symbol-command-14");g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(reset)),==,"Восстановить колонки символов");
  g_print("Symbol columns GUI passed: independent restore, invalid duplicate layout rejected, resizable/reorderable columns, group/query rebuild preservation, reset menu, RU label, unchanged editor and independent settings write\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
static void saved(GKeyFile *key,const gchar *name,const gint *expected,gsize count) { gsize n=0;gint *values=g_key_file_get_integer_list(key,"workbench",name,&n,NULL);g_assert_cmpuint(n,==,count);for(gsize i=0;i<count;i++) g_assert_cmpint(values[i],==,expected[i]);g_free(values); }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("symbol-columns-gui-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL);g_free(cwd);g_free(name);profile=g_build_filename(directory,"profile",NULL);path=g_build_filename(directory,"sample.c",NULL);gchar *sd=g_build_filename(profile,"insight",NULL);g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);settings=g_build_filename(sd,"workbench.conf",NULL);g_free(sd);
 g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nproject-visible=true\nfile-symbols-column-order=3;0;4;\nfile-symbols-column-widths=61;210;83;\nproject-symbols-column-order=0;1;1;3;4;\nproject-symbols-column-widths=800;800;800;800;800;\n",-1,NULL));g_assert_true(g_file_set_contents(path,"int alpha(void) { return 1; }\nint sample_variable;\n",-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"symbol-columns-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(200,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);
 GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL));saved(key,"file-symbols-column-order",file_defaults,3);saved(key,"file-symbols-column-widths",file_widths,3);const gint fields[]={4,0,1,2,3},widths[]={123,220,160,260,70};saved(key,"project-symbols-column-order",fields,5);saved(key,"project-symbols-column-widths",widths,5);g_key_file_unref(key);g_free(directory);g_free(profile);g_free(path);g_free(settings);return result;
}
