/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify native overview, membership, source previews and asynchronous project updates.
 * RU: Проверяет настоящий обзор, состав проекта, предпросмотр и асинхронные обновления. */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "keybindings.h"
#include "filetypes.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>

#undef SSM
#define SSM(sci,msg,w,l) scintilla_send_message((sci),(msg),(w),(l))
static guint phase,original_id,step_source,timeout_source; static gint initial_size; static gchar *test_directory,*test_project,*bad_project,*second_project;
static GtkWidget *window,*overview_menu;
static gboolean definition_cancel_checked;
static gboolean selection_stress; static gint64 heartbeat_previous,heartbeat_max_gap;
static guint preview_heartbeats; static gint large_last; static guint heartbeat_source;
static GtkTreeRowReference *retained_file_row,*retained_relation_row,*retained_search_row;
static GtkWidget *find_named(GtkWidget *root,const char *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root)); GtkWidget *found=NULL; for(GList *p=children;p && !found;p=p->next) found=find_named(p->data,name); g_list_free(children); if(found) return found; }
 return NULL;
}
static GtkWidget *find_button(GtkWidget *root,const char *label) {
 if(GTK_IS_BUTTON(root) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(root)),label)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root)); GtkWidget *found=NULL; for(GList *p=children;p && !found;p=p->next) found=find_button(p->data,label); g_list_free(children); if(found) return found; } return NULL;
}
static gboolean edit_membership(gpointer data) {
 gboolean cancel=GPOINTER_TO_INT(data); GList *windows=gtk_window_list_toplevels(); GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(GTK_IS_DIALOG(p->data) && !g_strcmp0(gtk_window_get_title(GTK_WINDOW(p->data)),"Add and Remove Project Files")) dialog=p->data;
 g_list_free(windows); g_assert_nonnull(dialog);GtkWidget *status=find_named(dialog,"insight-membership-status");if(status && !g_strcmp0(gtk_label_get_text(GTK_LABEL(status)),"Reading project selection…")) return G_SOURCE_CONTINUE;
 if(cancel) { GtkWidget *button=find_button(dialog,"Remove All"); g_assert_nonnull(button); gtk_button_clicked(GTK_BUTTON(button)); gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL); }
 else { GtkWidget *view=find_named(dialog,"insight-project-membership-files"); g_assert_nonnull(view); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,1); gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(view))); GtkWidget *button=find_button(dialog,"Remove File"); g_assert_nonnull(button); gtk_button_clicked(GTK_BUTTON(button)); gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_APPLY); }
 return G_SOURCE_REMOVE;
}
static gboolean pick_history(gpointer data) {
 GtkWidget *button=data; GList *menus=gtk_menu_get_for_attach_widget(button); g_assert_nonnull(menus); GtkWidget *item=find_named(menus->data,"insight-search-history-0"); g_assert_nonnull(item); gtk_menu_shell_activate_item(GTK_MENU_SHELL(menus->data),item,TRUE); return G_SOURCE_REMOVE;
}
static gboolean select_project(gpointer data) {
 const gchar *target=GPOINTER_TO_INT(data)==2 ? second_project : (data ? bad_project : test_project); GList *windows=gtk_window_list_toplevels(); GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(GTK_IS_FILE_CHOOSER(p->data) && !g_strcmp0(gtk_window_get_title(GTK_WINDOW(p->data)),"Open Project")) dialog=p->data;
 g_list_free(windows); g_assert_nonnull(dialog);
 gchar *filename=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
 if(g_strcmp0(filename,target)) { g_assert_true(gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog),target)); g_free(filename); return G_SOURCE_CONTINUE; }
 g_free(filename); gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); return G_SOURCE_REMOVE;
}
static gboolean dismiss_invalid(gpointer data) {
 static gboolean rejected; (void)data; GList *windows=gtk_window_list_toplevels(); GtkWidget *message=NULL,*chooser=NULL;
 for(GList *p=windows;p;p=p->next) { if(GTK_IS_MESSAGE_DIALOG(p->data)) message=p->data; else if(GTK_IS_FILE_CHOOSER(p->data)) chooser=p->data; }
 g_list_free(windows);
 if(message) { rejected=TRUE; gtk_dialog_response(GTK_DIALOG(message),GTK_RESPONSE_OK); return G_SOURCE_CONTINUE; }
 if(rejected && chooser) { rejected=FALSE; gtk_dialog_response(GTK_DIALOG(chooser),GTK_RESPONSE_CANCEL); return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
static void project_empty(void) { GtkWidget *view=find_named(window,"insight-project-files"),*symbols=find_named(window,"insight-project-symbols"); g_assert_nonnull(view); g_assert_nonnull(symbols); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,0); g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(symbols)),NULL),==,0); }
static ScintillaObject *map_for(GeanyDocument *doc) {
 GtkWidget *pane=gtk_widget_get_parent(GTK_WIDGET(doc->editor->sci)); g_assert_true(GTK_IS_PANED(pane)); GtkWidget *host=gtk_paned_get_child2(GTK_PANED(pane)); g_assert_true(GTK_IS_OVERLAY(host));
 return SCINTILLA(gtk_bin_get_child(GTK_BIN(host)));
}
static void same_text(GeanyDocument *doc) {
 ScintillaObject *map=map_for(doc); gchar *source=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1),*copy=sci_get_contents(map,sci_get_length(map)+1);
 g_assert_cmpuint(strlen(copy),==,strlen(source)); g_assert_true(!strcmp(copy,source)); g_free(source); g_free(copy); g_assert_false(SSM(doc->editor->sci,SCI_GETREADONLY,0,0)); g_assert_true(SSM(map,SCI_GETREADONLY,0,0));
}
static void screenshot(void) {
 if(!g_getenv("GEANY_GUI_SCREENSHOT")) return;
 GdkWindow *root=gdk_get_default_root_window(); GdkPixbuf *picture=gdk_pixbuf_get_from_window(root,0,0,MIN(1280,gdk_window_get_width(root)),MIN(900,gdk_window_get_height(root))); if(!picture) return;
 gint w=gdk_pixbuf_get_width(picture),h=gdk_pixbuf_get_height(picture),stride=gdk_pixbuf_get_rowstride(picture),channels=gdk_pixbuf_get_n_channels(picture); guchar *pixels=gdk_pixbuf_get_pixels(picture); GString *ppm=g_string_new(NULL); g_string_append_printf(ppm,"P6\n%d %d\n255\n",w,h);
 for(gint y=0;y<h;y++) for(gint x=0;x<w;x++) g_string_append_len(ppm,(const gchar *)pixels+y*stride+x*channels,3);
 g_file_set_contents("overview-gui-smoke.ppm",ppm->str,ppm->len,NULL); g_string_free(ppm,TRUE); g_object_unref(picture);
}
static gboolean preview_heartbeat(gpointer data) { (void)data; gint64 now=g_get_monotonic_time(); if(heartbeat_previous) heartbeat_max_gap=MAX(heartbeat_max_gap,now-heartbeat_previous); heartbeat_previous=now; preview_heartbeats++; return G_SOURCE_CONTINUE; }
static gboolean move_back_before_definition(gpointer data) {
 (void)data; GeanyDocument *doc=document_get_current(); gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1); gchar *name=strstr(text,"return worker_first"); g_assert_nonnull(name); sci_set_current_position(doc->editor->sci,(gint)(name-text)+9,FALSE); g_free(text); return G_SOURCE_REMOVE;
}
static gboolean check_cancelled_definition(gpointer data) {
 (void)data; ScintillaObject *preview=SCINTILLA(find_named(window,"insight-context-code")); g_assert_cmpint(sci_get_current_line(preview),==,0); definition_cancel_checked=TRUE; return G_SOURCE_REMOVE;
}
static gboolean timeout_fail(gpointer data) { (void)data; g_error("Overview GUI smoke timed out in phase %u",phase); return G_SOURCE_REMOVE; }
/* EN: Advance the overview from unused data; verify async membership/indexing and return whether to continue; assertions fail the fixture.
 * RU: Продвигает обзор из неиспользуемого data; проверяет асинхронный состав/индекс и возвращает признак продолжения; ошибки завершают проверку. */
