/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static ScintillaObject *sci;static GeanyDocument *doc;static gint guard;static guint phase;static const gchar *original="\t  αβ\r\nnext\r\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL; }
static void check(const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static gchar *preview_text(GtkWidget *d) { GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(d,"insight-line-insert-preview")));GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);return gtk_text_buffer_get_text(buffer,&a,&b,FALSE); }
static gboolean apply(gpointer data) {
 GtkWidget *d=data;
 if(guard==0) {
  check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));gchar *text=preview_text(d);g_assert_cmpstr(text,==,"⇥··│¶\n⇥··αβ¶\nnext¶\n");g_free(text);
  if(g_getenv("GEANY_LINE_INSERT_CAPTURE")) { GdkWindow *w=gtk_widget_get_window(d);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_LINE_INSERT_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
 }
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE;
}
static gboolean respond(gpointer data) {
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *d=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-line-insert-dialog")) d=p->data;g_list_free(windows);if(!d) return G_SOURCE_CONTINUE;
 g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(named(d,"insight-line-insert-preview"))));
 if(guard==1) SEND(sci,SCI_INSERTTEXT,0,(sptr_t)"changed ");
 if(guard==2) SEND(sci,SCI_SETSEL,0,1);
 if(guard==3) SEND(sci,SCI_SETREADONLY,1,0);
 if(guard==4) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
 if(guard==5) { SEND(sci,SCI_SETSAVEPOINT,0,0);g_assert_true(document_close(doc)); }
 if(guard==6) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(d,"insight-line-insert-indent")),FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-line-insert-mode")),1); }
 if(guard==7) g_assert_nonnull(document_new_file(NULL,NULL,NULL));
 if(guard==8) gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-line-insert-mode")),2);
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_HELP);g_timeout_add(200,apply,d);return G_SOURCE_REMOVE;
}
static void command(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void preview(gint g) { guard=g;g_timeout_add(50,respond,NULL);command("insight-line-insert-preview-command"); }
static void reset(const gchar *text,gint anchor,gint caret) { SEND(sci,SCI_SETREADONLY,0,0);sci_set_text(sci,text);SEND(sci,SCI_SETEOLMODE,SC_EOL_CRLF,0);doc->editor->auto_indent=TRUE;SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);SEND(sci,SCI_SETSEL,anchor,caret); }
static void position(gint caret) { g_assert_cmpint(SEND(sci,SCI_GETCURRENTPOS,0,0),==,caret);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,caret); }
static void undo(const gchar *after,const gchar *before) { check(after);SEND(sci,SCI_UNDO,0,0);check(before);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);check(after); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!phase++) { doc=document_get_current();sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci)); }
 reset(original,3,7);command("insight-insert-line");position(3);undo("\t  \r\n\t  αβ\r\nnext\r\n",original);
 reset(original,7,3);command("insight-insert-line-below");position(12);undo("\t  αβ\r\n\t  \r\nnext\r\n",original);
 reset(original,5,5);command("insight-insert-new-line");position(5);undo("\t  α\r\n\t  β\r\nnext\r\n",original);
 reset(original,12,5);command("insight-insert-new-line");position(5);undo("\t  α\r\n\t  t\r\n",original);
 reset(original,3,7);preview(0);position(3);undo("\t  \r\n\t  αβ\r\nnext\r\n",original);
 reset(original,3,3);preview(6);position(9);undo("\t  αβ\r\n\r\nnext\r\n",original);g_assert_true(doc->editor->auto_indent);
 reset(original,5,5);preview(8);position(5);undo("\t  α\r\n\t  β\r\nnext\r\n",original);
 reset("  last",6,6);command("insight-insert-line-below");position(10);undo("  last\r\n  ","  last");
 reset("",0,0);command("insight-insert-line");position(0);undo("\r\n","");
 reset(original,5,5);doc->editor->auto_indent=FALSE;command("insight-insert-new-line");position(5);undo("\t  α\r\nβ\r\nnext\r\n",original);
 for(gint g=1;g<=4;g++) { reset(original,3,3);preview(g);check(g==1 ? "changed \t  αβ\r\nnext\r\n" : original);if(g!=1) g_assert_false(SEND(sci,SCI_CANUNDO,0,0)); }
 reset(original,0,0);SEND(sci,SCI_SETREADONLY,1,0);command("insight-insert-line");command("insight-insert-line-below");command("insight-insert-new-line");check(original);SEND(sci,SCI_SETREADONLY,0,0);
 reset(original,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);command("insight-insert-line");check(original);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);
 reset(original,0,0);SEND(sci,SCI_ADDSELECTION,3,5);command("insight-insert-line-below");check(original);SEND(sci,SCI_CLEARSELECTIONS,0,0);
 reset("",0,0);SEND(sci,SCI_ADDTEXT,3,(sptr_t)"a\0b");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSEL,0,0);command("insight-insert-line");g_assert_cmpint(SEND(sci,SCI_GETLENGTH,0,0),==,3);g_assert_cmpint(SEND(sci,SCI_GETCHARAT,2,0),==,'b');g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original,3,3);preview(7);check(original);g_assert_true(document_close(document_get_current()));
 reset(original,3,3);guint id=doc->id;preview(5);g_assert_null(document_find_by_id(id));
 g_print("Line insertion GUI: above/below/stay, active/reverse UTF-8 selection, exact indent/CRLF, EOF, preview/temporary override, caret, one Undo/Redo, read-only, rectangle/multiple and cancelled/stale/switched/closed guards passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Line insertion GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("line-insert-XXXXXX",NULL),*config=g_build_filename(directory,"geany.conf",NULL),*source=g_build_filename(directory,"source.txt",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(source,original,-1,NULL));gchar *args[]={"line-insert-test","--new-instance","--no-session","--config",directory,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(500,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(6,args);
}
