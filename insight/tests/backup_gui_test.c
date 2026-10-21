/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static gchar *profile,*path,*directory;
static const gchar *original="disk original\n",*first="α first snapshot\r\nsecond line\rthird\n",*second="β unsaved snapshot\nmore code\n";
static GtkWidget *window;static GeanyDocument *source;static guint mode,tick;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *dialog(void) { GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data) && !g_strcmp0(gtk_widget_get_name(p->data),"insight-backup-dialog")) found=p->data;g_list_free(list);return found; }
static void action(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void contents(GeanyDocument *doc,const gchar *expected) { gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static GPtrArray *files(void) { GPtrArray *result=g_ptr_array_new_with_free_func(g_free);GDir *scan=g_dir_open(directory,0,NULL);if(scan) { const gchar *name;while((name=g_dir_read_name(scan))) if(g_str_has_suffix(name,".utf8")) g_ptr_array_add(result,g_build_filename(directory,name,NULL));g_dir_close(scan); }return result; }
static void disk(const gchar *file,const gchar *expected) { gchar *text=NULL;gsize length;g_assert_true(g_file_get_contents(file,&text,&length,NULL));g_assert_cmpmem(text,length,expected,strlen(expected));g_free(text); }
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;
 GtkWidget *tree=named(d,"insight-backup-versions"),*preview=named(d,"insight-backup-preview"),*status=named(d,"insight-backup-status");GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
 if(mode==4 && tick) { g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(mode==5) { g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT)));g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),202)));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,2);
 if(mode==0) {
  GtkTextIter begin,end;GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(preview));gtk_text_buffer_get_bounds(buffer,&begin,&end);gchar *text=gtk_text_buffer_get_text(buffer,&begin,&end,FALSE);g_assert_cmpstr(text,==,second);g_free(text);
  if(g_getenv("GEANY_BACKUP_CAPTURE")) { GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_BACKUP_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==1 || mode==2) {
  if(!tick++) { GtkTreePath *p=gtk_tree_path_new_from_indices(1,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),p,NULL,FALSE);gtk_tree_path_free(p);gtk_dialog_response(GTK_DIALOG(d),mode==1 ? 202:GTK_RESPONSE_ACCEPT);return mode==1 ? G_SOURCE_CONTINUE:G_SOURCE_REMOVE; }
  gchar *text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,"α first snapshot\nsecond line\nthird\n");g_free(text);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==3) {
  if(!tick++) { GtkTreeIter iter;GtkTreeModel *selected=NULL;g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),&selected,&iter));gchar *name=NULL;gtk_tree_model_get(selected,&iter,3,&name,-1);gchar *target=g_build_filename(directory,name,NULL);g_assert_true(g_file_set_contents(target,"tampered\n",-1,NULL));g_free(name);g_free(target);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_CONTINUE; }
  g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT)));g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(status)),!=,"");gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 if(mode==4) {
  if(!tick++) { GPtrArray *paths=files();for(guint i=0;i<paths->len;i++) g_remove(g_ptr_array_index(paths,i));g_ptr_array_free(paths,TRUE);gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,0);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static void browse(guint which,gboolean key) { mode=which;tick=0;g_timeout_add(120,respond,NULL);if(key) keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_OPENBACKUP);else action("insight-open-backup");g_assert_null(dialog()); }