static gboolean step(gpointer data)
{
#include "overview-gui/steps-1.inc"
#include "overview-gui/steps-2.inc"
}

static gboolean finish_shutdown_drain(gpointer loop) { g_main_loop_quit(loop); return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY"))) { g_print("Set GEANY_RUN_GUI_TESTS=1 and provide a display to run the real editor GUI smoke\n"); return 77; }
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("overview-gui-profile-%u",g_random_int()),*profile=g_build_filename(cwd,name,NULL); g_free(cwd); g_free(name); g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);
 gchar *config=g_build_filename(profile,"geany.conf",NULL); g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nuse_native_dialogs=false\n",-1,NULL)); g_free(config);
 test_directory=g_build_filename(profile,"исходники с пробелом",NULL); g_assert_cmpint(g_mkdir_with_parents(test_directory,0700),==,0); test_project=g_build_filename(profile,"проект тест.geany",NULL); g_assert_true(g_file_set_contents(test_project,"[project]\nname=GUI smoke\nbase_path=исходники с пробелом\n",-1,NULL));
 second_project=g_build_filename(profile,"второй проект.geany",NULL); g_assert_true(g_file_set_contents(second_project,"[project]\nname=GUI second\nbase_path=исходники с пробелом\n",-1,NULL));
 bad_project=g_build_filename(profile,"invalid.geany",NULL); g_assert_true(g_file_set_contents(bad_project,"[other]\nname=Not a project\n",-1,NULL));
 gchar *argv[]={"overview-gui-smoke","--new-instance","--no-session","--config",profile,test_project,NULL}; step_source=g_timeout_add(700,step,NULL); timeout_source=g_timeout_add_seconds(45,timeout_fail,NULL);
 g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 gint result=main_lib(6,argv);
 if(g_main_context_find_source_by_id(NULL,step_source)) g_source_remove(step_source);
 if(g_main_context_find_source_by_id(NULL,timeout_source)) g_source_remove(timeout_source);
 /* Deliver late worker completions after workbench/GTK teardown. */
 GMainLoop *drain=g_main_loop_new(NULL,FALSE); g_timeout_add(300,finish_shutdown_drain,drain); g_main_loop_run(drain); g_main_loop_unref(drain);
 g_free(second_project); g_free(bad_project); g_free(test_project); g_free(test_directory); g_free(profile); return result;
}
