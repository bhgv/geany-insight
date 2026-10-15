/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <stdlib.h>
static gboolean restore;static gchar *expected_path;static guint phase;
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=list;p && !found;p=p->next) found=named(p->data,name);g_list_free(list);return found; }return NULL; }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 GtkWidget *window=gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));GtkWidget *panes[]={ui_lookup_widget(window,"hpaned1"),named(window,"insight-project-split"),ui_lookup_widget(window,"vpaned1")};
 if(!phase) { phase++;return G_SOURCE_CONTINUE; }
 if(phase==1) {
  if(!restore) { GtkWidget *context=named(window,"insight-context");g_assert_true(gtk_widget_get_mapped(context));GtkWidget *book=gtk_widget_get_parent(context);g_assert_true(gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),gtk_notebook_get_current_page(GTK_NOTEBOOK(book)))==context); }
  if(restore) { GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,expected_path,G_KEY_FILE_NONE,NULL));for(guint i=0;i<3;i++) { gchar *name=g_strdup_printf("p%u",i);g_assert_cmpint(abs(gtk_paned_get_position(GTK_PANED(panes[i]))-g_key_file_get_integer(key,"positions",name,NULL)),<=,2);g_free(name); }g_key_file_unref(key); }
  else { g_assert_cmpint(abs(gtk_paned_get_position(GTK_PANED(panes[0]))-100),<=,5);g_assert_cmpint(abs(gtk_widget_get_allocated_width(panes[1])-gtk_paned_get_position(GTK_PANED(panes[1]))-100),<=,8);g_assert_cmpint(abs(gtk_paned_get_position(GTK_PANED(panes[2]))-gtk_widget_get_allocated_height(panes[2])*3/4),<=,8); }
  for(guint i=0;i<3;i++) { gtk_paned_set_position(GTK_PANED(panes[i]),16); }phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) { for(guint i=0;i<3;i++) g_assert_cmpint(abs(gtk_paned_get_position(GTK_PANED(panes[i]))-16),<=,2);
  gtk_paned_set_position(GTK_PANED(panes[0]),147);gtk_paned_set_position(GTK_PANED(panes[1]),MAX(0,gtk_widget_get_allocated_width(panes[1])-173));gtk_paned_set_position(GTK_PANED(panes[2]),MAX(0,gtk_widget_get_allocated_height(panes[2])-183));phase++;return G_SOURCE_CONTINUE;
 }
 GKeyFile *key=g_key_file_new();for(guint i=0;i<3;i++) { gchar *name=g_strdup_printf("p%u",i);g_key_file_set_integer(key,"positions",name,gtk_paned_get_position(GTK_PANED(panes[i])));g_free(name); }g_assert_true(g_key_file_save_to_file(key,expected_path,NULL));g_key_file_unref(key);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Panel geometry timeout");return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 if(argc==1) { gchar *directory=g_dir_make_tmp("panel-geometry-XXXXXX",NULL);g_assert_nonnull(directory);gchar *profile=g_build_filename(directory,"profile",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
  for(guint i=0;i<2;i++) { gchar *child[]={argv[0],profile,i ? "restore" : "fresh",NULL};gint status=0;GError *error=NULL;g_assert_true(g_spawn_sync(NULL,child,NULL,0,NULL,NULL,NULL,NULL,&status,&error));g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error); }
  g_free(profile);g_free(config);g_free(directory);g_print("Panel geometry GUI: first-run widths/quarter Context, collapse, drag positions and full process restart restoration passed\n");return 0;
 }
 restore=!g_strcmp0(argv[2],"restore");expected_path=g_build_filename(argv[1],"expected.ini",NULL);gchar *args[]={"geometry-gui","--new-instance","--no-session","--config",argv[1],NULL};g_timeout_add(350,step,NULL);g_timeout_add_seconds(25,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,args);g_free(expected_path);return result;
}
