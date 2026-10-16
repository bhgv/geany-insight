/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *root,*profile,*left,*right,*source;static guint phase;static GtkWidget *directory,*merge;
static GtkWidget *named(GtkWidget *widget,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(widget),name)) return widget;
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *window_named(const gchar *name) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;
 for(GList *p=windows;p && !found;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),name)) found=p->data;
 g_list_free(windows);return found;
}
static gboolean step(gpointer data) {
 (void)data;
 if(!phase) {
  if(!main_widgets.tools_menu) return G_SOURCE_CONTINUE;
  GtkWidget *item=named(main_widgets.tools_menu,"insight-directory-menu");if(!item) return G_SOURCE_CONTINUE;
  gtk_menu_item_activate(GTK_MENU_ITEM(item));directory=window_named("insight-directory-window");g_assert_nonnull(directory);
  gtk_entry_set_text(GTK_ENTRY(named(directory,"insight-directory-left")),left);gtk_entry_set_text(GTK_ENTRY(named(directory,"insight-directory-right")),right);
  gtk_button_clicked(GTK_BUTTON(named(directory,"insight-directory-scan")));phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==1) {
  if(!gtk_widget_get_sensitive(named(directory,"insight-directory-scan"))) return G_SOURCE_CONTINUE;
  GtkWidget *view=named(directory,"insight-directory-files");GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,1);
  GtkTreePath *path=gtk_tree_path_new_first();gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE);gtk_tree_path_free(path);
  GtkWidget *open=named(directory,"insight-directory-open");g_assert_true(gtk_widget_get_sensitive(open));gtk_button_clicked(GTK_BUTTON(open));merge=window_named("insight-diff-window");g_assert_nonnull(merge);phase++;return G_SOURCE_CONTINUE;
 }
 if(!gtk_widget_get_sensitive(named(merge,"insight-diff-compare"))) return G_SOURCE_CONTINUE;
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(merge,"insight-diff-result")));GtkTextIter a,z;gtk_text_buffer_get_bounds(buffer,&a,&z);gchar *text=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);g_assert_cmpstr(text,==,"port = 3000\n");g_free(text);
 gtk_widget_destroy(merge);gtk_widget_destroy(directory);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Directory Compare native GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 root=g_dir_make_tmp("insight-directory-native-XXXXXX",NULL);profile=g_build_filename(root,"profile",NULL);left=g_build_filename(root,"left",NULL);right=g_build_filename(root,"right",NULL);
 g_assert_cmpint(g_mkdir(profile,0700),==,0);g_assert_cmpint(g_mkdir(left,0700),==,0);g_assert_cmpint(g_mkdir(right,0700),==,0);
 source=g_build_filename(left,"config.ini",NULL);gchar *other=g_build_filename(right,"config.ini",NULL),*config=g_build_filename(profile,"geany.conf",NULL);
 g_assert_true(g_file_set_contents(source,"port = 3000\n",-1,NULL));g_assert_true(g_file_set_contents(other,"port = 8080\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 gchar *args[]={"directory-native","--new-instance","--no-session","--config",profile,source,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 gint result=main_lib(6,args);g_free(other);g_free(config);g_free(source);g_free(left);g_free(right);g_free(profile);g_free(root);return result;
}
