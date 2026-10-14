/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*view,*entry,*button,*project_button;static gchar *profile,*path,*settings;static guint phase;static gint caret,preview_line;
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!strcmp(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_ITEM(root)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(sub) { GtkWidget *f=named(sub,name);if(f) return f; } }if(GTK_IS_CONTAINER(root)) { GList *c=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *f=NULL;for(GList *p=c;p && !f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
static GtkWidget *history(GtkWidget *button) { GtkWidget *menu=GTK_WIDGET(gtk_menu_button_get_popup(GTK_MENU_BUTTON(button)));gtk_widget_hide(menu);gtk_widget_show(menu);return menu; }
static GtkWidget *item(GtkWidget *menu,guint i) { gchar *name=g_strdup_printf("insight-symbol-history-%u",i);GtkWidget *result=named(menu,name);g_free(name);return result; }
static void query_is(GtkWidget *menu,guint i,const gchar *query) { GtkWidget *row=item(menu,i);g_assert_nonnull(row);g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(row)),==,query); }
static void remember(const gchar *query) { gtk_entry_set_text(GTK_ENTRY(entry),query);GtkWidget *menu=history(button),*command=named(menu,"insight-symbol-history-remember");g_assert_true(gtk_widget_get_sensitive(command));gtk_menu_item_activate(GTK_MENU_ITEM(command));gtk_widget_hide(menu); }
static gboolean timeout(gpointer data) { (void)data;g_error("Symbol history GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(path,FALSE,NULL,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(!view) { view=named(window,"insight-local-symbols");entry=named(window,"insight-local-query");button=named(window,"insight-local-query-history");project_button=named(window,"insight-project-query-history"); }
 if(!gtk_widget_get_sensitive(view)) return G_SOURCE_CONTINUE;
 if(phase==1) {
  GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));if(gtk_tree_model_iter_n_children(model,NULL)!=1) return G_SOURCE_CONTINUE;
  g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)),==,"");g_assert_null(gtk_button_get_label(GTK_BUTTON(button)));g_assert_true(GTK_IS_IMAGE(gtk_button_get_image(GTK_BUTTON(button))));GtkWidget *menu=history(button);query_is(menu,0,"alpha");query_is(menu,1,"beta");query_is(menu,2,"Symbol");g_assert_null(item(menu,3));g_assert_false(gtk_widget_get_sensitive(named(menu,"insight-symbol-history-remember")));gtk_widget_hide(menu);menu=history(project_button);query_is(menu,0,"project_only");gtk_widget_hide(menu);
  caret=sci_get_current_position(doc->editor->sci);preview_line=sci_get_current_line(SCINTILLA(named(window,"insight-context-code")));remember("beta");phase++;
 } else if(phase==2) {
  GtkWidget *menu=history(button);query_is(menu,0,"beta");query_is(menu,1,"alpha");gtk_menu_item_activate(GTK_MENU_ITEM(item(menu,1)));gtk_widget_hide(menu);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)),==,"alpha");phase++;
 } else if(phase==3) {
  g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);g_assert_cmpint(sci_get_current_line(SCINTILLA(named(window,"insight-context-code"))),==,preview_line);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(window,"insight-local-group-types"))));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);GtkWidget *menu=history(button);query_is(menu,2,"Symbol");g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(named(menu,"insight-symbol-history-remember"))),==,"Запомнить текущий фильтр символов");gtk_widget_hide(menu);
  gtk_entry_set_text(GTK_ENTRY(entry),"no_match");phase++;
 } else if(phase==4) {
  g_signal_emit_by_name(entry,"activate");GtkWidget *menu=history(button);query_is(menu,0,"no_match");gtk_widget_hide(menu);
  for(guint i=0;i<20;i++) { gchar *query=g_strdup_printf("name_%02u",i);remember(query);g_free(query); }menu=history(button);query_is(menu,0,"name_19");query_is(menu,15,"name_04");g_assert_null(item(menu,16));gtk_widget_hide(menu);
  const gchar *bad[]={"?[","   ",""};for(guint i=0;i<G_N_ELEMENTS(bad);i++) { gtk_entry_set_text(GTK_ENTRY(entry),bad[i]);menu=history(button);g_assert_false(gtk_widget_get_sensitive(named(menu,"insight-symbol-history-remember")));query_is(menu,0,"name_19");gtk_widget_hide(menu); }
  gchar long_query[258];memset(long_query,'x',257);long_query[257]=0;gtk_entry_set_text(GTK_ENTRY(entry),long_query);menu=history(button);g_assert_false(gtk_widget_get_sensitive(named(menu,"insight-symbol-history-remember")));gtk_widget_hide(menu);gtk_entry_set_text(GTK_ENTRY(entry),"name_19");phase++;
 } else {
  GtkWidget *menu=history(button);gtk_menu_item_activate(GTK_MENU_ITEM(named(menu,"insight-symbol-history-clear")));gtk_widget_hide(menu);menu=history(button);g_assert_null(item(menu,0));g_assert_false(gtk_widget_get_sensitive(named(menu,"insight-symbol-history-clear")));gtk_widget_hide(menu);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)),==,"name_19");menu=history(project_button);query_is(menu,0,"project_only");gtk_widget_hide(menu);g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);
  g_print("Symbol history GUI passed: independent restore, duplicate and malformed filtering, icon-only controls, remember/Enter, MRU cap, literal queries in RU, non-navigating pick, preserved grouping, invalid/empty/long query rejection, independent clear and settings write\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("symbol-history-gui-%u",g_random_int()),*directory=g_build_filename(cwd,name,NULL);g_free(cwd);g_free(name);profile=g_build_filename(directory,"profile",NULL);path=g_build_filename(directory,"sample.c",NULL);gchar *sd=g_build_filename(profile,"insight",NULL);g_assert_cmpint(g_mkdir_with_parents(sd,0700),==,0);settings=g_build_filename(sd,"workbench.conf",NULL);g_free(sd);
 g_assert_true(g_file_set_contents(settings,"[workbench]\nversion=1\nfollow=false\nproject-visible=true\nfile-symbols-grouped=true\nfile-symbol-types=1\nfile-symbols-query-history=alpha;beta;?[;Symbol;alpha;\nproject-symbols-query-history=project_only;\n",-1,NULL));g_assert_true(g_file_set_contents(path,"int alpha(void) { return 1; }\nint beta(void) { return 2; }\nint sample_variable;\n",-1,NULL));gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"symbol-history-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(200,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);
 GKeyFile *key=g_key_file_new();g_assert_true(g_key_file_load_from_file(key,settings,G_KEY_FILE_NONE,NULL));gsize count=0;gchar **queries=g_key_file_get_string_list(key,"workbench","file-symbols-query-history",&count,NULL);g_assert_cmpuint(count,==,0);g_strfreev(queries);queries=g_key_file_get_string_list(key,"workbench","project-symbols-query-history",&count,NULL);g_assert_cmpuint(count,==,1);g_assert_cmpstr(queries[0],==,"project_only");g_strfreev(queries);g_assert_cmpint(g_key_file_get_integer(key,"workbench","file-symbol-types",NULL),==,1);g_key_file_unref(key);g_free(directory);g_free(profile);g_free(path);g_free(settings);return result;
}
