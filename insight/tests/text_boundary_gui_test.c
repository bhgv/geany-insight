/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window,*sub,*menu,*toggle;static ScintillaObject *sci;static GeanyDocument *doc;static gboolean started,captured;static const gchar *original="head\r\n\t αβ  \r\nend. tail";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL; }
static void action(const gchar *id) { gchar *name=g_strconcat("insight-nav-",id,NULL);GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));g_free(name); }
static void mode(gboolean active) { g_signal_emit_by_name(menu,"show");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(toggle),active);g_assert_cmpint(SEND(sci,SCI_GETMOVEEXTENDSSELECTION,0,0),==,active); }
static void reset(const gchar *text,gint anchor,gint caret) { SEND(sci,SCI_SETREADONLY,0,0);SEND(sci,SCI_CLEARSELECTIONS,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);sci_set_text(sci,text);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);SEND(sci,SCI_SETSEL,anchor,caret);mode(FALSE); }
static void selection(gint anchor,gint caret) { g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,anchor);g_assert_cmpint(SEND(sci,SCI_GETCURRENTPOS,0,0),==,caret); }
static void unchanged(const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));g_assert_false(SEND(sci,SCI_GETMODIFY,0,0)); }
static void checks(void) {
 reset(original,10,10);action("smart-line-start");selection(8,8);action("smart-line-start");selection(6,6);action("smart-line-start");selection(0,0);action("smart-line-start");selection(0,0);unchanged(original);
 reset(original,10,10);action("smart-line-end");selection(12,12);action("smart-line-end");selection(14,14);action("smart-line-end");selection(strlen(original),strlen(original));unchanged(original);
 reset(original,10,10);mode(TRUE);action("smart-line-start");selection(10,8);action("smart-line-start");selection(10,6);action("smart-line-start");selection(10,0);g_assert_true(SEND(sci,SCI_GETMOVEEXTENDSSELECTION,0,0));unchanged(original);
 reset(original,10,10);action("select-smart-line-start");selection(10,8);action("select-smart-line-end");selection(10,12);g_assert_false(SEND(sci,SCI_GETMOVEEXTENDSSELECTION,0,0));unchanged(original);
 reset(original,6,6);gint period=strchr(original,'.')-original+1;action("select-sentence");selection(6,period);action("select-sentence");selection(6,strlen(original));unchanged(original);
 reset(original,strlen(original),6);mode(TRUE);action("select-sentence");selection(strlen(original),period);g_assert_true(SEND(sci,SCI_GETMOVEEXTENDSSELECTION,0,0));unchanged(original);
 reset("a\n \t\nlast",3,3);action("smart-line-start");selection(2,2);action("smart-line-end");selection(4,4);action("smart-line-end");selection(9,9);unchanged("a\n \t\nlast");
 const gchar *fragment="CreateWindow HTMLOpen open_file Vip32Test ПриветМир";gint positions[]={6,12,13,17,21,22,26,31,32,35,37,41,42};
 reset(fragment,0,0);for(guint i=0;i<G_N_ELEMENTS(positions);i++) { action("fragment-right");selection(positions[i],positions[i]); }action("fragment-right");selection(strlen(fragment),strlen(fragment));unchanged(fragment);
 reset(fragment,31,31);action("fragment-left");selection(27,27);action("fragment-left");selection(22,22);unchanged(fragment);
 reset(fragment,0,0);action("select-fragment-right");selection(0,6);action("select-fragment-right");selection(0,12);action("select-fragment-left");selection(0,6);unchanged(fragment);
 reset(fragment,12,6);action("select-fragment-left");selection(12,0);unchanged(fragment);
 reset(fragment,0,0);mode(TRUE);action("fragment-right");selection(0,6);action("fragment-right");selection(0,12);action("fragment-left");selection(0,6);g_assert_true(SEND(sci,SCI_GETMOVEEXTENDSSELECTION,0,0));unchanged(fragment);
 reset(original,10,10);SEND(sci,SCI_SETREADONLY,1,0);action("smart-line-start");selection(8,8);action("select-sentence");selection(8,period);unchanged(original);
 reset(fragment,0,0);SEND(sci,SCI_SETREADONLY,1,0);action("select-fragment-right");selection(0,6);unchanged(fragment);
 reset(original,10,10);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);action("smart-line-start");action("fragment-left");g_assert_cmpint(SEND(sci,SCI_GETCURRENTPOS,0,0),==,10);g_signal_emit_by_name(sub,"show");g_assert_false(gtk_widget_get_sensitive(named(sub,"insight-nav-smart-line-start")));g_assert_true(gtk_widget_get_sensitive(named(sub,"insight-text-boundaries-help")));
 reset(original,10,10);SEND(sci,SCI_ADDSELECTION,12,12);gint before=SEND(sci,SCI_GETCURRENTPOS,0,0);action("smart-line-end");action("fragment-right");g_assert_cmpint(SEND(sci,SCI_GETCURRENTPOS,0,0),==,before);unchanged(original);
 reset(original,10,10);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,2);action("select-sentence");g_assert_cmpint(SEND(sci,SCI_GETCURRENTPOS,0,0),==,10);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,0);
 reset("",0,0);SEND(sci,SCI_ADDTEXT,3,(sptr_t)"a\0b");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSEL,0,0);action("smart-line-end");selection(0,0);g_assert_cmpint(SEND(sci,SCI_GETLENGTH,0,0),==,3);
 reset("",0,0);action("smart-line-start");action("smart-line-end");action("select-sentence");action("fragment-left");action("fragment-right");selection(0,0);unchanged("");
 reset(original,0,0);g_print("Text boundaries GUI: smart cycles, sentence periods, native camel/acronym/snake/digit/UTF-8 fragments, explicit/reverse/Extend selections, read-only and unsupported-mode guards passed without text/savepoint/Undo changes\n");
}
static gboolean capture(gpointer data) {
 (void)data;GdkWindow *w=gtk_widget_get_window(sub);g_assert_nonnull(w);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gtk_widget_get_allocated_width(sub),gtk_widget_get_allocated_height(sub));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_TEXT_BOUNDARY_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image);gtk_menu_popdown(GTK_MENU(sub));captured=TRUE;return G_SOURCE_REMOVE;
}
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!started) { doc=document_get_current();sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(named(window,"insight-selection-navigation")));toggle=named(menu,"insight-extend-mode");sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(named(window,"insight-text-boundaries")));started=TRUE;
  if(g_getenv("GEANY_TEXT_BOUNDARY_CAPTURE")) { GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(sci)));event->button.button=3;event->button.time=GDK_CURRENT_TIME;gdk_event_set_device(event,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));gtk_menu_popup_at_widget(GTK_MENU(sub),GTK_WIDGET(sci),GDK_GRAVITY_CENTER,GDK_GRAVITY_CENTER,event);gdk_event_free(event);g_timeout_add(200,capture,NULL);return G_SOURCE_CONTINUE; }captured=TRUE;
 }
 if(!captured) return G_SOURCE_CONTINUE;
 checks();gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Text boundary GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("text-boundary-XXXXXX",NULL),*config=g_build_filename(directory,"geany.conf",NULL),*source=g_build_filename(directory,"source.txt",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(source,original,-1,NULL));gchar *args[]={"text-boundary-test","--new-instance","--no-session","--config",directory,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(250,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(6,args);
}
