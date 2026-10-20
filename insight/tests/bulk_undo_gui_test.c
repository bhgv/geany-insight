/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static ScintillaObject *sci;static guint interrupt,interrupt_source;static gulong handler;
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL; }
static GtkWidget *dialog(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *result=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-bulk-undo")) result=p->data;g_list_free(windows);return result; }
static void check(const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static gchar *history(guint count) {
 SEND(sci,SCI_SETREADONLY,0,0);sci_set_text(sci,"seed\r\n");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);
 GString *text=g_string_new("seed\r\n");for(guint i=0;i<count;i++) { gchar *line=g_strdup_printf("α%u\r\n",i);SEND(sci,SCI_BEGINUNDOACTION,0,0);SEND(sci,SCI_APPENDTEXT,strlen(line),(sptr_t)line);SEND(sci,SCI_ENDUNDOACTION,0,0);g_string_append(text,line);g_free(line); }
 return g_string_free(text,FALSE);
}
static gboolean interfere(gpointer data) {
 (void)data;interrupt_source=0;GtkWidget *d=dialog();g_assert_nonnull(d);
 if(interrupt==1) {
  if(g_getenv("GEANY_UNDO_CAPTURE")) { GdkWindow *w=gtk_widget_get_window(d);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_UNDO_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);
 }else if(interrupt==2) SEND(sci,SCI_APPENDTEXT,8,(sptr_t)"external");
 else if(interrupt==3) SEND(sci,SCI_SETREADONLY,1,0);
 else if(interrupt==4) gtk_widget_destroy(d);
 else { GeanyDocument *doc=document_get_current();SEND(sci,SCI_SETSAVEPOINT,0,0);g_assert_true(document_close(doc)); }
 return G_SOURCE_REMOVE;
}
static void modified(ScintillaObject *widget,gint code,SCNotification *nt,gpointer data) {
 (void)widget;(void)code;(void)data;
 if(nt->nmhdr.code==SCN_MODIFIED && (nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT)) && !interrupt_source) {
  g_signal_handler_disconnect(sci,handler);handler=0;interrupt_source=g_idle_add(interfere,NULL);
 }
}
static void command(gboolean redo,guint mode) {
 interrupt=mode;if(mode) handler=g_signal_connect(sci,"sci-notify",G_CALLBACK(modified),NULL);
 gtk_menu_item_activate(GTK_MENU_ITEM(named(window,redo ? "insight-redo-all" : "insight-undo-all")));
 if(handler) { g_signal_handler_disconnect(sci,handler);handler=0; }
 g_assert_cmpuint(interrupt_source,==,0);
}
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));
 gchar *all=history(160);command(FALSE,0);check("seed\r\n");g_assert_false(SEND(sci,SCI_GETMODIFY,0,0));g_assert_false(SEND(sci,SCI_CANUNDO,0,0));command(TRUE,0);check(all);g_assert_true(SEND(sci,SCI_GETMODIFY,0,0));g_assert_false(SEND(sci,SCI_CANREDO,0,0));
 command(FALSE,0);SEND(sci,SCI_SETREADONLY,1,0);command(TRUE,0);check("seed\r\n");SEND(sci,SCI_SETREADONLY,0,0);command(TRUE,0);check(all);g_free(all);
 all=history(1000);command(FALSE,1);g_assert_true(SEND(sci,SCI_CANUNDO,0,0));g_assert_true(SEND(sci,SCI_CANREDO,0,0));command(TRUE,0);check(all);g_free(all);
 all=history(1000);command(FALSE,2);gchar *current=sci_get_contents(sci,-1);g_assert_true(g_str_has_suffix(current,"external"));g_free(current);g_assert_true(SEND(sci,SCI_CANUNDO,0,0));g_assert_false(SEND(sci,SCI_CANREDO,0,0));g_free(all);
 all=history(1000);command(FALSE,3);g_assert_cmpint(SEND(sci,SCI_GETUNDOCURRENT,0,0),>,0);SEND(sci,SCI_SETREADONLY,0,0);command(TRUE,0);check(all);g_free(all);
 all=history(1000);command(FALSE,4);g_assert_true(SEND(sci,SCI_CANUNDO,0,0));command(TRUE,0);check(all);g_free(all);
 /* Open undo groups must be left to their owner. */
 SEND(sci,SCI_BEGINUNDOACTION,0,0);gint before=SEND(sci,SCI_GETUNDOCURRENT,0,0);command(FALSE,0);g_assert_cmpint(SEND(sci,SCI_GETUNDOCURRENT,0,0),==,before);SEND(sci,SCI_ENDUNDOACTION,0,0);
 guint id=doc->id;command(FALSE,5);g_assert_null(document_find_by_id(id));
 g_print("Bulk Undo/Redo: retained groups, UTF-8/CRLF, complete traversal, cancellation, redo preservation, stale-history/read-only guards, dialog destruction and open-group rejection passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Bulk Undo GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *profile=g_dir_make_tmp("bulk-undo-XXXXXX",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *args[]={"bulk-undo","--new-instance","--no-session","--config",profile,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(600,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(5,args);
}
