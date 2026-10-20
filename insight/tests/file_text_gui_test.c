/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#ifdef G_OS_UNIX
#include <unistd.h>
#endif
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static const gchar *original="prefix α suffix\r\nline2\r\n";
static GtkWidget *window;static GeanyDocument *source;static ScintillaObject *sci;static gchar *directory,*source_path,*input,*empty,*binary,*output,*copy,*stale;static guint phase,rounds;static const gchar *chosen;static gint mode;static gboolean importing;
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL; }
static GtkWidget *dialog(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *result=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),importing ? "insight-insert-file-dialog" : "insight-export-text-dialog")) result=p->data;g_list_free(windows);return result; }
static GtkFileChooserConfirmation overwrite(GtkFileChooser *chooser,gpointer data) { (void)chooser;(void)data;return GTK_FILE_CHOOSER_CONFIRMATION_ACCEPT_FILENAME; }
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;
 if(mode==4) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
 if(!rounds++) {
  gtk_window_resize(GTK_WINDOW(d),900,550);
  g_signal_connect(d,"confirm-overwrite",G_CALLBACK(overwrite),NULL);
  if(importing) g_assert_true(gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(d),chosen));
  else { g_assert_true(gtk_file_chooser_get_do_overwrite_confirmation(GTK_FILE_CHOOSER(d)));gchar *base=g_path_get_basename(chosen),*parent=g_path_get_dirname(chosen);gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(d),parent);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d),base);g_free(base);g_free(parent); }
  return G_SOURCE_CONTINUE;
 }
 gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));gboolean ready=!g_strcmp0(path,chosen);g_free(path);if(!ready || rounds<4) { g_assert_cmpuint(rounds,<,40);return G_SOURCE_CONTINUE; }
 if(mode==1) SEND(sci,SCI_INSERTTEXT,0,(sptr_t)"stale ");
 if(mode==2) SEND(sci,SCI_SETSEL,0,0);
 if(mode==3) SEND(sci,SCI_SETREADONLY,1,0);
 if(mode==5) { SEND(sci,SCI_SETSAVEPOINT,0,0);g_assert_true(document_close(source)); }
 if(!importing && chosen==output && mode==0 && g_getenv("GEANY_FILE_TEXT_CAPTURE")) { GdkWindow *w=gtk_widget_get_window(d);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_FILE_TEXT_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static void command(const gchar *id,const gchar *path,gint guard) { importing=!strcmp(id,"insight-insert-file");chosen=path;mode=guard;rounds=0;g_timeout_add(100,respond,NULL);GtkWidget *entry=named(window,id);g_assert_nonnull(entry);gtk_menu_item_activate(GTK_MENU_ITEM(entry)); }
static void check(const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void bytes(const gchar *path,const gchar *expected) { gchar *text=NULL;gsize length;g_assert_true(g_file_get_contents(path,&text,&length,NULL));g_assert_cmpuint(length,==,strlen(expected));g_assert_cmpmem(text,length,expected,strlen(expected));g_free(text); }
static void reset(void) { source=document_open_file(source_path,FALSE,NULL,NULL);g_assert_nonnull(source);sci=source->editor->sci;SEND(sci,SCI_SETREADONLY,0,0);sci_set_text(sci,original);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);SEND(sci,SCI_SETSEL,9,7); }
static void undo(const gchar *expected) { check(expected);SEND(sci,SCI_UNDO,0,0);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);check(expected); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!phase) { source=document_open_file(source_path,FALSE,NULL,NULL);sci=source->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));phase++;return G_SOURCE_CONTINUE; }
 reset();command("insight-insert-file",input,0);undo("prefix β\nline suffix\r\nline2\r\n");bytes(input,"\xef\xbb\xbf" "β\nline");
 reset();command("insight-insert-file",empty,0);undo("prefix  suffix\r\nline2\r\n");
 reset();command("insight-insert-file",binary,0);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset();command("insight-insert-file",input,1);check("stale prefix α suffix\r\nline2\r\n");SEND(sci,SCI_UNDO,0,0);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset();command("insight-insert-file",input,2);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset();command("insight-insert-file",input,3);check(original);
 reset();command("insight-insert-file",input,4);check(original);
 reset();SEND(sci,SCI_SETREADONLY,1,0);command("insight-save-selection",output,0);bytes(output,"α");g_assert_true(document_get_current()!=source);g_assert_false(SEND(document_get_current()->editor->sci,SCI_GETMODIFY,0,0));g_assert_true(document_close(document_get_current()));check(original);g_assert_false(SEND(sci,SCI_GETMODIFY,0,0));g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset();SEND(sci,SCI_SETREADONLY,1,0);command("insight-save-copy",copy,0);bytes(copy,original);g_assert_true(document_get_current()!=source);g_assert_true(document_close(document_get_current()));check(original);
 reset();SEND(sci,SCI_INSERTTEXT,0,(sptr_t)"local ");gint undo_position=SEND(sci,SCI_GETUNDOCURRENT,0,0);command("insight-save-copy",stale,0);bytes(stale,"local prefix α suffix\r\nline2\r\n");g_assert_true(SEND(sci,SCI_GETMODIFY,0,0));g_assert_cmpint(SEND(sci,SCI_GETUNDOCURRENT,0,0),==,undo_position);g_assert_cmpstr(source->file_name,==,source_path);g_assert_true(document_close(document_get_current()));g_remove(stale);bytes(source_path,original);
 reset();command("insight-save-selection",stale,1);g_assert_false(g_file_test(stale,G_FILE_TEST_EXISTS));
 reset();command("insight-save-selection",stale,2);g_assert_false(g_file_test(stale,G_FILE_TEST_EXISTS));
 reset();command("insight-save-copy",stale,4);g_assert_false(g_file_test(stale,G_FILE_TEST_EXISTS));
 reset();command("insight-save-copy",source_path,0);bytes(source_path,original);
 GeanyDocument *open=document_open_file(output,FALSE,NULL,NULL);g_assert_nonnull(open);reset();command("insight-save-copy",output,0);bytes(output,"α");g_assert_true(document_close(open));
