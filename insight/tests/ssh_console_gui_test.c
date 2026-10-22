/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window,*output,*state,*input;static guint phase,ticks;static gchar *directory,*profile;
static GtkWidget *named(GtkWidget *root,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static void click(const gchar *name) { GtkWidget *button=named(window,name);g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button)); }
static gchar *transcript(void) { GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(output));GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);return gtk_text_buffer_get_text(buffer,&a,&b,FALSE); }
static guint ready_count(const gchar *text) { guint count=0;const gchar *p=text;while((p=strstr(p,"\nINSIGHT_SSH_OK\n"))) { count++;p++; }return count; }
static void send(const gchar *command) { gtk_entry_set_text(GTK_ENTRY(input),command);g_signal_emit_by_name(input,"activate"); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!window) {
  window=gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));GtkWidget *page=named(window,"insight-ssh-page");g_assert_nonnull(page);GtkWidget *book=gtk_widget_get_parent(page);g_assert_true(GTK_IS_NOTEBOOK(book));gtk_widget_show(book);gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),page));gtk_window_present(GTK_WINDOW(window));
  output=named(window,"insight-ssh-output");state=named(window,"insight-ssh-status");input=named(window,"insight-ssh-input");g_assert_nonnull(output);g_assert_nonnull(state);g_assert_nonnull(input);g_assert_true(gtk_label_get_selectable(GTK_LABEL(state)));g_assert_cmpint(gtk_label_get_ellipsize(GTK_LABEL(state)),==,PANGO_ELLIPSIZE_NONE);
  gtk_entry_set_text(GTK_ENTRY(named(window,"insight-ssh-host"))," 127.0.0.1 ");gchar *user=g_strdup_printf(" %s ",g_getenv("INSIGHT_SSH_TEST_USER"));gtk_entry_set_text(GTK_ENTRY(named(window,"insight-ssh-user")),user);g_free(user);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-ssh-identity")),g_getenv("INSIGHT_SSH_TEST_KEY"));gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(window,"insight-ssh-port")),g_ascii_strtod(g_getenv("INSIGHT_SSH_TEST_PORT"),NULL));
  click("insight-ssh-connect");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(state)),"Starting SSH"));send("printf '\\nINSIGHT_SSH_OK\\n'");return G_SOURCE_CONTINUE;
 }
 gchar *text=transcript();const gchar *label=gtk_label_get_text(GTK_LABEL(state));
 if(phase==0 && ready_count(text)>=1) {
  /* Old EOF/wait callbacks must not tear down a replacement connection. */
  click("insight-ssh-disconnect");click("insight-ssh-connect");send("printf '\\nINSIGHT_SSH_OK\\n'");phase++;
 } else if(phase==1 && ready_count(text)>=2) { send("exit 7");phase++; }
 else if(phase==2 && strstr(label,"status 7")) {
  g_assert_cmpuint(ready_count(text),==,2);g_assert_nonnull(strstr(text,"status 7"));gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(window,"insight-ssh-port")),g_ascii_strtod(g_getenv("INSIGHT_SSH_REFUSED_PORT"),NULL));click("insight-ssh-connect");phase++;
 } else if(phase==3 && strstr(label,"status 255")) {
  g_assert_nonnull(strstr(text,"Connection refused"));g_assert_nonnull(strstr(text,"status 255"));g_assert_cmpint(g_remove(g_getenv("INSIGHT_SSH_TEST_WRAPPER")),==,0);click("insight-ssh-connect");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(state)),"was not found"));phase++;
 } else if(phase==4) {
  g_assert_nonnull(strstr(text,"OpenSSH client 'ssh' was not found"));gtk_entry_set_text(GTK_ENTRY(named(window,"insight-ssh-host")),"");click("insight-ssh-connect");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(state)),"Use a host"));phase++;
 } else if(phase==5) {
  g_assert_nonnull(strstr(text,"Use a host"));g_print("SSH console GUI: real OpenSSH loopback command, whitespace normalization, reconnect during old callbacks, drained output and exit 7, refused connection and exit 255, missing client and invalid input visibly reported\n");g_free(text);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 if(++ticks%20==0) g_printerr("SSH phase %u: %s\n",phase,label);
 g_free(text);return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("SSH console timeout at phase %u: %s",phase,state ? gtk_label_get_text(GTK_LABEL(state)) : "not started");return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY") || !g_getenv("INSIGHT_SSH_TEST_PORT")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("ssh-console-%u",g_random_int());directory=g_build_filename(cwd,name,NULL);profile=g_build_filename(directory,"profile",NULL);g_free(cwd);g_free(name);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *argv[]={"ssh-console-gui","--new-instance","--no-session","--config",profile,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(250,step,NULL);g_timeout_add_seconds(40,timeout,NULL);gint result=main_lib(5,argv);g_free(directory);g_free(profile);return result;
}
