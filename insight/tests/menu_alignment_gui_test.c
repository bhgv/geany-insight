/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window;static guint phase;
static GtkWidget *named(GtkWidget *root,const char *name) { if(!strcmp(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_ITEM(root)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(sub) { GtkWidget *found=named(sub,name);if(found) return found; } }if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=c;p && !found;p=p->next) found=named(p->data,name);g_list_free(c);return found; }return NULL; }
static void aligned(void) {
 GtkWidget *bar=ui_lookup_widget(window,"menubar1");GList *items=gtk_container_get_children(GTK_CONTAINER(bar));gint minimum=G_MAXINT,maximum=G_MININT;guint count=0;
 for(GList *p=items;p;p=p->next) {
  GtkWidget *item=p->data;if(!GTK_IS_MENU_ITEM(item) || !gtk_widget_get_visible(item)) continue;
  GtkWidget *label=gtk_bin_get_child(GTK_BIN(item));g_assert_true(GTK_IS_LABEL(label));const gchar *text=gtk_label_get_text(GTK_LABEL(label));g_assert_null(strchr(text,'\r'));g_assert_null(strchr(text,'\n'));
  gint x=0,y=0,lx=0,ly=0;g_assert_true(gtk_widget_translate_coordinates(label,bar,0,0,&x,&y));gtk_label_get_layout_offsets(GTK_LABEL(label),&lx,&ly);
  gint baseline=y+ly+PANGO_PIXELS(pango_layout_get_baseline(gtk_label_get_layout(GTK_LABEL(label))));minimum=MIN(minimum,baseline);maximum=MAX(maximum,baseline);count++;
  g_printerr("Menu baseline %d: %s\n",baseline,text);
 }
 g_list_free(items);g_assert_cmpuint(count,>=,10);g_assert_cmpint(maximum-minimum,<=,1);
}
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { gtk_window_resize(GTK_WINDOW(window),1400,800);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE); }
 else if(phase==1) { aligned();gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE); }
 else if(phase==2) { aligned();gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE); }
 else { aligned();g_print("Menu alignment GUI: all main-menu text baselines match in EN/RU/EN; no CR/LF in labels\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 phase++;return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Menu alignment GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("menu-align-%u",g_random_int()),*profile=g_build_filename(cwd,name,NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *argv[]={"menu-alignment-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(300,step,NULL);g_timeout_add_seconds(20,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(cwd);g_free(name);g_free(profile);g_free(config);return result;
}