#ifdef G_OS_UNIX
 gchar *alias=g_build_filename(directory,"source-alias.c",NULL);
 if(link(source_path,alias)==0) { reset();command("insight-save-copy",alias,0);GStatBuf a,b;g_assert_cmpint(g_stat(source_path,&a),==,0);g_assert_cmpint(g_stat(alias,&b),==,0);g_assert_cmpuint(a.st_ino,==,b.st_ino);bytes(source_path,original);g_remove(alias); }
 g_free(alias);
#endif
 g_assert_true(g_file_set_contents(copy,"old",-1,NULL));reset();command("insight-save-copy",copy,0);bytes(copy,original);g_assert_true(document_close(document_get_current()));
 reset();guint id=source->id;command("insight-save-selection",stale,5);g_assert_null(document_find_by_id(id));g_assert_false(g_file_test(stale,G_FILE_TEST_EXISTS));bytes(source_path,original);
 g_print("File text GUI: BOM/UTF-8/EOL, exact/reverse selection, empty input, Undo/Redo, read-only export, cancelled/stale/closed targets, original/open destinations protected and native editor routing passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("File text GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 directory=g_dir_make_tmp("file-text-XXXXXX",NULL);gchar *profile=g_build_filename(directory,"profile",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_mkdir_with_parents(profile,0700);source_path=g_build_filename(directory,"source.c",NULL);input=g_build_filename(directory,"input.txt",NULL);empty=g_build_filename(directory,"empty.txt",NULL);binary=g_build_filename(directory,"binary.txt",NULL);output=g_build_filename(directory,"selection-output.txt",NULL);copy=g_build_filename(directory,"copy.txt",NULL);stale=g_build_filename(directory,"stale.txt",NULL);
 g_assert_true(g_file_set_contents(source_path,original,-1,NULL));g_assert_true(g_file_set_contents(input,"\xef\xbb\xbf" "β\nline",-1,NULL));g_assert_true(g_file_set_contents(empty,"",0,NULL));g_assert_true(g_file_set_contents(binary,"a\0b",3,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 gchar *args[]={"file-text","--new-instance","--no-session","--config",profile,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(500,step,NULL);g_timeout_add_seconds(50,timeout,NULL);return main_lib(5,args);
}
