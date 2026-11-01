/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "../../src/insight-symbol-view.h"
#include "../../src/insight-virtual-list.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *source;static guint phase;static GtkWidget *window,*project,*entry;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_CONTAINER(w)) {GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=list;p&&!found;p=p->next) found=named(p->data,name);g_list_free(list);return found;}return NULL;
}
static gboolean has(GtkWidget *view,const gchar *name) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter it;
 for(gboolean valid=gtk_tree_model_get_iter_first(model,&it);valid;valid=gtk_tree_model_iter_next(model,&it)) {
  gchar *value=NULL;gtk_tree_model_get(model,&it,0,&value,-1);gboolean same=!g_strcmp0(value,name);g_free(value);if(same)return TRUE;
 }return FALSE;
}
static gboolean step(gpointer unused) {
 (void)unused;GeanyDocument *doc=document_get_current();if(!doc)return G_SOURCE_CONTINUE;
 if(!window) {window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));project=named(window,"insight-project-symbols");entry=named(window,"insight-symbol-query");}
 if(!phase) {document_open_file(source,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE;}
 if(!gtk_widget_get_sensitive(project) || insight_virtual_list_loading(GTK_TREE_VIEW(project))) return G_SOURCE_CONTINUE;
 if(phase==1) {
  insight_symbol_view_set_grouped(GTK_TREE_VIEW(project),FALSE);insight_symbol_view_set_scopes(GTK_TREE_VIEW(project),FALSE);
  insight_symbol_view_set_types(GTK_TREE_VIEW(project),0x7ff);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {
  if(insight_virtual_list_count(GTK_TREE_VIEW(project))!=2301) return G_SOURCE_CONTINUE;
  g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(project)),NULL),<=,256);
  g_assert_false(has(project,"Z_after_limit"));gtk_entry_set_text(GTK_ENTRY(entry),"Z_after_limit");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==3) {
  if(insight_virtual_list_count(GTK_TREE_VIEW(project))!=1 || !has(project,"Z_after_limit")) return G_SOURCE_CONTINUE;
  gtk_entry_set_text(GTK_ENTRY(entry),"");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==4) {
  if(insight_virtual_list_count(GTK_TREE_VIEW(project))!=2301) return G_SOURCE_CONTINUE;
  insight_virtual_list_position(GTK_TREE_VIEW(project),2301);phase++;return G_SOURCE_CONTINUE;
 }
 if(!has(project,"Z_after_limit")) return G_SOURCE_CONTINUE;
 g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(project)),NULL),<=,256);
 g_print("Virtual project symbols: global search beyond window, 2301 results, bounded rows and jump to final symbol passed\n");
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) {(void)data;g_error("Virtual project symbols timeout phase %u",phase);return G_SOURCE_REMOVE;}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS")||!g_getenv("DISPLAY"))return 77;
 gchar *dir=g_dir_make_tmp("virtual-symbols-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL);g_mkdir(profile,0700);
 gchar *config=g_build_filename(profile,"geany.conf",NULL);g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL);g_free(config);
 source=g_build_filename(dir,"large.c",NULL);GString *code=g_string_new(NULL);for(guint n=0;n<2300;n++)g_string_append_printf(code,"#define A_MACRO_%04u %u\n",n,n);g_string_append(code,"int Z_after_limit(void) { return 0; }\n");g_file_set_contents(source,code->str,code->len,NULL);g_string_free(code,TRUE);
 gchar *args[]={"virtual-symbols","--new-instance","--no-session","--config",profile,NULL};
 g_timeout_add(100,step,NULL);g_timeout_add_seconds(45,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);return main_lib(5,args);
}
