/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>

#undef SSM
#define SSM(sci,msg,w,l) scintilla_send_message((sci),(msg),(w),(l))
static guint phase,primary_id,untitled_id;
static gchar *profile,*project,*primary,*external;
static GtkWidget *window;
static GtkWidget *named(GtkWidget *root,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_MENU_ITEM(root)) { GtkWidget *submenu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(submenu) { GtkWidget *found=named(submenu,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }
 return NULL;
}
static GtkWidget *button(GtkWidget *root,const gchar *label) {
 if(GTK_IS_BUTTON(root) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(root)),label)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=button(p->data,label);g_list_free(children);return found; }return NULL;
}
static GtkWidget *toplevel(const gchar *name,gboolean by_title) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;
 for(GList *p=windows;p;p=p->next) if(!g_strcmp0(by_title ? gtk_window_get_title(GTK_WINDOW(p->data)) : gtk_widget_get_name(p->data),name)) { found=p->data;break; }
 g_list_free(windows);return found;
}
static void click(GtkWidget *root,const gchar *label) { GtkWidget *w=button(root,label);g_assert_nonnull(w);g_assert_true(gtk_widget_get_sensitive(w));g_signal_emit_by_name(w,"clicked"); }
static GtkWidget *info_window(void) { GtkWidget *w=toplevel("insight-symbol-info-window",FALSE);g_assert_nonnull(w);return w; }
static void show_info(GeanyDocument *doc,const gchar *word) {
 gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1),*at=strstr(text,word);g_assert_nonnull(at);SSM(doc->editor->sci,SCI_GOTOPOS,at-text+1,0);g_free(text);
 GtkWidget *command=named(window,"insight-symbol-info-command");g_assert_nonnull(command);gtk_menu_item_activate(GTK_MENU_ITEM(command));
}
static gboolean cancel_dialog(gpointer data) {
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *dialog=NULL;
 for(GList *p=windows;p;p=p->next) if(GTK_IS_MESSAGE_DIALOG(p->data) || GTK_IS_FILE_CHOOSER(p->data)) { dialog=p->data;break; }
 g_list_free(windows);if(!dialog) return G_SOURCE_CONTINUE;gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
static gboolean select_doc(GtkWidget *view,guint id) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter iter;gboolean valid=gtk_tree_model_get_iter_first(model,&iter);
 while(valid) { guint row=0;gtk_tree_model_get(model,&iter,0,&row,-1);if(row==id) { gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&iter);return TRUE; }valid=gtk_tree_model_iter_next(model,&iter); }return FALSE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Symbol Info GUI timed out at phase %u",phase);return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));if(!window) return G_SOURCE_CONTINUE;
 if(phase==0) {
  if(!doc->file_name || strcmp(doc->file_name,primary)) return G_SOURCE_CONTINUE;
  primary_id=doc->id;show_info(doc,"helper");GtkWidget *info=info_window(),*code=named(info,"insight-symbol-info-code");g_assert_nonnull(code);
  gchar *text=sci_get_contents(SCINTILLA(code),sci_get_length(SCINTILLA(code))+1);g_assert_nonnull(strstr(text,"int helper"));g_free(text);g_assert_true(SSM(SCINTILLA(code),SCI_GETREADONLY,0,0));g_assert_cmpuint(document_get_current()->id,==,primary_id);phase++;
 } else if(phase==1) {
  GtkWidget *info=info_window();gtk_entry_set_text(GTK_ENTRY(named(info,"insight-symbol-info-query")),"external_symbol");click(info,"Refresh");
  GtkComboBox *choice=GTK_COMBO_BOX(named(info,"insight-symbol-info-candidates"));GtkTreeModel *model=gtk_combo_box_get_model(choice);GtkTreeIter iter;gboolean valid=gtk_tree_model_get_iter_first(model,&iter);gint index=0,found=-1;
  while(valid) { gchar *path=NULL;gtk_tree_model_get(model,&iter,1,&path,-1);if(!g_strcmp0(path,external)) found=index;g_free(path);index++;valid=gtk_tree_model_iter_next(model,&iter); }
  if(found<0) return G_SOURCE_CONTINUE;
  gtk_combo_box_set_active(choice,found);g_assert_null(document_find_by_filename(external));click(info,"Leave file open");g_assert_nonnull(document_find_by_filename(external));g_assert_cmpuint(document_get_current()->id,==,primary_id);g_assert_false(gtk_widget_get_sensitive(button(info,"Leave file open")));click(info,"Close");phase++;
 } else if(phase==2) {
  doc=document_new_file(NULL,filetypes_lookup_by_name("C"),"int ephemeral(int n) { return n + 1; }\n");g_assert_nonnull(doc);untitled_id=doc->id;show_info(doc,"ephemeral");
  GtkWidget *info=info_window();GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(info,"insight-symbol-info-details")));GtkTextIter first,last;gtk_text_buffer_get_bounds(buffer,&first,&last);gchar *text=gtk_text_buffer_get_text(buffer,&first,&last,FALSE);g_assert_nonnull(strstr(text,"untitled:"));g_free(text);g_assert_false(gtk_widget_get_sensitive(button(info,"Leave file open")));
  SSM(doc->editor->sci,SCI_APPENDTEXT,11,(sptr_t)"// changed\n");document_set_text_changed(doc,TRUE);gint position=sci_get_current_position(doc->editor->sci);click(info,"Jump");g_assert_true(gtk_widget_get_visible(info));g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,position);g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(info,"insight-symbol-info-status"))),"Source changed"));
  click(info,"Refresh");click(info,"Jump");g_assert_null(toplevel("insight-symbol-info-window",FALSE));phase++;
 } else if(phase==3) {
  GtkWidget *menu=named(window,"insight-window-menu");g_assert_nonnull(menu);g_signal_emit_by_name(menu,"show");GtkWidget *command=named(window,"insight-window-list-command");g_assert_nonnull(command);gtk_menu_item_activate(GTK_MENU_ITEM(command));GtkWidget *list=toplevel("Window List",TRUE);g_assert_nonnull(list);
  GtkWidget *view=named(list,"insight-window-list");g_assert_nonnull(view);g_assert_true(select_doc(view,primary_id));g_assert_cmpuint(document_get_current()->id,==,untitled_id);
  GtkWidget *context=named(window,"insight-context-code");g_assert_nonnull(context);gchar *text=sci_get_contents(SCINTILLA(context),sci_get_length(SCINTILLA(context))+1);g_assert_nonnull(strstr(text,"int helper"));g_free(text);
  g_assert_true(select_doc(view,untitled_id));g_timeout_add(20,cancel_dialog,NULL);click(list,"Close file");g_assert_nonnull(document_find_by_id(untitled_id));g_timeout_add(20,cancel_dialog,NULL);click(list,"Save");g_assert_null(document_find_by_id(untitled_id)->file_name);
  g_assert_true(select_doc(view,primary_id));click(list,"Activate");g_assert_cmpuint(document_get_current()->id,==,primary_id);
  gtk_entry_set_text(GTK_ENTRY(named(list,"insight-window-filter")),"HELPER");g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,1);gtk_widget_destroy(list);document_set_text_changed(document_find_by_id(untitled_id),FALSE);phase++;
 } else {
  g_print("Real editor GUI: Symbol Info source/metadata, background open, snapshot guard, untitled Jump; Window List preview/activate, filtering and cancelled close/save passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY"))) return 77;
 gchar *cwd=g_get_current_dir(),*dirname=g_strdup_printf("symbol-info-gui-%u",g_random_int());profile=g_build_filename(cwd,dirname,"profile",NULL);g_free(cwd);g_free(dirname);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);
 gchar *directory=g_path_get_dirname(profile),*config=g_build_filename(profile,"geany.conf",NULL);primary=g_build_filename(directory,"основной тест.c",NULL);external=g_build_filename(directory,"external.c",NULL);project=g_build_filename(directory,"smoke.geany",NULL);
 g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nuse_native_dialogs=false\n",-1,NULL));g_assert_true(g_file_set_contents(project,"[project]\nname=Symbol Info smoke\nbase_path=.\n",-1,NULL));g_assert_true(g_file_set_contents(primary,"extern int external_symbol(int);\nint helper(int n) { return n + 1; }\nint main(void) { return helper(0) + external_symbol(1); }\n",-1,NULL));g_assert_true(g_file_set_contents(external,"// closed definition\nint external_symbol(int n) { return n + 2; }\n",-1,NULL));g_free(config);g_free(directory);
 gchar *argv[]={"symbol-info-gui","--new-instance","--no-session","--config",profile,project,primary,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(40,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(7,argv);
 g_free(profile);g_free(project);g_free(primary);g_free(external);return result;
}
