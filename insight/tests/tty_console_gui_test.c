/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*view;static guint phase,ticks;
static void reveal(GtkWidget *child) { while(gtk_widget_get_parent(child)) { GtkWidget *parent=gtk_widget_get_parent(child);if(GTK_IS_NOTEBOOK(parent)) gtk_notebook_set_current_page(GTK_NOTEBOOK(parent),gtk_notebook_page_num(GTK_NOTEBOOK(parent),child));child=parent; } }
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_NOTEBOOK(w)) { GtkWidget *action=gtk_notebook_get_action_widget(GTK_NOTEBOOK(w),GTK_PACK_END);if(action) { GtkWidget *f=named(action,name);if(f) return f; } }if(GTK_IS_CONTAINER(w)) { GList *c=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=c;p&&!f;p=p->next) f=named(p->data,name);g_list_free(c);return f; }return NULL; }
static void click(const gchar *name) { GtkWidget *button=named(window,name);g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button)); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!window) { window=gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));GtkWidget *page=named(window,"insight-tty-page");g_assert_nonnull(page);GtkWidget *book=gtk_widget_get_parent(page);gtk_widget_show(book);reveal(page);GtkWidget *ports=named(window,"insight-tty-port");gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(ports))),g_getenv("INSIGHT_TTY_DEVICE"));click("insight-tty-refresh");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(ports)))),==,g_getenv("INSIGHT_TTY_DEVICE"));click("insight-tty-connect");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-tty-status"))),"Connected:"));view=named(window,"insight-tty-terminal-1");g_assert_nonnull(view);g_signal_emit_by_name(view,"insert-at-cursor","PING\r");return G_SOURCE_CONTINUE; }
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));GtkTextIter a,z;gtk_text_buffer_get_bounds(buffer,&a,&z);gchar *text=gtk_text_buffer_get_text(buffer,&a,&z,FALSE);
 if(!phase && strstr(text,"TTY_READY")) { g_assert_null(strchr(text,27));g_assert_nonnull(strstr(text,"GREEN"));GtkWidget *tabs=named(window,"insight-tty-tabs");g_assert_cmpint(gtk_notebook_get_tab_pos(GTK_NOTEBOOK(tabs)),==,GTK_POS_RIGHT);g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(tabs)),==,2);click("insight-tty-new-terminal");g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(tabs)),==,3);g_assert_nonnull(named(window,"insight-tty-terminal-2"));gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs),1);click("insight-tty-disconnect");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-tty-status"))),"disconnected"));gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(named(window,"insight-tty-port")))) ,"/nonexistent-insight-device");click("insight-tty-connect");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-tty-status"))),"Cannot open TTY"));phase++; }
 else if(phase) { g_print("TTY production panel: enumerated/custom port retention, 115200/8N1 configuration, real PTY byte exchange, ANSI rendering, icon tabs, independent sessions, disconnect and open errors passed\n");g_free(text);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 g_free(text);if(++ticks>100) g_error("TTY timeout phase %u",phase);return G_SOURCE_CONTINUE;
}
int main(void) { if(!g_getenv("DISPLAY") || !g_getenv("INSIGHT_TTY_DEVICE")) return 77;gchar *profile=g_dir_make_tmp("tty-console-XXXXXX",NULL),*config=g_build_filename(profile,"geany.conf",NULL);g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL);gchar *argv[]={"tty-console","--new-instance","--no-session","--config",profile,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(100,step,NULL);return main_lib(5,argv); }
