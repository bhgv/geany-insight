/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static ScintillaObject *sci;static GeanyDocument *doc;static guint phase;static gint guard;static const gchar *original="α [112*14] tail\r\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL; }
static void check(const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void clipboard(const gchar *expected) { gchar *text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,expected);g_free(text); }
static GtkTextBuffer *expression(GtkWidget *d) { return gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(d,"insight-calculator-expression"))); }
static gboolean copied(gpointer data) {
 GtkWidget *d=data;clipboard(guard==10 ? "42" : "1568");
 if(guard==9 || guard==10) { check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE); }
 else gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY);
 return G_SOURCE_REMOVE;
}
static gboolean finish(gpointer data) {
 GtkWidget *d=data;
 if(guard==8 || guard==11) { const gchar *status=gtk_label_get_text(GTK_LABEL(named(d,"insight-calculator-status")));g_assert_nonnull(strstr(status,guard==8 ? "zero" : "4096"));g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(d,"insight-calculator-result"))),==,"");gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(guard==0 || guard==9 || guard==10) {
  check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(d,"insight-calculator-result"))),==,"1568");
  if(guard==0 && g_getenv("GEANY_CALCULATE_CAPTURE")) { GdkWindow *w=gtk_widget_get_window(d);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_CALCULATE_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
  if(guard==10) { gtk_text_buffer_set_text(expression(d),"Copy Result",-1);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);GtkTextIter a,b;gtk_text_buffer_get_bounds(expression(d),&a,&b);gchar *preserved=gtk_text_buffer_get_text(expression(d),&a,&b,FALSE);g_assert_cmpstr(preserved,==,"Copy Result");g_free(preserved);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);gtk_text_buffer_set_text(expression(d),"6*7",-1);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(d,"insight-calculator-result"))),==,""); }
  gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);g_timeout_add(100,copied,d);return G_SOURCE_REMOVE;
 }
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE;
}
static gboolean respond(gpointer data) {
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *d=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-calculator-dialog")) d=p->data;g_list_free(windows);if(!d) return G_SOURCE_CONTINUE;
 if(guard==1) SEND(sci,SCI_INSERTTEXT,0,(sptr_t)"changed ");
 if(guard==2) SEND(sci,SCI_SETSEL,0,1);
 if(guard==3) SEND(sci,SCI_SETREADONLY,1,0);
 if(guard==4) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
 if(guard==5) { SEND(sci,SCI_SETSAVEPOINT,0,0);g_assert_true(document_close(doc)); }
 if(guard==6) gtk_text_buffer_set_text(expression(d),"sqrt(9) + 2^3",-1);
 if(guard==7) g_assert_nonnull(document_new_file(NULL,NULL,NULL));
 if(guard==8) gtk_text_buffer_set_text(expression(d),"1/0",-1);
 if(guard==9) g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY)));
 if(guard==11) { gchar *large=g_strnfill(4097,'1');gtk_text_buffer_set_text(expression(d),large,-1);g_free(large); }
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_HELP);g_timeout_add(200,finish,d);return G_SOURCE_REMOVE;
}
static void command(const gchar *id) { GtkWidget *item=named(window,id);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void dialog(gint g) { guard=g;g_timeout_add(50,respond,NULL);command("insight-calculator"); }
static void reset(const gchar *text,gint anchor,gint caret) { SEND(sci,SCI_SETREADONLY,0,0);SEND(sci,SCI_CLEARSELECTIONS,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);sci_set_text(sci,text);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);SEND(sci,SCI_SETSEL,anchor,caret); }
static void undo(const gchar *after,const gchar *before) { check(after);SEND(sci,SCI_UNDO,0,0);check(before);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);check(after); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!phase++) { doc=document_get_current();sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci)); }
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"KEEP",-1);
 reset(original,10,4);command("insight-calculate-selection");undo("α [1568] tail\r\n",original);clipboard("KEEP");
 reset("1/0",0,3);command("insight-calculate-selection");check("1/0");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("1568",0,4);command("insight-calculate-selection");check("1568");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original,0,0);command("insight-calculate-selection");check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original,10,4);dialog(0);undo("α [1568] tail\r\n",original);
 reset(original,4,4);dialog(6);undo("α [11112*14] tail\r\n",original);
 for(gint g=1;g<=4;g++) { reset(original,10,4);dialog(g);check(g==1 ? "changed α [112*14] tail\r\n" : original);if(g!=1) g_assert_false(SEND(sci,SCI_CANUNDO,0,0)); }
 reset(original,10,4);dialog(8);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original,10,4);dialog(11);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original,10,4);SEND(sci,SCI_SETREADONLY,1,0);command("insight-calculate-selection");check(original);dialog(9);check(original);
 reset(original,10,4);dialog(10);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original,10,4);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);command("insight-calculate-selection");check(original);
 reset(original,10,4);SEND(sci,SCI_ADDSELECTION,0,0);command("insight-calculate-selection");check(original);
 reset("",0,0);SEND(sci,SCI_ADDTEXT,3,(sptr_t)"1\0x");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSEL,0,3);command("insight-calculate-selection");g_assert_cmpint(SEND(sci,SCI_GETLENGTH,0,0),==,3);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("\xff",0,1);command("insight-calculate-selection");g_assert_cmpint(SEND(sci,SCI_GETLENGTH,0,0),==,1);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));dialog(4);
 reset(original,10,4);dialog(7);check(original);g_assert_true(document_close(document_get_current()));
 reset(original,10,4);guint id=doc->id;dialog(5);g_assert_null(document_find_by_id(id));
 g_print("Calculate GUI: exact/reverse UTF-8 selection, no-op/error, one Undo/Redo, preview/copy/recompute, caret insertion, read-only and cancelled/stale/switched/closed/unsupported/NUL guards passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Calculate GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("calculate-XXXXXX",NULL),*config=g_build_filename(directory,"geany.conf",NULL),*source=g_build_filename(directory,"source.txt",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(source,original,-1,NULL));gchar *args[]={"calculate-test","--new-instance","--no-session","--config",directory,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(500,step,NULL);g_timeout_add_seconds(35,timeout,NULL);return main_lib(6,args);
}
