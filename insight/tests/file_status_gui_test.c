/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;static GeanyDocument *doc;static ScintillaObject *sci;static guint phase;static const gchar *original="α\r\nline\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL; }
static gboolean has_text(GtkWidget *w,const gchar *text) { if(GTK_IS_LABEL(w) && strstr(gtk_label_get_text(GTK_LABEL(w)),text)) return TRUE;if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));gboolean found=FALSE;for(GList *p=children;p && !found;p=p->next) found=has_text(p->data,text);g_list_free(children);return found; }return FALSE; }
static void status(const gchar *expected) { GtkWidget *item=named(window,"insight-show-file-status");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));g_assert_true(has_text(ui_lookup_widget(window,"statusbar"),expected)); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!phase++) { doc=document_get_current();sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));status("source.txt — lines: 3, bytes: 9 — Saved — Writable");return G_SOURCE_CONTINUE; }
 if(g_getenv("GEANY_FILE_STATUS_CAPTURE")) { GtkWidget *bar=ui_lookup_widget(window,"statusbar");gint x,y;g_assert_true(gtk_widget_translate_coordinates(bar,window,0,0,&x,&y));gint top=MAX(0,y-80),width=MIN(1000,gtk_widget_get_allocated_width(bar)),height=y-top+gtk_widget_get_allocated_height(bar);GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(window),x,top,width,height);g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_FILE_STATUS_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
 gint anchor=SEND(sci,SCI_GETANCHOR,0,0),caret=SEND(sci,SCI_GETCURRENTPOS,0,0);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));status("Saved — Writable");g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,anchor);g_assert_cmpint(SEND(sci,SCI_GETCURRENTPOS,0,0),==,caret);g_assert_false(SEND(sci,SCI_GETMODIFY,0,0));
 SEND(sci,SCI_INSERTTEXT,0,(sptr_t)"x");gint history=SEND(sci,SCI_GETUNDOCURRENT,0,0);status("source.txt — lines: 3, bytes: 10 — Modified — Writable");g_assert_cmpint(SEND(sci,SCI_GETUNDOCURRENT,0,0),==,history);
 SEND(sci,SCI_SETREADONLY,1,0);status("Modified — Read-only");SEND(sci,SCI_SETREADONLY,0,0);SEND(sci,SCI_UNDO,0,0);status("Saved — Writable");
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE);status("source.txt — строк: 3, байт: 9 — Сохранён — Доступен для записи");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-en")),TRUE);
 gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,original);g_free(text);
 GeanyDocument *untitled=document_new_file(NULL,NULL,NULL);g_assert_nonnull(untitled);status("Untitled — lines: 1, bytes: 0 — Saved — Writable");g_assert_false(SEND(untitled->editor->sci,SCI_CANUNDO,0,0));
 g_print("File Status GUI: exact UTF-8 bytes/mixed-EOL line counts, saved/modified/read-only/untitled, live RU/EN and unchanged text/selection/Undo passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("File status GUI timeout");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("file-status-XXXXXX",NULL),*config=g_build_filename(directory,"geany.conf",NULL),*source=g_build_filename(directory,"source.txt",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(source,original,-1,NULL));gchar *args[]={"file-status-test","--new-instance","--no-session","--config",directory,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(250,step,NULL);g_timeout_add_seconds(25,timeout,NULL);return main_lib(6,args);
}
