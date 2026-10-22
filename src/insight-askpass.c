/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>
#include "../insight/desktop/remote_credentials.h"
int main(int argc,char **argv) {
 const char *prompt=argc>1 ? argv[1] : "SSH authentication";
 gboolean confirmation=strstr(prompt,"yes/no")!=NULL;
 const char *config=g_getenv("GEANY_INSIGHT_SSH_CONFIG"),*target=g_getenv("GEANY_INSIGHT_SSH_TARGET"),*session=g_getenv("GEANY_INSIGHT_SSH_AUTH_SESSION");
 gboolean save=!confirmation && config && target && !g_strcmp0(g_getenv("GEANY_INSIGHT_SSH_SAVE_PASSWORD"),"1");
 gchar *credential=NULL;
 if(save) {
  credential=strstr(prompt,"passphrase") ? g_compute_checksum_for_string(G_CHECKSUM_SHA256,prompt,-1) : g_strdup(target);
  gchar *marker=session ? g_build_filename(session,credential,NULL) : NULL; gboolean first=FALSE;
  if(marker) { GFile *file=g_file_new_for_path(marker); GFileOutputStream *stream=g_file_create(file,G_FILE_CREATE_PRIVATE,NULL,NULL); first=stream!=NULL; if(stream) g_object_unref(stream); g_object_unref(file); }
  if(first) { gchar *password=insight_remote_password_load(config,credential,NULL); if(password) { fputs(password,stdout); fputc('\n',stdout); fflush(stdout); g_free(password); g_free(marker); g_free(credential); return 0; } }
  g_free(marker);
 }
 if(!gtk_init_check(NULL,NULL)) return 1;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Geany Insight — SSH",NULL,GTK_DIALOG_MODAL,"Cancel",GTK_RESPONSE_CANCEL,"Continue",GTK_RESPONSE_ACCEPT,NULL),*label=gtk_label_new(prompt),*entry=gtk_entry_new();
 gtk_label_set_line_wrap(GTK_LABEL(label),TRUE); gtk_label_set_max_width_chars(GTK_LABEL(label),72); gtk_label_set_selectable(GTK_LABEL(label),TRUE);
 gtk_entry_set_visibility(GTK_ENTRY(entry),confirmation); gtk_entry_set_activates_default(GTK_ENTRY(entry),TRUE);
 gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT); GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)); gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,8); gtk_box_pack_start(GTK_BOX(box),entry,FALSE,FALSE,8); gtk_widget_show_all(dialog);gtk_window_present(GTK_WINDOW(dialog)); gtk_widget_grab_focus(entry);
 int result=1;
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { const char *response=gtk_entry_get_text(GTK_ENTRY(entry)); if(!strchr(response,'\n') && !strchr(response,'\r')) {
  if(save) { GError *error=NULL; if(!insight_remote_password_save(config,credential,response,&error)) { GtkWidget *message=gtk_message_dialog_new(GTK_WINDOW(dialog),GTK_DIALOG_MODAL,GTK_MESSAGE_WARNING,GTK_BUTTONS_CLOSE,"SSH password was not saved: %s",error ? error->message : "write failed"); gtk_dialog_run(GTK_DIALOG(message)); gtk_widget_destroy(message); g_clear_error(&error); } }
  fputs(response,stdout); fputc('\n',stdout); fflush(stdout); result=0;
 } }
 gtk_widget_destroy(dialog); g_free(credential); return result;
}
