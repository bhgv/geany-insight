/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static ScintillaObject *sci;static GeanyDocument *doc;static guint phase;static gint mode,radix,guard;static const gchar *value;
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL; }
static void check(const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static gboolean cancel_invalid(gpointer data) {
 GtkWidget *d=data;g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(d,"insight-renumber-status"))),"64-bit"));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
}
static gboolean capture(gpointer data) {
 GtkWidget *d=data;
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(d,"insight-renumber-preview")));GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);gchar *preview=gtk_text_buffer_get_text(buffer,&a,&b,FALSE);g_assert_cmpstr(preview,==,"α 10, 0xb, 014\r\nend 13");g_free(preview);check("α 4, 0x10, 020\r\nend 9");
 if(g_getenv("GEANY_RENUMBER_CAPTURE")) { GdkWindow *w=gtk_widget_get_window(d);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_RENUMBER_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE;
}
static gboolean respond(gpointer data) {
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *d=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-renumber-dialog")) d=p->data;g_list_free(windows);if(!d) return G_SOURCE_CONTINUE;
 gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-renumber-mode")),mode);gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-renumber-radix")),radix);gtk_entry_set_text(GTK_ENTRY(named(d,"insight-renumber-value")),value);
 if(guard==1) SEND(sci,SCI_INSERTTEXT,0,(sptr_t)"changed ");
 if(guard==2) SEND(sci,SCI_SETSEL,0,1);
 if(guard==3) SEND(sci,SCI_SETREADONLY,1,0);
 if(guard==4) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
 if(guard==5) { SEND(sci,SCI_SETSAVEPOINT,0,0);g_assert_true(document_close(doc)); }
 if(guard==6) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_HELP);g_timeout_add(300,capture,d);return G_SOURCE_REMOVE; }
 if(guard==7) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY);g_timeout_add(300,cancel_invalid,d);return G_SOURCE_REMOVE; }
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE;
}
static void command(gint m,gint r,const gchar *v,gint g) { mode=m;radix=r;value=v;guard=g;g_timeout_add(50,respond,NULL);GtkWidget *item=named(window,"insight-renumber");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void reset(const gchar *text,gint anchor,gint caret) { SEND(sci,SCI_SETREADONLY,0,0);sci_set_text(sci,text);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);SEND(sci,SCI_SETSEL,anchor,caret); }
static void undo(const gchar *after,const gchar *before) { check(after);SEND(sci,SCI_UNDO,0,0);check(before);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);check(after); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!phase++) { doc=document_get_current();sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci)); }
 const gchar *original="α 4, 0x10, 020\r\nend 9";
 reset(original,0,0);command(1,0,"10",6);undo("α 10, 0xb, 014\r\nend 13",original);
 reset(original,15,3);command(2,1,"2",0);g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),>,SEND(sci,SCI_GETCURRENTPOS,0,0));undo("α 6, 18, 18\r\nend 9",original);
 reset(original,0,0);command(0,0,"0",1);check("changed α 4, 0x10, 020\r\nend 9");SEND(sci,SCI_UNDO,0,0);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 for(gint g=2;g<=4;g++) { reset(original,0,0);command(1,1,"1",g);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0)); }
 reset("9223372036854775807 1",0,0);command(2,1,"1",7);check("9223372036854775807 1");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("1",0,0);command(0,0,"0",0);check("1");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original,0,0);guint id=doc->id;command(0,0,"0",5);g_assert_null(document_find_by_id(id));
 g_print("Renumber GUI: whole document, reverse UTF-8 selection, mixed radices, preview, one Undo/Redo, no-op, cancelled/stale/read-only/closed targets passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Renumber GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("renumber-XXXXXX",NULL),*config=g_build_filename(directory,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));gchar *args[]={"renumber-test","--new-instance","--no-session","--config",directory,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(500,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(5,args);
}
