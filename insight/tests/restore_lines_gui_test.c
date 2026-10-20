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
static const gchar *original="head\r\nα original\r\nkeep\r\nlast\r\n";
static const gchar *edited="head\r\nβ edited\r\nkeep\r\nLAST\r\n";
static const gchar *partial="head\r\nα original\r\nkeep\r\nLAST\r\n";
static GtkWidget *window;
static GeanyDocument *source,*other;
static gchar *path;
static guint mode,tick;
static gboolean captured;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *dialog(void) {
 GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-restore-lines-dialog")) { found=p->data;break; }g_list_free(windows);return found;
}
static void text_is(GeanyDocument *doc,const gchar *expected) { gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void action(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void reset(const gchar *text,gint anchor,gint caret) {
 ScintillaObject *sci=source->editor->sci;source->readonly=FALSE;SEND(sci,SCI_SETREADONLY,0,0);SEND(sci,SCI_CLEARSELECTIONS,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);SEND(sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,0);SEND(sci,SCI_SETSELECTIONNANCHORVIRTUALSPACE,0,0);sci_set_text(sci,text);SEND(sci,SCI_SETSEL,anchor,caret);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);
}
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;
 if(!mode && !captured && g_getenv("GEANY_RESTORE_LINES_CAPTURE") && !tick++) return G_SOURCE_CONTINUE;
 if(!mode && !captured && g_getenv("GEANY_RESTORE_LINES_CAPTURE")) { captured=TRUE; GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(d),0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_RESTORE_LINES_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
 if(mode==1) { gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
 if(mode==2) {
  if(!tick++) { gtk_dialog_response(GTK_DIALOG(d),201);return G_SOURCE_CONTINUE; }
  gchar *text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,"α original\n");g_free(text);SEND(source->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"prefix\n");
 }
 if(mode==3) SEND(source->editor->sci,SCI_SETSEL,0,0);
 if(mode==4) { g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT))); }
 if(mode==5) other=document_new_file("other.c",NULL,"other");
 if(mode==6) { document_set_text_changed(source,FALSE);g_assert_true(document_close(source)); }
 gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);return G_SOURCE_REMOVE;
}
static void restore(guint which) { mode=which;tick=0;g_timeout_add(150,respond,NULL);action("insight-restore-lines"); }
static void original_view(const gchar *expected) {
 guint id=source->id;action("insight-original-contents");GeanyDocument *view=document_get_current();g_assert_true(view->id!=id);g_assert_true(view->readonly);g_assert_true(SEND(view->editor->sci,SCI_GETREADONLY,0,0));g_assert_null(view->real_path);text_is(view,expected);g_assert_false(SEND(view->editor->sci,SCI_CANUNDO,0,0));g_assert_true(document_close(view));g_assert_true(document_get_current()==source);
}
static gboolean step(gpointer data) {
 (void)data;source=document_get_current();if(!source) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));
 original_view(original);reset(edited,8,8);g_assert_true(document_save_file(source,FALSE));restore(0);text_is(source,partial);SEND(source->editor->sci,SCI_UNDO,0,0);text_is(source,edited);g_assert_false(SEND(source->editor->sci,SCI_CANUNDO,0,0));SEND(source->editor->sci,SCI_REDO,0,0);text_is(source,partial);
 gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,edited);g_free(disk);original_view(original);
 g_assert_true(document_reload_force(source,NULL));text_is(source,edited);original_view(original);
 reset(edited,strlen(edited),0);restore(0);text_is(source,original);g_assert_cmpint(SEND(source->editor->sci,SCI_GETANCHOR,0,0),>,SEND(source->editor->sci,SCI_GETCURRENTPOS,0,0));SEND(source->editor->sci,SCI_UNDO,0,0);text_is(source,edited);
 reset(edited,8,8);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);restore(1);text_is(source,edited);g_assert_false(SEND(source->editor->sci,SCI_CANUNDO,0,0));
 restore(2);gchar *prefixed=g_strconcat("prefix\n",edited,NULL);text_is(source,prefixed);g_free(prefixed);SEND(source->editor->sci,SCI_UNDO,0,0);text_is(source,edited);
 reset(edited,8,8);restore(3);text_is(source,edited);g_assert_false(SEND(source->editor->sci,SCI_CANUNDO,0,0));
 reset(edited,8,8);SEND(source->editor->sci,SCI_SETREADONLY,1,0);restore(4);text_is(source,edited);g_assert_false(SEND(source->editor->sci,SCI_CANUNDO,0,0));
 reset(edited,8,8);restore(5);g_assert_true(document_get_current()==other);text_is(other,"other");text_is(source,edited);g_assert_true(document_close(other));
 reset(edited,8,8);SEND(source->editor->sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);action("insight-restore-lines");g_assert_null(dialog());text_is(source,edited);
 reset(edited,8,8);SEND(source->editor->sci,SCI_ADDSELECTION,2,2);action("insight-restore-lines");g_assert_null(dialog());text_is(source,edited);
 reset(edited,8,8);SEND(source->editor->sci,SCI_SETSELECTIONNCARETVIRTUALSPACE,0,1);action("insight-restore-lines");g_assert_null(dialog());text_is(source,edited);
 reset("α",1,1);action("insight-restore-lines");g_assert_null(dialog());text_is(source,"α");
 reset("",0,0);SEND(source->editor->sci,SCI_ADDTEXT,3,(sptr_t)"a\0b");SEND(source->editor->sci,SCI_SETSEL,0,3);SEND(source->editor->sci,SCI_EMPTYUNDOBUFFER,0,0);action("insight-restore-lines");g_assert_null(dialog());g_assert_cmpint(SEND(source->editor->sci,SCI_GETLENGTH,0,0),==,3);g_assert_false(SEND(source->editor->sci,SCI_CANUNDO,0,0));
 gint keep=strstr(edited,"keep")-edited;reset(edited,keep,keep);action("insight-restore-lines");g_assert_null(dialog());text_is(source,edited);g_assert_false(SEND(source->editor->sci,SCI_CANUNDO,0,0));
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);reset(edited,8,8);restore(1);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);
 reset(edited,8,8);restore(6);source=document_open_file(path,FALSE,NULL,NULL);g_assert_nonnull(source);original_view(edited);
 GeanyDocument *old=source;GeanyDocument *blank=document_new_file(NULL,NULL,NULL);source=blank;sci_set_text(blank->editor->sci,"inserted");SEND(blank->editor->sci,SCI_SETSEL,0,0);SEND(blank->editor->sci,SCI_EMPTYUNDOBUFFER,0,0);restore(0);text_is(blank,"");SEND(blank->editor->sci,SCI_UNDO,0,0);text_is(blank,"inserted");document_set_text_changed(blank,FALSE);g_assert_true(document_close(blank));source=old;
 gchar *large=g_strnfill(8*1024*1024+1,'x');GeanyDocument *big=document_new_file("large.txt",NULL,large);g_free(large);action("insight-restore-lines");g_assert_null(dialog());action("insight-original-contents");g_assert_true(document_get_current()==big);g_assert_true(document_close(big));
 g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,edited);g_free(disk);
 g_print("Restore Lines GUI: original baseline after Save/Reload, exact UTF-8/CRLF, partial/full/reverse blocks, one Undo/Redo, original read-only view, clipboard Copy, stale/edit/selection/readonly/switched/closed guards, invalid/unsupported/large inputs, new/reopened buffers and unchanged disk passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Restore Lines GUI timeout mode %u",mode);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("restore-lines-XXXXXX",NULL),*config=g_build_filename(dir,"geany.conf",NULL);path=g_build_filename(dir,"source.c",NULL);g_assert_true(g_file_set_contents(path,original,-1,NULL));g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\nshow_keep_edit_history_on_reload_msg=false\n",-1,NULL));gchar *args[]={"restore-lines","--new-instance","--no-session","--config",dir,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(400,step,NULL);g_timeout_add_seconds(40,timeout,NULL);return main_lib(6,args);
}
