/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>
#include <gdk/gdkkeysyms.h>
static GtkWidget *window,*output,*state,*input,*output2,*input2,*tabs;static guint phase,ticks;static gchar *directory,*profile;
static void reveal(GtkWidget *child) { while(gtk_widget_get_parent(child)) { GtkWidget *parent=gtk_widget_get_parent(child);if(GTK_IS_NOTEBOOK(parent)) gtk_notebook_set_current_page(GTK_NOTEBOOK(parent),gtk_notebook_page_num(GTK_NOTEBOOK(parent),child));child=parent; } }
static GtkWidget *named(GtkWidget *root,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_NOTEBOOK(root)) { for(guint i=0;i<2;i++) { GtkWidget *action=gtk_notebook_get_action_widget(GTK_NOTEBOOK(root),i ? GTK_PACK_END : GTK_PACK_START);if(action) { GtkWidget *found=named(action,name);if(found) return found; } } }
 if(GTK_IS_CONTAINER(root)) { GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static void click(const gchar *name) { GtkWidget *button=named(window,name);g_assert_nonnull(button);gtk_button_clicked(GTK_BUTTON(button)); }
static gchar *view_text(GtkWidget *view) { GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);return gtk_text_buffer_get_text(buffer,&a,&b,FALSE); }
static gchar *transcript(void) { return view_text(output); }
static guint ready_count(const gchar *text) { guint count=0;const gchar *p=text;while((p=strstr(p,"\nINSIGHT_SSH_OK\n"))) { count++;p++; }return count; }
static void send_to(GtkWidget *view,const gchar *command) {
 g_signal_emit_by_name(view,"insert-at-cursor",command);GdkEvent *event=gdk_event_new(GDK_KEY_PRESS);event->key.window=g_object_ref(gtk_widget_get_window(window));event->key.keyval=GDK_KEY_Return;gboolean handled=FALSE;g_signal_emit_by_name(view,"key-press-event",event,&handled);g_assert_true(handled);gdk_event_free(event);
}
static void send_line(const gchar *command) { send_to(input,command); }
static void transfer_path(const gchar *widget,const gchar *name) { gchar *path=g_build_filename(g_getenv("INSIGHT_SSH_TRANSFER_DIR"),name,NULL);gtk_entry_set_text(GTK_ENTRY(named(window,widget)),path);g_free(path); }
static gboolean transfer_complete(void) { return strstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-scp-status"))),"Transfer completed")!=NULL; }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!window) {
  window=gtk_widget_get_toplevel(GTK_WIDGET(document_get_current()->editor->sci));GtkWidget *page=named(window,"insight-ssh-page");g_assert_nonnull(page);GtkWidget *book=gtk_widget_get_parent(page);g_assert_true(GTK_IS_NOTEBOOK(book));gtk_widget_show(book);reveal(page);gtk_window_present(GTK_WINDOW(window));
  tabs=named(window,"insight-ssh-tabs");g_assert_nonnull(tabs);g_assert_cmpint(gtk_notebook_get_tab_pos(GTK_NOTEBOOK(tabs)),==,GTK_POS_RIGHT);g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(tabs)),==,3);output=named(window,"insight-ssh-output");state=named(window,"insight-ssh-status");input=named(window,"insight-ssh-output");g_assert_nonnull(output);g_assert_nonnull(state);g_assert_nonnull(input);g_assert_true(gtk_label_get_selectable(GTK_LABEL(state)));g_assert_cmpint(gtk_label_get_ellipsize(GTK_LABEL(state)),==,PANGO_ELLIPSIZE_NONE);
  gtk_entry_set_text(GTK_ENTRY(named(window,"insight-ssh-host"))," 127.0.0.1 ");gchar *user=g_strdup_printf(" %s ",g_getenv("INSIGHT_SSH_TEST_USER"));gtk_entry_set_text(GTK_ENTRY(named(window,"insight-ssh-user")),user);g_free(user);gtk_entry_set_text(GTK_ENTRY(named(window,"insight-ssh-identity")),g_getenv("INSIGHT_SSH_TEST_KEY"));gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(window,"insight-ssh-port")),g_ascii_strtod(g_getenv("INSIGHT_SSH_TEST_PORT"),NULL));
  click("insight-ssh-connect");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(state)),"Starting SSH"));send_line("printf '\\nINSIGHT_SSH_OK\\n'");return G_SOURCE_CONTINUE;
 }
 gchar *text=transcript();const gchar *label=gtk_label_get_text(GTK_LABEL(state));
 if(phase==0 && ready_count(text)>=1) { gtk_window_resize(GTK_WINDOW(window),1280,900);phase=20;
 } else if(phase==20) { send_line("printf '\\nPTY_SIZE '; stty size");phase=21;
 } else if(phase==21 && strstr(text,"\nPTY_SIZE ")) { gint rows=0,cols=0;g_assert_cmpint(sscanf(strstr(text,"\nPTY_SIZE ")+1,"PTY_SIZE %d %d",&rows,&cols),==,2);g_assert_cmpint(rows,==,GPOINTER_TO_INT(g_object_get_data(G_OBJECT(output),"insight-terminal-rows")));g_assert_cmpint(cols,==,GPOINTER_TO_INT(g_object_get_data(G_OBJECT(output),"insight-terminal-columns")));
  click("insight-ssh-new-terminal");g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(tabs)),==,4);output2=named(window,"insight-ssh-output-2");input2=named(window,"insight-ssh-output-2");g_assert_nonnull(output2);g_assert_nonnull(input2);click("insight-ssh-connect");send_to(input2,"printf '\\nSESSION_TWO\\n'");phase=10;
 } else if(phase==10) {
  gchar *second=view_text(output2);gboolean ready=strstr(second,"\nSESSION_TWO\n")!=NULL;g_free(second);if(ready) { gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs),2);send_line("printf '\\nSESSION_ONE_ALIVE\\n'");phase=11; }
 } else if(phase==11 && strstr(text,"\nSESSION_ONE_ALIVE\n")) {
  transfer_path("insight-scp-local","transfer-source.bin");transfer_path("insight-scp-remote","uploaded.bin");click("insight-scp-send");phase=12;
 } else if(phase==12 && transfer_complete()) {
  transfer_path("insight-scp-local","received.bin");click("insight-scp-receive");phase=13;
 } else if(phase==13 && transfer_complete()) {
  transfer_path("insight-scp-local","folder-source");transfer_path("insight-scp-remote","uploaded.tar.gz");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-scp-folder")),TRUE);gtk_combo_box_set_active(GTK_COMBO_BOX(named(window,"insight-scp-packing")),2);click("insight-scp-send");phase=14;
 } else if(phase==14 && transfer_complete()) {
  transfer_path("insight-scp-local","received.gz");transfer_path("insight-scp-remote","transfer-source.bin");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(window,"insight-scp-folder")),FALSE);gtk_combo_box_set_active(GTK_COMBO_BOX(named(window,"insight-scp-packing")),1);click("insight-scp-receive");phase=15;
 } else if(phase==15 && transfer_complete()) {
  click("insight-ssh-disconnect");click("insight-ssh-connect");send_line("printf '\\nINSIGHT_SSH_OK\\n'");phase=1;
 } else if(phase==1 && ready_count(text)>=2) { send_line("exit 7");phase++; }
 else if(phase==2 && strstr(label,"status 7") && strstr(text,"status 7")) {
  g_assert_cmpuint(ready_count(text),==,2);g_assert_nonnull(strstr(text,"status 7"));gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(window,"insight-ssh-port")),g_ascii_strtod(g_getenv("INSIGHT_SSH_REFUSED_PORT"),NULL));click("insight-ssh-connect");phase++;
 } else if(phase==3 && strstr(label,"status 255") && strstr(text,"status 255")) {
  g_assert_nonnull(strstr(text,"Connection refused"));g_assert_nonnull(strstr(text,"status 255"));g_assert_cmpint(g_remove(g_getenv("INSIGHT_SSH_TEST_WRAPPER")),==,0);click("insight-ssh-connect");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(state)),"was not found"));phase++;
 } else if(phase==4) {
  g_assert_nonnull(strstr(text,"OpenSSH client 'ssh' was not found"));gtk_entry_set_text(GTK_ENTRY(named(window,"insight-ssh-host")),"");click("insight-ssh-connect");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(state)),"Use a host"));phase++;
 } else if(phase==5) {
  g_assert_nonnull(strstr(text,"Use a host"));gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs),3);send_to(input2,"printf '\\nSESSION_TWO_STILL_ALIVE\\n'");phase=6;
 } else if(phase==6) {
  gchar *second=view_text(output2);gboolean ready=strstr(second,"\nSESSION_TWO_STILL_ALIVE\n")!=NULL;g_free(second);if(!ready) { g_free(text);return G_SOURCE_CONTINUE; }g_print("SSH console GUI: real OpenSSH loopback command, whitespace normalization, reconnect during old callbacks, drained output and exit 7, refused connection and exit 255, missing client and invalid input visibly reported\n");g_free(text);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
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
