/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static gchar *narrow_path;static GtkWidget *window;static ScintillaObject *sci;static GeanyDocument *doc;static gint guard;static guint phase;static const gchar *original="  one two\r\n three four\r\n\r\nnext block\r\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL; }
static void check(const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static gboolean apply(gpointer data) {
 GtkWidget *d=data;
 if(guard==0) {
  check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(d,"insight-paragraph-preview")));GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);gchar *text=gtk_text_buffer_get_text(buffer,&a,&b,FALSE);g_assert_cmpstr(text,==,"  one two\r\n  three four\r\n");g_free(text);
  if(g_getenv("GEANY_PARAGRAPH_CAPTURE")) { GdkWindow *w=gtk_widget_get_window(d);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_PARAGRAPH_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
 }
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE;
}
static gboolean respond(gpointer data) {
 (void)data;GList *windows=gtk_window_list_toplevels();GtkWidget *d=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-paragraph-dialog")) d=p->data;g_list_free(windows);if(!d) return G_SOURCE_CONTINUE;
 if(guard==8) g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(named(d,"insight-paragraph-margin"))),==,20);
 else gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(d,"insight-paragraph-margin")),12);
 if(guard==1) SEND(sci,SCI_INSERTTEXT,0,(sptr_t)"changed ");
 if(guard==2) SEND(sci,SCI_SETSEL,0,1);
 if(guard==3) SEND(sci,SCI_SETREADONLY,1,0);
 if(guard==4) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
 if(guard==5) { SEND(sci,SCI_SETSAVEPOINT,0,0);g_assert_true(document_close(doc)); }
 if(guard==6) { gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-paragraph-mode")),1);g_assert_false(gtk_widget_get_sensitive(named(d,"insight-paragraph-margin"))); }
 if(guard==7) { GeanyDocument *other=document_new_file(NULL,NULL,NULL);g_assert_nonnull(other); }
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_HELP);g_timeout_add(200,apply,d);return G_SOURCE_REMOVE;
}
static void command(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void preview(gint g) { guard=g;g_timeout_add(50,respond,NULL);command("insight-paragraph-preview-command"); }
static void reset(const gchar *text,gint anchor,gint caret) { SEND(sci,SCI_SETREADONLY,0,0);sci_set_text(sci,text);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);SEND(sci,SCI_SETSEL,anchor,caret); }
static void undo(const gchar *after,const gchar *before) { check(after);SEND(sci,SCI_UNDO,0,0);check(before);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);check(after); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!phase++) { doc=document_get_current();sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci)); }
 reset(original,3,3);preview(0);undo("  one two\r\n  three four\r\n\r\nnext block\r\n",original);
 reset(original,3,3);command("insight-reform-paragraph");undo("  one two three four\r\n\r\nnext block\r\n",original);
 reset(original,3,3);command("insight-join-lines");undo("  one two three four\r\n\r\nnext block\r\n",original);
 reset("α  a\n b  c\nlast",11,0);command("insight-join-lines");check("α  a b  c\nlast");g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),>,SEND(sci,SCI_GETCURRENTPOS,0,0));undo("α  a b  c\nlast","α  a\n b  c\nlast");
 reset(original,0,0);preview(6);undo("  one two three four\r\n\r\nnext block\r\n",original);
 for(gint g=1;g<=4;g++) { reset(original,3,3);preview(g);check(g==1 ? "changed   one two\r\n three four\r\n\r\nnext block\r\n" : original);if(g!=1) g_assert_false(SEND(sci,SCI_CANUNDO,0,0)); }
 reset("unchanged\n",0,0);command("insight-reform-paragraph");command("insight-join-lines");check("unchanged\n");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original,0,0);SEND(sci,SCI_SETREADONLY,1,0);command("insight-reform-paragraph");command("insight-join-lines");check(original);SEND(sci,SCI_SETREADONLY,0,0);
 reset(original,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);command("insight-reform-paragraph");check(original);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);
 GeanyDocument *source_doc=doc;ScintillaObject *source_sci=sci;doc=document_open_file(narrow_path,FALSE,NULL,NULL);g_assert_nonnull(doc);sci=doc->editor->sci;
 const gchar *narrow="  one two three four five six\n seven eight\n",*wrapped="  one two three four\n  five six seven\n  eight\n";
 reset(narrow,3,3);command("insight-reform-paragraph");undo(wrapped,narrow);reset(narrow,3,3);preview(8);undo(wrapped,narrow);SEND(sci,SCI_SETSAVEPOINT,0,0);g_assert_true(document_close(doc));doc=source_doc;sci=source_sci;
 reset(original,3,3);preview(7);check(original);g_assert_true(document_close(document_get_current()));
 reset(original,3,3);guint id=doc->id;preview(5);g_assert_null(document_find_by_id(id));
 g_print("Paragraph GUI: native commands/preview, caret and reverse UTF-8 selection, blank boundaries, exact spaces/CRLF, one Undo/Redo, no-op, read-only, rectangular, cancelled/stale/switched/closed targets passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Paragraph GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("paragraph-XXXXXX",NULL),*config=g_build_filename(directory,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nlong_line_column=72\n",-1,NULL));gchar *folder=g_build_filename(directory,"languages","Narrow",NULL),*language=g_build_filename(folder,"language.conf",NULL),*css=g_build_filename(folder,"style.css",NULL);g_assert_cmpint(g_mkdir_with_parents(folder,0700),==,0);g_assert_true(g_file_set_contents(language,"[language]\nname=Narrow\nparser=C++\nfile-filters=*.narrow\nextensions=narrow\n[editing]\nversion=1\nenabled=true\nmargin=20\nright-margin=false\n",-1,NULL));g_assert_true(g_file_set_contents(css,"",0,NULL));narrow_path=g_build_filename(directory,"test.narrow",NULL);g_assert_true(g_file_set_contents(narrow_path,"",0,NULL));
 gchar *source=g_build_filename(directory,"source.txt",NULL);g_assert_true(g_file_set_contents(source,original,-1,NULL));gchar *args[]={"paragraph-test","--new-instance","--no-session","--config",directory,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(500,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(6,args);
}