static void unchanged(gint undo,gint anchor,gint caret) { g_assert_true(document_get_current()==source);contents(source,second);g_assert_cmpint(SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0),==,undo);g_assert_cmpint(SEND(source->editor->sci,SCI_GETANCHOR,0,0),==,anchor);g_assert_cmpint(SEND(source->editor->sci,SCI_GETCURRENTPOS,0,0),==,caret);g_assert_true(SEND(source->editor->sci,SCI_GETMODIFY,0,0));disk(path,original); }
static gboolean step(gpointer data) {
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));
 gchar *canonical=g_canonicalize_filename(path,NULL),*hash=g_compute_checksum_for_string(G_CHECKSUM_SHA256,canonical,-1);directory=g_build_filename(profile,"insight-backups",hash,NULL);g_free(canonical);g_free(hash);
 browse(5,FALSE);g_assert_false(g_file_test(directory,G_FILE_TEST_EXISTS));
 sci_set_text(source->editor->sci,first);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_SAVEBACKUP);GPtrArray *paths=files();g_assert_cmpuint(paths->len,==,1);disk(g_ptr_array_index(paths,0),first);g_ptr_array_free(paths,TRUE);
 sci_set_text(source->editor->sci,second);SEND(source->editor->sci,SCI_SETSEL,14,2);gint undo=SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0);action("insight-save-backup");paths=files();g_assert_cmpuint(paths->len,==,2);g_ptr_array_free(paths,TRUE);unchanged(undo,14,2);
 browse(0,TRUE);unchanged(undo,14,2);browse(1,FALSE);unchanged(undo,14,2);
 browse(2,TRUE);GeanyDocument *view=document_get_current();g_assert_true(view!=source);g_assert_true(view->readonly);g_assert_true(SEND(view->editor->sci,SCI_GETREADONLY,0,0));g_assert_false(view->changed);g_assert_false(SEND(view->editor->sci,SCI_CANUNDO,0,0));contents(view,first);g_assert_true(document_close(view));unchanged(undo,14,2);
 browse(3,FALSE);unchanged(undo,14,2);browse(4,FALSE);unchanged(undo,14,2);
 /* Read-only sources can still be backed up; the command is a snapshot, not Save. */
 source->readonly=TRUE;SEND(source->editor->sci,SCI_SETREADONLY,1,0);action("insight-save-backup");paths=files();g_assert_cmpuint(paths->len,==,1);disk(g_ptr_array_index(paths,0),second);g_ptr_array_free(paths,TRUE);source->readonly=FALSE;SEND(source->editor->sci,SCI_SETREADONLY,0,0);unchanged(undo,14,2);
 GeanyDocument *untitled=document_new_file(NULL,NULL,"not named\n");action("insight-save-backup");action("insight-open-backup");g_assert_null(dialog());document_set_text_changed(untitled,FALSE);g_assert_true(document_close(untitled));unchanged(undo,14,2);
 /* Same basename in another directory never merges the version families. */
 gchar *other_dir=g_build_filename(profile,"other",NULL);g_mkdir(other_dir,0700);gchar *other_path=g_build_filename(other_dir,"source.c",NULL);g_assert_true(g_file_set_contents(other_path,"other file\n",-1,NULL));GeanyDocument *other=document_open_file(other_path,FALSE,NULL,NULL);action("insight-save-backup");g_assert_true(document_close(other));paths=files();g_assert_cmpuint(paths->len,==,1);g_ptr_array_free(paths,TRUE);unchanged(undo,14,2);
 gchar invalid[]={'x',0,'y',0};SEND(source->editor->sci,SCI_CLEARALL,0,0);SEND(source->editor->sci,SCI_ADDTEXT,3,(sptr_t)invalid);action("insight-save-backup");paths=files();g_assert_cmpuint(paths->len,==,1);g_ptr_array_free(paths,TRUE);disk(path,original);
 g_print("Backup GUI: unsaved exact snapshots, no disk/Undo/selection changes, empty list, UTC list/preview, copy, detached read-only open, stale entry, refresh, read-only source, untitled/NUL rejection and separate path identities passed\n");document_set_text_changed(source,FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Backup GUI timeout mode %u tick %u",mode,tick);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 profile=g_dir_make_tmp("backup-gui-XXXXXX",NULL);gchar *config=g_build_filename(profile,"geany.conf",NULL);path=g_build_filename(profile,"source.c",NULL);g_assert_true(g_file_set_contents(path,original,-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 gchar *args[]={"backups","--new-instance","--no-session","--config",profile,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(45,timeout,NULL);return main_lib(6,args);
}
