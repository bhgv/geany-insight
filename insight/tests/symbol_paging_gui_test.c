/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*local,*project,*file_controls,*project_controls;static gchar *directory,*profile,*source;static guint phase,project_page;
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!strcmp(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_ITEM(root)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
static gboolean text(GtkWidget *root,const gchar *value) { if(GTK_IS_LABEL(root) && !strcmp(gtk_label_get_text(GTK_LABEL(root)),value)) return TRUE;if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));gboolean found=FALSE;for(GList *p=c;p && !found;p=p->next) found=text(p->data,value);g_list_free(c);return found; }return FALSE; }
static gboolean find(GtkWidget *view,const gchar *name,GtkTreeIter *result) { GtkTreeModel *m=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter it;for(gboolean v=gtk_tree_model_get_iter_first(m,&it);v;v=gtk_tree_model_iter_next(m,&it)) { gchar *n=NULL;gtk_tree_model_get(m,&it,0,&n,-1);gboolean match=!strcmp(n,name);g_free(n);if(match) {*result=it;return TRUE;}GtkTreeIter child;if(gtk_tree_model_iter_children(m,&child,&it)) do {gtk_tree_model_get(m,&child,0,&n,-1);match=!strcmp(n,name);g_free(n);if(match) {*result=child;return TRUE;}}while(gtk_tree_model_iter_next(m,&child)); }return FALSE; }
static GtkWidget *more(GtkWidget *controls) { GtkWidget *button=named(controls,"insight-symbol-more");g_assert_true(GTK_IS_BUTTON(button));g_assert_null(gtk_button_get_label(GTK_BUTTON(button)));g_assert_true(GTK_IS_IMAGE(gtk_button_get_image(GTK_BUTTON(button))));g_assert_true(gtk_widget_get_visible(gtk_button_get_image(GTK_BUTTON(button))));return button; }
static void page(GtkWidget *controls) { GtkWidget *button=more(controls);g_assert_true(gtk_widget_get_visible(button));g_assert_true(gtk_widget_get_sensitive(button));gtk_button_clicked(GTK_BUTTON(button)); }
static gboolean timeout(gpointer data) { (void)data;g_error("Symbol paging GUI timeout at phase %u / project page %u",phase,project_page);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(phase==0) { document_open_file(source,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!local) { local=named(window,"insight-local-symbols");project=named(window,"insight-project-symbols");file_controls=named(window,"insight-local-type-filters");project_controls=named(window,"insight-project-type-filters"); }
 GtkTreeIter it;if(!gtk_widget_get_sensitive(local)) return G_SOURCE_CONTINUE;
 if(phase==1) {
  if(!text(file_controls,"Symbols: 0 / 2000+")) return G_SOURCE_CONTINUE;
  g_assert_false(find(local,"Z_after_limit",&it));gint caret=sci_get_current_position(doc->editor->sci);page(file_controls);g_assert_false(gtk_widget_get_sensitive(local));g_assert_false(gtk_widget_get_sensitive(more(file_controls)));g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);phase++;
 } else if(phase==2) {
  if(!text(file_controls,"Symbols: 1 / 2301")) return G_SOURCE_CONTINUE;
  g_assert_true(find(local,"Z_after_limit",&it));g_assert_false(gtk_widget_get_visible(more(file_controls)));g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,0);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"Z_after_limit");phase++;
 } else if(phase==3) {
  if(!text(file_controls,"Symbols: 1 / 1")) return G_SOURCE_CONTINUE;
  g_assert_false(gtk_widget_get_visible(more(file_controls)));gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"");phase++;
 } else if(phase==4) {
  if(!text(file_controls,"Symbols: 0 / 2000+")) return G_SOURCE_CONTINUE;
  g_assert_true(gtk_widget_get_visible(more(file_controls)));phase++;
 } else if(phase==5) {
  if(!gtk_widget_get_sensitive(project)) return G_SOURCE_CONTINUE;
  if(project_page<4) { gchar *expected=g_strdup_printf("Symbols: 0 / %u+",500*(project_page+1));gboolean ready=text(project_controls,expected);g_free(expected);if(!ready) return G_SOURCE_CONTINUE;if(!project_page) {
   GtkWidget *node=project;while(gtk_widget_get_parent(node) && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(node))) node=gtk_widget_get_parent(node);GtkNotebook *book=GTK_NOTEBOOK(gtk_widget_get_parent(node));gtk_notebook_set_current_page(book,gtk_notebook_page_num(book,node));gtk_widget_grab_focus(project);
   GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(window));event->key.keyval=GDK_KEY_Menu;gdk_event_set_device(event,gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default())));gtk_main_do_event(event);gdk_event_free(event);
   GtkWidget *menu=g_object_get_data(G_OBJECT(project),"insight-symbol-popup");g_assert_nonnull(menu);gtk_menu_popdown(GTK_MENU(menu));GtkWidget *command=named(menu,"insight-symbol-command-12");g_assert_true(gtk_widget_get_sensitive(command));g_assert_true(GTK_IS_IMAGE(gtk_image_menu_item_get_image(GTK_IMAGE_MENU_ITEM(command))));gtk_menu_item_activate(GTK_MENU_ITEM(command));
  }else page(project_controls);project_page++; }
  else { if(!text(project_controls,"Symbols: 1 / 2301")) return G_SOURCE_CONTINUE;
  g_assert_true(find(project,"Z_after_limit",&it));g_assert_false(gtk_widget_get_visible(more(project_controls)));gtk_entry_set_text(GTK_ENTRY(named(window,"insight-symbol-query")),"Z_after_limit");phase++; }
 } else if(phase==6) {
  if(!gtk_widget_get_sensitive(project) || !text(project_controls,"Symbols: 1 / 1")) return G_SOURCE_CONTINUE;
  gtk_entry_set_text(GTK_ENTRY(named(window,"insight-symbol-query")),"");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);phase++;
 } else if(phase==7) {
  if(!gtk_widget_get_sensitive(project) || !text(project_controls,"Символы: 0 / 500+")) return G_SOURCE_CONTINUE;
  gchar *tip=gtk_widget_get_tooltip_text(more(project_controls));g_assert_cmpstr(tip,==,"Загрузить следующие 500 символов");g_free(tip);tip=gtk_widget_get_tooltip_text(more(file_controls));g_assert_cmpstr(tip,==,"Загрузить следующие 2000 символов");g_free(tip);
  /* Query generations must discard an in-flight expanded result. */
  page(file_controls);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-local-query")),"Z_after_limit");phase++;
 } else {
  if(!text(file_controls,"Символы: 1 / 1")) return G_SOURCE_CONTINUE;
  g_assert_true(find(local,"Z_after_limit",&it));g_assert_false(gtk_widget_get_visible(more(file_controls)));g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,0);
  g_print("Symbol paging GUI passed: 2301 native C tags, function beyond initial file/project limits, 2000/500-row page increments, explicit partial counters, icon-only controls with visible images, no editor movement, query resets, stale page cancellation, RU counters and dynamic tooltips\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("symbol-paging-gui-%u",g_random_int());directory=g_build_filename(cwd,name,NULL);profile=g_build_filename(directory,"profile",NULL);source=g_build_filename(directory,"large.c",NULL);g_free(cwd);g_free(name);gchar *folder=g_build_filename(profile,"insight",NULL);g_assert_cmpint(g_mkdir_with_parents(folder,0700),==,0);gchar *settings=g_build_filename(folder,"workbench.conf",NULL);g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nvisible=true\nproject-visible=true\nfile-symbol-types=1\nproject-symbol-types=1\nproject-symbols-grouped=true\n",-1,NULL));g_free(settings);g_free(folder);GString *code=g_string_new(NULL);for(guint i=0;i<2300;i++) g_string_append_printf(code,"#define A_MACRO_%04u %u\n",i,i);g_string_append(code,"int Z_after_limit(void) { return 0; }\n");g_assert_true(g_file_set_contents(source,code->str,code->len,NULL));g_string_free(code,TRUE);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);gchar *argv[]={"symbol-paging-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(40,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);g_free(source);return result;
}
