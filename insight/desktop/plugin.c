/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <geanyplugin.h>
#include "runtime.h"
#include "navigation.h"
#include <string.h>
static GtkWidget *item;
static Navigation *navigation;
typedef struct { GeanyDocument *doc; gchar *scratch; } Host;
static const char *get_text(void *ctx) {
 Host *h=ctx; g_free(h->scratch);
 h->scratch=sci_get_contents(h->doc->editor->sci,sci_get_length(h->doc->editor->sci)+1);
 return h->scratch;
}
static const char *get_selection(void *ctx) {
 Host *h=ctx; g_free(h->scratch);
 h->scratch=sci_get_selection_contents(h->doc->editor->sci); return h->scratch;
}
static int replace_selection(void *ctx,const char *text) {
 Host *h=ctx; sci_replace_sel(h->doc->editor->sci,text); return 1;
}
static void activate(GtkMenuItem *menu,gpointer data) {
 (void)menu; GeanyPlugin *plugin=data;
 GtkWidget *dialog=gtk_file_chooser_dialog_new("Run Lua script",
 GTK_WINDOW(plugin->geany_data->main_widgets->window),GTK_FILE_CHOOSER_ACTION_OPEN,
 "Cancel",GTK_RESPONSE_CANCEL,"Run",GTK_RESPONSE_ACCEPT,NULL);
 if (gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
  gchar *source=NULL; gsize size=0; GError *io_error=NULL;
  GeanyDocument *doc=document_get_current();
  if (!doc) ui_set_statusbar(TRUE,"Lua: no active document");
  else if (!g_file_get_contents(path,&source,&size,&io_error)) {
   ui_set_statusbar(TRUE,"Lua: %s",io_error->message); g_error_free(io_error);
  } else {
   Host h={doc,NULL}; InsightHost host={&h,get_text,get_selection,replace_selection,navigation_index(navigation)};
   char error[512]; sci_start_undo_action(doc->editor->sci);
   int result=insight_run(&host,source,size,error,sizeof error);
   sci_end_undo_action(doc->editor->sci); g_free(h.scratch);
   ui_set_statusbar(TRUE,"Lua: %s",result ? (error[0] ? error : "runtime unavailable") : "completed");
  }
  g_free(source); g_free(path);
 }
 gtk_widget_destroy(dialog);
}
static gboolean init(GeanyPlugin *plugin,gpointer data) {
 (void)data; navigation=navigation_new(plugin);
 if(!navigation) return FALSE;
 item=gtk_menu_item_new_with_label("Insight: Run Lua script…");
 gtk_container_add(GTK_CONTAINER(plugin->geany_data->main_widgets->tools_menu),item);
 g_signal_connect(item,"activate",G_CALLBACK(activate),plugin);
 ui_add_document_sensitive(item); gtk_widget_show(item); return TRUE;
}
static void cleanup(GeanyPlugin *plugin,gpointer data) {
 (void)plugin; (void)data; gtk_widget_destroy(item); item=NULL; navigation_free(navigation); navigation=NULL;
}
void geany_load_module(GeanyPlugin *plugin) {
 plugin->info->name="Insight Lua"; plugin->info->description="Shared embedded Lua editor runtime";
 plugin->info->version="0.2.0"; plugin->info->author="Insight contributors";
 plugin->funcs->init=init; plugin->funcs->cleanup=cleanup;
 GEANY_PLUGIN_REGISTER(plugin,225);
}
