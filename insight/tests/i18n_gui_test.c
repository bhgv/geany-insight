/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *directory,*profile;static GtkWidget *window;static guint phase;
static GtkWidget *named(GtkWidget *root,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_ITEM(root)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static GtkWidget *viewer(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!strcmp(gtk_widget_get_name(p->data),"insight-help-viewer")) found=p->data;g_list_free(windows);return found; }
static gboolean has_text(GtkWidget *widget,const gchar *text) { if(GTK_IS_LABEL(widget) && strstr(gtk_label_get_text(GTK_LABEL(widget)),text)) return TRUE;if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));gboolean found=FALSE;for(GList *p=children;p && !found;p=p->next) found=has_text(p->data,text);g_list_free(children);return found; }return FALSE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(phase==0) { g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(named(window,"insight-language-options"))),==,"Профили языков…");sci_set_text(doc->editor->sci,"Help\nOptions\nSave\nИсходник\n");document_set_text_changed(doc,FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(named(window,"insight-help-command")));g_assert_nonnull(viewer());g_assert_true(has_text(viewer(),"руководство пользователя"));phase++;return G_SOURCE_CONTINUE; }
 if(phase==1) { gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(named(window,"insight-language-options"))),==,"Language profiles…");g_assert_true(has_text(viewer(),"user guide"));gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);g_assert_cmpstr(text,==,"Help\nOptions\nSave\nИсходник\n");g_free(text);phase++;return G_SOURCE_CONTINUE; }
 if(phase==2) { gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(named(window,"insight-language-options"))),==,"Профили языков…");g_assert_true(has_text(viewer(),"руководство пользователя"));g_assert_nonnull(g_object_get_data(G_OBJECT(named(window,"insight-language-options")),"insight-menu-icon"));gchar *settings=g_build_filename(profile,"insight","interface.conf",NULL),*text=NULL;g_assert_true(g_file_get_contents(settings,&text,NULL,NULL));g_assert_nonnull(strstr(text,"language=ru"));g_free(text);g_free(settings);phase++;return G_SOURCE_CONTINUE; }
 if(phase==3) { gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-auto")),TRUE);const gchar *expected=g_getenv("GEANY_TEST_SYSTEM_RU") ? "Профили языков…" : "Language profiles…";g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(named(window,"insight-language-options"))),==,expected);g_print("I18n GUI: persisted Russian startup, live English/Russian round trip, automatic system selection, localized offline help, shared icons and unchanged source passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("I18n GUI timeout at phase %u",phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 if(g_getenv("GEANY_TEST_SYSTEM_RU")) g_setenv("LANGUAGE","ru",TRUE);else g_setenv("LANGUAGE","en",TRUE);
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("i18n-gui-%u",g_random_int());directory=g_build_filename(cwd,name,NULL);profile=g_build_filename(directory,"profile",NULL);g_free(cwd);g_free(name);gchar *folder=g_build_filename(profile,"insight",NULL);g_assert_cmpint(g_mkdir_with_parents(folder,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);config=g_build_filename(folder,"interface.conf",NULL);g_assert_true(g_file_set_contents(config,"[interface]\nlanguage=ru\n",-1,NULL));g_free(config);g_free(folder);gchar *argv[]={"i18n-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(35,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);return result;
}
