/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "main.h"
#include "document.h"
#include "editor.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *root,*profile,*left,*right,*source;static guint phase;static GtkWidget *host,*directory,*merge;
static GtkWidget *named(GtkWidget *widget,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(widget),name)) return widget;
 if(GTK_IS_MENU_ITEM(widget)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(widget));GtkWidget *found=sub ? named(sub,name) : NULL;if(found) return found; }
 if(GTK_IS_CONTAINER(widget)) { GList *children=gtk_container_get_children(GTK_CONTAINER(widget));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *window_named(const gchar *name) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;
 for(GList *p=windows;p && !found;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),name)) found=p->data;
 g_list_free(windows);return found;
}
static gboolean respond_file_dialog(gpointer data) {GtkWidget *dialog=window_named("insight-directory-operation-dialog");g_assert_nonnull(dialog);gtk_dialog_response(GTK_DIALOG(dialog),GPOINTER_TO_INT(data));return G_SOURCE_REMOVE;
}
static gboolean apply_compare_options(gpointer data) {
 (void)data;GtkWidget *dialog=window_named("insight-diff-options");g_assert_nonnull(dialog);gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(dialog,"insight-diff-options-zoom")),130);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(dialog,"insight-diff-options-6")),TRUE);gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE;
}
static gboolean step(gpointer data) {
 (void)data;
 if(!phase) {
  GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
  host=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
  GtkWidget *item=named(host,"insight-directory-menu");if(!item) return G_SOURCE_CONTINUE;
  gtk_menu_item_activate(GTK_MENU_ITEM(item));directory=window_named("insight-directory-window");g_assert_nonnull(directory);
  gtk_entry_set_text(GTK_ENTRY(named(directory,"insight-directory-left")),left);gtk_entry_set_text(GTK_ENTRY(named(directory,"insight-directory-right")),right);
  GtkWidget *known=named(directory,"insight-directory-known");g_assert_true(gtk_widget_get_sensitive(known));gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(known),TRUE);
  gtk_button_clicked(GTK_BUTTON(named(directory,"insight-directory-scan")));phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==1) {
  if(!gtk_widget_get_sensitive(named(directory,"insight-directory-scan"))) return G_SOURCE_CONTINUE;
  GtkWidget *view=named(directory,"insight-directory-files");GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,1);
  GtkTreePath *path=gtk_tree_path_new_first();gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE);gtk_tree_path_free(path);
  /* The current editor buffer is the left file: copying from it, replacing
   * it, and trashing it must all be rejected before any confirmation/task. */
  for(guint action=0;action<3;action++) { gchar *name=g_strdup_printf("insight-directory-operation-%u",action);GtkWidget *button=named(directory,name);g_free(name);g_assert_true(gtk_widget_get_sensitive(button));gtk_button_clicked(GTK_BUTTON(button));g_assert_null(window_named("insight-directory-operation-dialog"));g_assert_true(gtk_widget_get_sensitive(named(directory,"insight-directory-scan"))); }
  g_idle_add(respond_file_dialog,GINT_TO_POINTER(GTK_RESPONSE_CANCEL));gtk_button_clicked(GTK_BUTTON(named(directory,"insight-directory-operation-3")));g_assert_null(window_named("insight-directory-operation-dialog"));
  GtkWidget *open=named(directory,"insight-directory-open");g_assert_true(gtk_widget_get_sensitive(open));gtk_button_clicked(GTK_BUTTON(open));merge=window_named("insight-diff-window");g_assert_nonnull(merge);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {
  if(!gtk_widget_get_sensitive(named(merge,"insight-diff-compare"))) return G_SOURCE_CONTINUE;
  GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(merge,"insight-diff-result")));GtkTextIter a,z;gtk_text_buffer_get_bounds(buffer,&a,&z);gchar *text=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);g_assert_cmpstr(text,==,"port = 3000\n");g_free(text);
  GdkEventKey shortcut={0};shortcut.type=GDK_KEY_PRESS;shortcut.state=GDK_CONTROL_MASK;shortcut.keyval=GDK_KEY_q;gboolean handled=FALSE;g_idle_add(apply_compare_options,NULL);g_signal_emit_by_name(merge,"key-press-event",&shortcut,&handled);g_assert_true(handled);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(named(merge,"insight-diff-zoom"))),==,130);g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(merge,"insight-diff-overview"))));
  GtkWidget *right_view=named(merge,"insight-diff-right");GtkTextBuffer *right_buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(right_view));GtkTextIter line;gtk_widget_grab_focus(right_view);shortcut.keyval=GDK_KEY_g;handled=FALSE;g_signal_emit_by_name(right_view,"key-press-event",&shortcut,&handled);g_assert_true(handled);GtkWidget *line_number=named(merge,"insight-diff-line");g_assert_nonnull(line_number);gtk_spin_button_set_value(GTK_SPIN_BUTTON(line_number),2);g_signal_emit_by_name(line_number,"activate");gtk_text_buffer_get_iter_at_mark(right_buffer,&line,gtk_text_buffer_get_insert(right_buffer));g_assert_cmpint(gtk_text_iter_get_line(&line),==,1);shortcut.keyval=GDK_KEY_e;handled=FALSE;g_signal_emit_by_name(right_view,"key-press-event",&shortcut,&handled);g_assert_true(handled);GeanyDocument *opened=document_get_current();g_assert_nonnull(opened);gchar *expected=g_build_filename(right,"config.ini",NULL);g_assert_cmpstr(opened->real_path,==,expected);g_free(expected);g_assert_cmpint(sci_get_current_line(opened->editor->sci),==,1);g_assert_false(opened->changed);gtk_widget_destroy(merge);
  gchar *copy=g_build_filename(left,"transfer.c",NULL);g_assert_true(g_file_set_contents(copy,"int transfer;\n",-1,NULL));g_free(copy);gtk_button_clicked(GTK_BUTTON(named(directory,"insight-directory-scan")));phase=3;return G_SOURCE_CONTINUE;
 }
 if(phase==3) {
  if(!gtk_widget_get_sensitive(named(directory,"insight-directory-scan"))) return G_SOURCE_CONTINUE;
  GtkWidget *view=named(directory,"insight-directory-files");GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter iter;gboolean found=FALSE;gint name_column=gtk_tree_view_column_get_sort_column_id(gtk_tree_view_get_column(GTK_TREE_VIEW(view),0));
  for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *name=NULL;gtk_tree_model_get(model,&iter,name_column,&name,-1);found=!strcmp(name,"transfer.c");g_free(name);if(found) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE);gtk_tree_path_free(path);break; } }
  g_assert_true(found);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(directory,"insight-directory-confirm")),FALSE);gtk_button_clicked(GTK_BUTTON(named(directory,"insight-directory-operation-0")));phase=4;return G_SOURCE_CONTINUE;
 }
 if(!gtk_widget_get_sensitive(named(directory,"insight-directory-scan"))) return G_SOURCE_CONTINUE;
 gchar *copy=g_build_filename(right,"transfer.c",NULL),*bytes=NULL;g_assert_true(g_file_get_contents(copy,&bytes,NULL,NULL));g_assert_cmpstr(bytes,==,"int transfer;\n");g_free(bytes);g_free(copy);g_assert_false(gtk_widget_get_sensitive(named(directory,"insight-directory-operation-0")));gtk_widget_destroy(directory);
 gtk_menu_item_activate(GTK_MENU_ITEM(named(host,"insight-directory-menu")));directory=window_named("insight-directory-window");g_assert_nonnull(directory);g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(directory,"insight-directory-confirm"))));g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(directory,"insight-directory-known"))));g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(directory,"insight-directory-left"))),==,left);g_assert_true(gtk_widget_get_sensitive(named(directory,"insight-directory-scan")));g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(named(directory,"insight-directory-files"))),NULL),==,0);
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(host,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Directory Compare native GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 root=g_dir_make_tmp("insight-directory-native-XXXXXX",NULL);profile=g_build_filename(root,"profile",NULL);left=g_build_filename(root,"left",NULL);right=g_build_filename(root,"right",NULL);
 g_assert_cmpint(g_mkdir(profile,0700),==,0);g_assert_cmpint(g_mkdir(left,0700),==,0);g_assert_cmpint(g_mkdir(right,0700),==,0);
 source=g_build_filename(left,"config.ini",NULL);gchar *other=g_build_filename(right,"config.ini",NULL),*config=g_build_filename(profile,"geany.conf",NULL);
 g_assert_true(g_file_set_contents(source,"port = 3000\n",-1,NULL));g_assert_true(g_file_set_contents(other,"port = 8080\n",-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 gchar *unknown=g_build_filename(left,"payload.insight-unknown-binary-77",NULL);g_assert_true(g_file_set_contents(unknown,"a\0b",3,NULL));g_free(unknown);
 gchar *args[]={"directory-native","--new-instance","--no-session","--config",profile,source,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 gint result=main_lib(6,args);
 GKeyFile *saved=g_key_file_new();gchar *prefs=g_build_filename(profile,"insight","workbench.conf",NULL);g_assert_true(g_key_file_load_from_file(saved,prefs,G_KEY_FILE_NONE,NULL));g_assert_cmpint(g_key_file_get_integer(saved,"directory-compare","version",NULL),==,1);g_assert_true(g_key_file_get_boolean(saved,"directory-compare","known",NULL));g_assert_false(g_key_file_get_boolean(saved,"directory-compare","confirm",NULL));g_assert_cmpint(g_key_file_get_integer(saved,"file-compare","zoom",NULL),==,130);g_assert_true(g_key_file_get_boolean(saved,"file-compare","overview",NULL));g_free(prefs);g_key_file_unref(saved);g_free(other);g_free(config);g_free(source);g_free(left);g_free(right);g_free(profile);g_free(root);return result;
}
